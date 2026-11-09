#include "updatemanager.h"
#include "db/databasemanager.h"
#include "server/remoteapiclient.h"

#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QEventLoop>
#include <QTimer>
#include <QUrl>
#include <QDateTime>
#include <QByteArray>
#include <QRegularExpression>

namespace {
const qint64 kChunkSize = 1024 * 1024;   // 分块上传：1MB，避免把大安装包整包塞进内存

// PC 与手机 APK 各用一份元数据/临时文件，互不覆盖
QString metaName(PackageKind k)
{
    return k == PackageKind::Android ? QStringLiteral("update_android.json")
                                     : QStringLiteral("update.json");
}
QString partName(PackageKind k)
{
    return k == PackageKind::Android ? QStringLiteral(".uploading_android.part")
                                     : QStringLiteral(".uploading.part");
}
QString extName(PackageKind k)
{
    return k == PackageKind::Android ? QStringLiteral(".apk") : QStringLiteral(".exe");
}
QString globName(PackageKind k)
{
    return k == PackageKind::Android ? QStringLiteral("*.apk") : QStringLiteral("*.exe");
}
QString kindTag(PackageKind k)
{
    return k == PackageKind::Android ? QStringLiteral("android") : QStringLiteral("pc");
}
QString infoApi(PackageKind k)
{
    return k == PackageKind::Android ? QStringLiteral("/api/update/android/info")
                                     : QStringLiteral("/api/update/info");
}
QString packageApi(PackageKind k)
{
    return k == PackageKind::Android ? QStringLiteral("/api/update/android/package")
                                     : QStringLiteral("/api/update/package");
}

// 安装包里带 version.txt（两行：version= / stamp=），保证安装后版本号仍是构建日期，
// 而不会变成安装时间；开发目录没有该文件时回退到 exe 的最后修改时间。
void readVersionFile(QString *ver, QString *stamp)
{
    QFile f(QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("version.txt")));
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        return;
    const QString text = QString::fromUtf8(f.readAll());
    const QStringList lines = text.split(QLatin1Char('\n'));
    for (const QString &raw : lines) {
        const QString line = raw.trimmed();
        if (line.startsWith(QLatin1String("version=")))
            *ver = line.mid(8).trimmed();
        else if (line.startsWith(QLatin1String("stamp=")))
            *stamp = line.mid(6).trimmed();
    }
}

QString exeVersion()
{
    return QFileInfo(QCoreApplication::applicationFilePath())
        .lastModified().toString(QStringLiteral("yyyy-MM-dd"));
}

QString exeStamp()
{
    return QFileInfo(QCoreApplication::applicationFilePath())
        .lastModified().toString(QStringLiteral("yyyyMMddHHmmss"));
}

// 逐块复制并回调进度；上传/下载的服务器本机分支共用
bool copyWithProgress(const QString &src, const QString &dst, QString *err,
                      const std::function<void(int, qint64, qint64)> &progress)
{
    QFile in(src);
    QFile out(dst);
    if (!in.open(QIODevice::ReadOnly)) {
        if (err) *err = QStringLiteral("无法读取文件：%1").arg(src);
        return false;
    }
    if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (err) *err = QStringLiteral("无法写入文件：%1").arg(dst);
        return false;
    }
    const qint64 total = in.size();
    qint64 done = 0;
    char buf[64 * 1024];
    while (!in.atEnd()) {
        const qint64 n = in.read(buf, sizeof(buf));
        if (n <= 0)
            break;
        if (out.write(buf, n) != n) {
            if (err) *err = QStringLiteral("写文件失败，磁盘空间可能不足");
            out.close();
            QFile::remove(dst);
            return false;
        }
        done += n;
        if (progress)
            progress(total > 0 ? static_cast<int>(done * 100 / total) : 0, done, total);
    }
    out.close();
    return true;
}

QString cleanHost(QString host)
{
    host = host.trimmed();
    if (host.startsWith(QLatin1String("http://"))) host = host.mid(7);
    else if (host.startsWith(QLatin1String("https://"))) host = host.mid(8);
    const int slash = host.indexOf(QLatin1Char('/'));
    if (slash >= 0) host.truncate(slash);
    return host;
}

UpdateInfo infoFromJson(const QJsonObject &o)
{
    UpdateInfo info;
    info.available = o.value(QStringLiteral("available")).toBool();
    info.version = o.value(QStringLiteral("version")).toString();
    info.buildStamp = o.value(QStringLiteral("build_stamp")).toString();
    info.fileName = o.value(QStringLiteral("file")).toString();
    info.size = static_cast<qint64>(o.value(QStringLiteral("size")).toDouble());
    info.uploadedAt = o.value(QStringLiteral("uploaded_at")).toString();
    return info;
}
} // namespace

