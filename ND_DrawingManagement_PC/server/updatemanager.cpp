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

QString archTagOf(PackageArch a)
{
    if (a == PackageArch::X86) return QStringLiteral("x86");
    if (a == PackageArch::X64) return QStringLiteral("x64");
    return QString();
}

PackageArch archFromTagOf(const QString &tag)
{
    const QString s = tag.trimmed().toLower();
    if (s == QLatin1String("x86") || s == QLatin1String("32")
        || s == QLatin1String("win32"))
        return PackageArch::X86;
    if (s == QLatin1String("x64") || s == QLatin1String("64")
        || s == QLatin1String("win64") || s == QLatin1String("amd64"))
        return PackageArch::X64;
    return PackageArch::Unknown;
}

QString archSuffixOf(PackageArch a)
{
    const QString tag = archTagOf(a);
    return tag.isEmpty() ? QString() : QStringLiteral("_%1").arg(tag);
}

// PC / 手机 APK / 下线机 Device 各用一份元数据/临时文件；Device 再按 32/64 位拆开
QString metaName(PackageKind k, PackageArch a)
{
    if (k == PackageKind::Android) return QStringLiteral("update_android.json");
    if (k == PackageKind::Device) {
        const QString tag = archTagOf(a);
        if (!tag.isEmpty())
            return QStringLiteral("update_device_%1.json").arg(tag);
        return QStringLiteral("update_device.json");
    }
    return QStringLiteral("update.json");
}
QString partName(PackageKind k, PackageArch a)
{
    if (k == PackageKind::Android) return QStringLiteral(".uploading_android.part");
    if (k == PackageKind::Device) {
        const QString tag = archTagOf(a);
        if (!tag.isEmpty())
            return QStringLiteral(".uploading_device_%1.part").arg(tag);
        return QStringLiteral(".uploading_device.part");
    }
    return QStringLiteral(".uploading.part");
}
QString extName(PackageKind k)
{
    if (k == PackageKind::Android) return QStringLiteral(".apk");
    return QStringLiteral(".exe");
}
QStringList globNames(PackageKind k, PackageArch a)
{
    if (k == PackageKind::Android) return QStringList() << QStringLiteral("*.apk");
    if (k == PackageKind::Device) {
        const QString tag = archTagOf(a);
        if (!tag.isEmpty()) {
            // 新中文名（下线机设备…-x86.exe）与历史 CutDevice_*_x86.exe 都要能清理，
            // 否则升级后服务器「安装包」目录会同时留着新旧两套 exe。
            return QStringList()
                << QStringLiteral("下线机设备*%1.exe").arg(tag)
                << QStringLiteral("CutDevice_*%1.exe").arg(tag)
                << QStringLiteral("*_%1.exe").arg(tag);
        }
        return QStringList() << QStringLiteral("CutDevice_*.exe");
    }
    // PC 安装包与设备端 exe 扩展名相同，按文件名前缀区分，避免互相删除
    return QStringList() << QStringLiteral("ND_DrawingManagement_PC*.exe");
}
QString globName(PackageKind k, PackageArch a)
{
    const QStringList l = globNames(k, a);
    return l.isEmpty() ? QString() : l.first();
}