UpdateManager &UpdateManager::instance()
{
    static UpdateManager inst;
    return inst;
}

QString UpdateManager::localVersion() const
{
    QString ver, stamp;
    readVersionFile(&ver, &stamp);
    return ver.isEmpty() ? exeVersion() : ver;
}

QString UpdateManager::localStamp() const
{
    QString ver, stamp;
    readVersionFile(&ver, &stamp);
    return stamp.isEmpty() ? exeStamp() : stamp;
}

bool UpdateManager::hasUpdate(const UpdateInfo &info) const
{
    if (!info.available)
        return false;
    // 构建戳定长字符串，可直接按字典序比较；同一天重新编译也能识别为新版本
    const QString mine = localStamp();
    if (!info.buildStamp.isEmpty() && !mine.isEmpty())
        return info.buildStamp > mine;
    return info.version > localVersion();
}

QString UpdateManager::readableSize(qint64 bytes)
{
    if (bytes >= 1024 * 1024)
        return QStringLiteral("%1 MB").arg(bytes / 1024.0 / 1024.0, 0, 'f', 1);
    if (bytes >= 1024)
        return QStringLiteral("%1 KB").arg(bytes / 1024.0, 0, 'f', 1);
    return QStringLiteral("%1 B").arg(bytes);
}

QString UpdateManager::packageDirectory()
{
    const QString root = DatabaseManager::instance().storageRoot();
    if (root.isEmpty())
        return QString();
    return QDir::cleanPath(root + QStringLiteral("/安装包"));
}

QString UpdateManager::metaFilePath(PackageKind kind)
{
    const QString dir = packageDirectory();
    return dir.isEmpty() ? QString() : QDir(dir).filePath(metaName(kind));
}

QString UpdateManager::partFilePath(PackageKind kind)
{
    const QString dir = packageDirectory();
    return dir.isEmpty() ? QString() : QDir(dir).filePath(partName(kind));
}

QString UpdateManager::localPackagePath(PackageKind kind)
{
    const UpdateInfo info = readLocalMeta(kind);
    if (!info.available || info.fileName.isEmpty())
        return QString();
    return QDir(packageDirectory()).filePath(info.fileName);
}

UpdateInfo UpdateManager::readLocalMeta(PackageKind kind)
{
    const QString dir = packageDirectory();
    const QString path = metaFilePath(kind);
    if (dir.isEmpty() || !QFileInfo::exists(path))
        return UpdateInfo();
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return UpdateInfo();
    QJsonParseError pe;
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &pe);
    if (pe.error != QJsonParseError::NoError || !doc.isObject())
        return UpdateInfo();
    UpdateInfo info = infoFromJson(doc.object());
    info.available = false;
    if (info.version.isEmpty() || info.fileName.isEmpty())
        return info;
    // 元数据在但文件丢了，视为没有可用安装包
    if (!QFileInfo::exists(QDir(dir).filePath(info.fileName)))
        return info;
    info.available = true;
    return info;
}

bool UpdateManager::writeLocalMeta(const QString &fileName, const QString &version,
                                   const QString &stamp, qint64 size, QString *err,
                                   PackageKind kind)
{
    const QString dir = packageDirectory();
    if (dir.isEmpty() || !QDir().mkpath(dir)) {
        if (err) *err = QStringLiteral("无法创建服务器安装包目录");
        return false;
    }
    QJsonObject o;
    o[QStringLiteral("version")] = version;
    o[QStringLiteral("build_stamp")] = stamp;
    o[QStringLiteral("file")] = fileName;
    o[QStringLiteral("size")] = size;
    o[QStringLiteral("uploaded_at")] =
        QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"));
    QFile f(metaFilePath(kind));
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (err) *err = QStringLiteral("无法写入版本信息文件");
        return false;
    }
    f.write(QJsonDocument(o).toJson(QJsonDocument::Compact));
    f.close();
    return true;
}

QString UpdateManager::packageVersion(const QString &srcPath, PackageKind kind)
{
    if (kind == PackageKind::Pc)
        return instance().localVersion();
    // APK 文件名形如 ND_DrawingManagement_v1.0_2026-11-09_debug.apk，优先取里面的构建日期
    const QRegularExpression re(QStringLiteral("\\d{4}-\\d{2}-\\d{2}"));
    const QRegularExpressionMatch m = re.match(QFileInfo(srcPath).fileName());
    if (m.hasMatch() && QDate::fromString(m.captured(0), Qt::ISODate).isValid())
        return m.captured(0);
    return QFileInfo(srcPath).lastModified().toString(QStringLiteral("yyyy-MM-dd"));
}

QString UpdateManager::packageStamp(const QString &srcPath, PackageKind kind)
{
    if (kind == PackageKind::Pc)
        return instance().localStamp();
    return QFileInfo(srcPath).lastModified().toString(QStringLiteral("yyyyMMddHHmmss"));
}

bool UpdateManager::installLocalPackage(const QString &srcPath, const QString &version,
                                        const QString &stamp, QString *err,
                                        const std::function<void(int)> &progress,
                                        PackageKind kind)
{
    const QFileInfo si(srcPath);
    if (!si.exists() || !si.isFile()) {
        if (err) *err = QStringLiteral("安装包不存在");
        return false;
    }
    const QString name = si.fileName();
    if (!name.endsWith(extName(kind), Qt::CaseInsensitive)) {
        if (err) *err = QStringLiteral("请选择 %1 格式的安装包").arg(extName(kind));
        return false;
    }
    const QString dir = packageDirectory();
    if (dir.isEmpty() || !QDir().mkpath(dir)) {
        if (err) *err = QStringLiteral("无法创建服务器安装包目录");
        return false;
    }
    const QString part = partFilePath(kind);
    QFile::remove(part);
    const bool copied = copyWithProgress(srcPath, part, err,
        [&progress](int p, qint64, qint64) { if (progress) progress(p); });
    if (!copied) {
        QFile::remove(part);
        return false;
    }
    const QString target = QDir(dir).filePath(name);
    if (QFile::exists(target) && !QFile::remove(target)) {
        QFile::remove(part);
        if (err) *err = QStringLiteral("旧安装包被占用，无法替换");
        return false;
    }
    if (!QFile::rename(part, target)) {
        QFile::remove(part);
        if (err) *err = QStringLiteral("安装包保存失败");
        return false;
    }
    // 同类型安装包只保留一份（PC 的 exe 与手机 apk 各一份）
    const QFileInfoList others = QDir(dir).entryInfoList(
        QStringList() << globName(kind), QDir::Files);
    for (const QFileInfo &fi : others) {
        if (fi.absoluteFilePath() != target)
            QFile::remove(fi.absoluteFilePath());
    }
    return writeLocalMeta(name, version, stamp, si.size(), err, kind);
}

QString UpdateManager::serverBaseUrl()
{
    const QString host = cleanHost(DatabaseManager::serverHost());
    const int port = DatabaseManager::serverPort();
    return QStringLiteral("http://%1:%2").arg(host).arg(port > 0 ? port : 8080);
}

QJsonObject UpdateManager::postJson(const QString &path, const QJsonObject &payload,
                                    QString *err, int timeoutMs) const
{
    auto fail = [err](const QString &msg) {
        if (err) *err = msg;
        return QJsonObject();
    };
    if (cleanHost(DatabaseManager::serverHost()).isEmpty())
        return fail(QStringLiteral("未配置服务器地址"));
    QNetworkAccessManager nam;
    QNetworkRequest req(QUrl(serverBaseUrl() + path));
    req.setHeader(QNetworkRequest::ContentTypeHeader,
                  QByteArray("application/json; charset=utf-8"));
    QNetworkReply *reply = nam.post(req, QJsonDocument(payload).toJson(QJsonDocument::Compact));
    QEventLoop loop;
    QTimer timer;
    timer.setSingleShot(true);
    QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    timer.start(timeoutMs);
    loop.exec();
    timer.stop();
    if (!reply->isFinished()) {
        reply->abort();
        reply->deleteLater();
        return fail(QStringLiteral("服务器响应超时"));
    }
    const QByteArray data = reply->readAll();
    const QNetworkReply::NetworkError netErr = reply->error();
    const QString netText = reply->errorString();
    reply->deleteLater();
    if (netErr != QNetworkReply::NoError)
        return fail(QStringLiteral("无法连接服务器：%1").arg(netText));
    QJsonParseError pe;
    const QJsonDocument doc = QJsonDocument::fromJson(data, &pe);
    if (pe.error != QJsonParseError::NoError || !doc.isObject())
        return fail(QStringLiteral("服务器返回数据格式错误"));
    return doc.object();
}

UpdateInfo UpdateManager::checkUpdate(QString *err, PackageKind kind) const
{
    if (DatabaseManager::isServer())
        return readLocalMeta(kind);
    // PC 端统一走已有的 POST /api/update/info，用 kind 区分手机 APK；
    // 手机 APP 用 GET /api/update/android/info（服务端另有路由）
    QJsonObject payload;
    if (kind == PackageKind::Android)
        payload[QStringLiteral("kind")] = QStringLiteral("android");
    const QJsonObject res = postJson(QStringLiteral("/api/update/info"), payload, err, 8000);
    if (res.isEmpty())
        return UpdateInfo();
    if (!res.value(QStringLiteral("ok")).toBool()) {
        if (err) *err = res.value(QStringLiteral("error")).toString(QStringLiteral("查询失败"));
        return UpdateInfo();
    }
    return infoFromJson(res);
}