// 与 build_device.ps1 的输出名一致：下线机设备<yyyyMMdd-bXX>-x86/x64.exe。
// 兼容历史 CutDevice_…_x86.exe，两种分隔符（- 与 _）都接受。
bool isDevicePackageNameForArch(const QString &name, PackageArch a)
{
    const QString tag = archTagOf(a);
    if (tag.isEmpty()) return false;
    const QString lower = name.toLower();
    return lower.endsWith(QStringLiteral("-%1.exe").arg(tag))
        || lower.endsWith(QStringLiteral("_%1.exe").arg(tag));
}
QString kindTag(PackageKind k)
{
    if (k == PackageKind::Android) return QStringLiteral("android");
    if (k == PackageKind::Device)  return QStringLiteral("device");
    return QStringLiteral("pc");
}
QString infoApi(PackageKind k)
{
    if (k == PackageKind::Android) return QStringLiteral("/api/update/android/info");
    if (k == PackageKind::Device)  return QStringLiteral("/api/update/device/info");
    return QStringLiteral("/api/update/info");
}
QString packageApi(PackageKind k)
{
    if (k == PackageKind::Android) return QStringLiteral("/api/update/android/package");
    if (k == PackageKind::Device)  return QStringLiteral("/api/update/device/package");
    return QStringLiteral("/api/update/package");
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
    info.arch = o.value(QStringLiteral("arch")).toString();
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

QString UpdateManager::archTag(PackageArch arch)
{
    return archTagOf(arch);
}

PackageArch UpdateManager::archFromTag(const QString &tag)
{
    return archFromTagOf(tag);
}

bool UpdateManager::isDevicePackageName(const QString &fileName, PackageArch arch)
{
    return isDevicePackageNameForArch(fileName, arch);
}

QStringList UpdateManager::packageGlobs(PackageKind kind, PackageArch arch)
{
    return globNames(kind, arch);
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

QString UpdateManager::metaFilePath(PackageKind kind, PackageArch arch)
{
    const QString dir = packageDirectory();
    return dir.isEmpty() ? QString() : QDir(dir).filePath(metaName(kind, arch));
}

QString UpdateManager::partFilePath(PackageKind kind, PackageArch arch)
{
    const QString dir = packageDirectory();
    return dir.isEmpty() ? QString() : QDir(dir).filePath(partName(kind, arch));
}

QString UpdateManager::localPackagePath(PackageKind kind, PackageArch arch)
{
    const UpdateInfo info = readLocalMeta(kind, arch);
    if (!info.available || info.fileName.isEmpty())
        return QString();
    return QDir(packageDirectory()).filePath(info.fileName);
}

QString UpdateManager::packageGlob(PackageKind kind, PackageArch arch)
{
    return globName(kind, arch);
}

UpdateInfo UpdateManager::readLocalMeta(PackageKind kind, PackageArch arch)
{
    const QString dir = packageDirectory();
    const QString path = metaFilePath(kind, arch);
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
    if (info.arch.isEmpty())
        info.arch = archTagOf(arch);
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
                                   PackageKind kind, PackageArch arch)
{
    const QString dir = packageDirectory();
    if (dir.isEmpty() || !QDir().mkpath(dir)) {
        if (err) *err = QStringLiteral("无法创建服务器安装包目录");
        return false;
    }
    QJsonObject o;
    o[QStringLiteral("version")] = version;
    o[QStringLiteral("build_stamp")] = stamp;
    if (!archTagOf(arch).isEmpty())
        o[QStringLiteral("arch")] = archTagOf(arch);
    o[QStringLiteral("file")] = fileName;
    o[QStringLiteral("size")] = size;
    o[QStringLiteral("uploaded_at")] =
        QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"));
    QFile f(metaFilePath(kind, arch));
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (err) *err = QStringLiteral("无法写入版本信息文件");
        return false;
    }
    f.write(QJsonDocument(o).toJson(QJsonDocument::Compact));
    f.close();
    return true;
}

QString UpdateManager::packageVersion(const QString &srcPath, PackageKind kind,
                                      PackageArch arch)
{
    Q_UNUSED(arch);
    if (kind == PackageKind::Pc)
        return instance().localVersion();
    // 下线机 下线机设备20261119-b33-x86.exe（旧名 CutDevice_…_x86.exe 同样兼容）：
    // 版本就是文件名里的完整构建号（含位数），不能只取日期，否则设备端会误判为同一版本
    if (kind == PackageKind::Device) {
        const QString fn = QFileInfo(srcPath).fileName();
        QRegularExpression reFull(
            QStringLiteral("(\\d{8}|\\d{4}-\\d{2}-\\d{2})-b\\d+(?:-x86|-x64)?"),
            QRegularExpression::CaseInsensitiveOption);
        const QRegularExpressionMatch mf = reFull.match(fn);
        if (mf.hasMatch())
            return mf.captured(0).toLower();
        // 兼容旧文件名：只有日期时仍按日期，客户端会提示版本不一致
        QRegularExpression re1(QStringLiteral("\\d{4}-\\d{2}-\\d{2}"));
        QRegularExpressionMatch m1 = re1.match(fn);
        if (m1.hasMatch()) return m1.captured(0);
        QRegularExpression re2(QStringLiteral("\\d{8}"));
        QRegularExpressionMatch m2 = re2.match(fn);
        if (m2.hasMatch()) {
            const QString s = m2.captured(0);
            return QStringLiteral("%1-%2-%3").arg(s.left(4), s.mid(4, 2), s.right(2));
        }
        return QFileInfo(srcPath).lastModified().toString(QStringLiteral("yyyy-MM-dd"));
    }
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
                                        PackageKind kind, PackageArch arch)
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
    if (kind == PackageKind::Device && arch != PackageArch::Unknown
        && !isDevicePackageNameForArch(name, arch)) {
        if (err) *err = QStringLiteral("下线机 %1 位安装包文件名须以 -%2.exe 结尾，"
                                       "例如：下线机设备20261119-b33-%2.exe")
                            .arg(archTagOf(arch) == QLatin1String("x64")
                                     ? QStringLiteral("64") : QStringLiteral("32"),
                                 archTagOf(arch));
        return false;
    }
    const QString dir = packageDirectory();
    if (dir.isEmpty() || !QDir().mkpath(dir)) {
        if (err) *err = QStringLiteral("无法创建服务器安装包目录");
        return false;
    }
    const QString part = partFilePath(kind, arch);
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
    // 同一类型 + 同一位数只保留一份；PC 与 Device 不再按 *.exe 互相清理
    const QFileInfoList others = QDir(dir).entryInfoList(globNames(kind, arch), QDir::Files);
    for (const QFileInfo &fi : others) {
        if (fi.absoluteFilePath() != target)
            QFile::remove(fi.absoluteFilePath());
    }
    return writeLocalMeta(name, version, stamp, si.size(), err, kind, arch);
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

UpdateInfo UpdateManager::checkUpdate(QString *err, PackageKind kind, PackageArch arch) const
{
    if (DatabaseManager::isServer())
        return readLocalMeta(kind, arch);
    // PC 端统一走已有的 POST /api/update/info，用 kind + arch 区分；
    // 手机 APP 用 GET /api/update/android/info（服务端另有路由）
    QJsonObject payload;
    if (kind == PackageKind::Android)
        payload[QStringLiteral("kind")] = QStringLiteral("android");
    else if (kind == PackageKind::Device)
        payload[QStringLiteral("kind")] = QStringLiteral("device");
    if (arch != PackageArch::Unknown)
        payload[QStringLiteral("arch")] = archTagOf(arch);
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
                                  PackageKind kind, PackageArch arch) const
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
    if (kind == PackageKind::Device && arch != PackageArch::Unknown
        && !isDevicePackageNameForArch(name, arch)) {
        if (err) *err = QStringLiteral("下线机 %1 位安装包文件名须以 -%2.exe 结尾，"
                                       "例如：下线机设备20261119-b33-%2.exe")
                            .arg(archTagOf(arch) == QLatin1String("x64")
                                     ? QStringLiteral("64") : QStringLiteral("32"),
                                 archTagOf(arch));
        return false;
    }
    const QString version = packageVersion(srcPath, kind, arch);
    const QString stamp = packageStamp(srcPath, kind);
    if (DatabaseManager::isServer())
        return installLocalPackage(srcPath, version, stamp, err, progress, kind, arch);

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
        if (arch != PackageArch::Unknown)
            p[QStringLiteral("arch")] = archTagOf(arch);
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
                                    PackageKind kind, PackageArch arch) const
{
    if (DatabaseManager::isServer()) {
        const QString src = localPackagePath(kind, arch);
        if (src.isEmpty()) {
            if (err) *err = QStringLiteral("服务器上还没有安装包");
            return false;
        }
        return copyWithProgress(src, destPath, err, progress);
    }

    QString api = packageApi(kind);
    if (arch != PackageArch::Unknown)
        api += QStringLiteral("?arch=%1").arg(archTagOf(arch));
    QNetworkAccessManager nam;
    QNetworkRequest req(QUrl(serverBaseUrl() + api));
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