bool UpdateManager::uploadPackage(const QString &srcPath, QString *err,
                                  const std::function<void(int)> &progress,
                                  PackageKind kind) const
{
    const QFileInfo si(srcPath);
    if (!si.exists() || !si.isFile()) {
        if (err) *err = QStringLiteral("安装包不存在");
        return false;
    }
    const QString name = si.fileName();
    if (!name.endsWith(extName(kind), Qt::CaseInsensitive)) {
        if (err) *err = QStringLiteral("请选择 %1 格式的安装包").arg(extName(kind));
        return false;
    }
    const QString version = packageVersion(srcPath, kind);
    const QString stamp = packageStamp(srcPath, kind);
    if (DatabaseManager::isServer())
        return installLocalPackage(srcPath, version, stamp, err, progress, kind);

    const qint64 total = si.size();
    QFile in(srcPath);
    if (!in.open(QIODevice::ReadOnly)) {
        if (err) *err = QStringLiteral("无法读取安装包");
        return false;
    }
    qint64 offset = 0;
    for (;;) {
        const QByteArray chunk = in.read(kChunkSize);
        const bool final = in.atEnd();
        QJsonObject p;
        p[QStringLiteral("token")] = RemoteApiClient::instance().token();
        p[QStringLiteral("kind")] = kindTag(kind);
        p[QStringLiteral("name")] = name;
        p[QStringLiteral("version")] = version;
        p[QStringLiteral("build_stamp")] = stamp;
        p[QStringLiteral("size")] = total;
        p[QStringLiteral("offset")] = offset;
        p[QStringLiteral("chunk")] = QString::fromLatin1(chunk.toBase64());
        p[QStringLiteral("final")] = final;
        QString e;
        const QJsonObject res = postJson(QStringLiteral("/api/update/upload"), p, &e, 60000);
        if (res.isEmpty() || !res.value(QStringLiteral("ok")).toBool()) {
            if (err) *err = e.isEmpty()
                ? res.value(QStringLiteral("error")).toString(QStringLiteral("上传失败")) : e;
            return false;
        }
        offset += chunk.size();
        if (progress)
            progress(total > 0 ? static_cast<int>(offset * 100 / total) : 100);
        if (final || chunk.isEmpty())
            break;
    }
    return true;
}

bool UpdateManager::downloadPackage(const QString &destPath, QString *err,
                                    const std::function<void(int, qint64, qint64)> &progress,
                                    PackageKind kind) const
{
    if (DatabaseManager::isServer()) {
        const QString src = localPackagePath(kind);
        if (src.isEmpty()) {
            if (err) *err = QStringLiteral("服务器上还没有安装包");
            return false;
        }
        return copyWithProgress(src, destPath, err, progress);
    }

    QNetworkAccessManager nam;
    QNetworkRequest req(QUrl(serverBaseUrl() + packageApi(kind)));
    QNetworkReply *reply = nam.get(req);
    QFile out(destPath);
    if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        reply->abort();
        reply->deleteLater();
        if (err) *err = QStringLiteral("无法创建下载文件：%1").arg(destPath);
        return false;
    }
    QObject::connect(reply, &QNetworkReply::readyRead, [&out, reply] {
        out.write(reply->readAll());
    });
    QObject::connect(reply, &QNetworkReply::downloadProgress,
                     [progress](qint64 received, qint64 total) {
        if (progress)
            progress(total > 0 ? static_cast<int>(received * 100 / total) : 0, received, total);
    });
    QEventLoop loop;
    QTimer timer;
    timer.setSingleShot(true);
    QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    timer.start(600000);   // 大安装包按局域网速度给 10 分钟
    loop.exec();
    timer.stop();
    if (!reply->isFinished()) {
        reply->abort();
        reply->deleteLater();
        out.close();
        QFile::remove(destPath);
        if (err) *err = QStringLiteral("下载超时");
        return false;
    }
    out.write(reply->readAll());
    const qint64 written = out.size();
    const QNetworkReply::NetworkError netErr = reply->error();
    const QString netText = reply->errorString();
    reply->deleteLater();
    out.close();
    if (netErr != QNetworkReply::NoError || written == 0) {
        QFile::remove(destPath);
        if (err) *err = QStringLiteral("下载失败：%1").arg(netText);
        return false;
    }
    return true;
}
