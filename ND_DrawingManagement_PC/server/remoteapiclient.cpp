#include "remoteapiclient.h"
#include "db/databasemanager.h"

#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QUrl>
#include <QEventLoop>
#include <QTimer>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonParseError>
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QStandardPaths>
#include <QDateTime>

RemoteApiClient &RemoteApiClient::instance()
{
    static RemoteApiClient inst;
    return inst;
}

RemoteApiClient::RemoteApiClient(QObject *parent) : QObject(parent)
{
    m_nam = new QNetworkAccessManager(this);
}

void RemoteApiClient::configure(const QString &host, int port)
{
    m_host = host.trimmed();
    m_port = port > 0 ? port : 8080;
    m_token.clear();
    m_reachable = false;
    m_lastError.clear();
}

QString RemoteApiClient::baseUrl() const
{
    QString h = m_host.trimmed();
    if (h.startsWith(QLatin1String("http://"))) h = h.mid(7);
    else if (h.startsWith(QLatin1String("https://"))) h = h.mid(8);
    const int slash = h.indexOf(QLatin1Char('/'));
    if (slash >= 0) h.truncate(slash);
    return QStringLiteral("http://%1:%2").arg(h).arg(m_port);
}

// 设置页改了服务器地址/端口后，不必重启也能立刻连到新的服务器主机
void RemoteApiClient::syncEndpoint() const
{
    const QString h = DatabaseManager::serverHost().trimmed();
    const int p = DatabaseManager::serverPort();
    if (h != m_host || p != m_port)
        m_token.clear(); // 换了服务器，原 token 不再有效，需重新登录
    m_host = h;
    m_port = p > 0 ? p : 8080;
}

// ---- 同步 HTTP 基础方法 ----
QJsonObject RemoteApiClient::post(const QString &path, const QJsonObject &payload,
                                 QString *err, int timeoutMs) const
{
    syncEndpoint();
    auto fail = [this, err](const QString &msg) -> QJsonObject {
        m_lastError = msg;
        if (err) *err = msg;
        return QJsonObject();
    };
    if (m_host.trimmed().isEmpty())
        return fail(QStringLiteral("未配置服务器地址，请到「设置 → 本机角色」填写"));

    QNetworkRequest req(QUrl(baseUrl() + path));
    req.setHeader(QNetworkRequest::ContentTypeHeader,
                  QByteArray("application/json; charset=utf-8"));
    QNetworkReply *reply = m_nam->post(req,
        QJsonDocument(payload).toJson(QJsonDocument::Compact));

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
        m_reachable = false;
        return fail(QStringLiteral("服务器响应超时：%1").arg(baseUrl()));
    }
    const QByteArray data = reply->readAll();
    const QNetworkReply::NetworkError netErr = reply->error();
    const QString netErrText = reply->errorString();
    reply->deleteLater();
    if (netErr != QNetworkReply::NoError) {
        m_reachable = false;
        return fail(QStringLiteral("无法连接服务器 %1（%2）").arg(baseUrl(), netErrText));
    }

    QJsonParseError pe;
    const QJsonDocument doc = QJsonDocument::fromJson(data, &pe);
    if (pe.error != QJsonParseError::NoError || !doc.isObject()) {
        m_reachable = false;
        return fail(QStringLiteral("服务器返回数据格式错误（请确认地址指向服务器主机）"));
    }
    m_reachable = true;
    m_lastError.clear();
    return doc.object();
}

QByteArray RemoteApiClient::get(const QString &path, const QString &query,
                                QString *err, int timeoutMs) const
{
    syncEndpoint();
    if (m_host.trimmed().isEmpty()) {
        m_lastError = QStringLiteral("未配置服务器地址");
        if (err) *err = m_lastError;
        return QByteArray();
    }
    QUrl url(baseUrl() + path);
    url.setQuery(query);
    QNetworkReply *reply = m_nam->get(QNetworkRequest(url));

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
        m_lastError = QStringLiteral("图纸下载超时");
        if (err) *err = m_lastError;
        return QByteArray();
    }
    const QByteArray data = reply->readAll();
    const QNetworkReply::NetworkError netErr = reply->error();
    const QString netErrText = reply->errorString();
    reply->deleteLater();
    if (netErr != QNetworkReply::NoError || data.isEmpty()) {
        m_lastError = QStringLiteral("图纸下载失败：%1").arg(netErrText);
        if (err) *err = m_lastError;
        return QByteArray();
    }
    return data;
}

QJsonObject RemoteApiClient::readAction(const QString &action,
                                        const QJsonObject &args, QString *err) const
{
    QJsonObject payload = args;
    payload[QStringLiteral("action")] = action;
    const QJsonObject res = post(QStringLiteral("/api/data"), payload, err, 8000);
    if (res.isEmpty()) return QJsonObject();
    if (!res.value(QStringLiteral("ok")).toBool()) {
        const QString e = res.value(QStringLiteral("error"))
                              .toString(QStringLiteral("服务器拒绝了查询"));
        m_lastError = e;
        if (err) *err = e;
        return QJsonObject();
    }
    return res;
}

QJsonObject RemoteApiClient::writeAction(const QString &action,
                                         const QJsonObject &args, QString *err,
                                         int timeoutMs) const
{
    if (m_token.isEmpty()) {
        const QString e = QStringLiteral("登录已失效，请重新登录后再修改数据");
        m_lastError = e;
        if (err) *err = e;
        return QJsonObject();
    }
    QJsonObject payload = args;
    payload[QStringLiteral("action")] = action;
    payload[QStringLiteral("token")] = m_token;
    const QJsonObject res = post(QStringLiteral("/api/write"), payload, err, timeoutMs);
    if (res.isEmpty()) return QJsonObject();
    if (!res.value(QStringLiteral("ok")).toBool()) {
        const QString e = res.value(QStringLiteral("error"))
                              .toString(QStringLiteral("服务器拒绝了本次修改"));
        m_lastError = e;
        if (err) *err = e;
        if (e.contains(QStringLiteral("登录"))) m_token.clear();
        return QJsonObject();
    }
    return res;
}

bool RemoteApiClient::ping(QString *err)
{
    // 启动时只做一次快速探测：服务器没开机时最多等 3 秒，避免启动卡顿
    QJsonObject payload;
    payload[QStringLiteral("action")] = QStringLiteral("ping");
    const QJsonObject res = post(QStringLiteral("/api/data"), payload, err, 3000);
    m_reachable = !res.isEmpty() && res.value(QStringLiteral("ok")).toBool()
                  && res.value(QStringLiteral("server")).toBool();
    if (!m_reachable && err && err->isEmpty())
        *err = QStringLiteral("服务器未响应，请确认服务器主机程序已启动、地址与端口正确");
    return m_reachable;
}

QString RemoteApiClient::login(const QString &user, const QString &pwd, QString *err) const
{
    QJsonObject p;
    p[QStringLiteral("user")] = user;
    p[QStringLiteral("pwd")] = pwd;
    const QJsonObject res = post(QStringLiteral("/api/auth/login"), p, err);
    if (res.isEmpty() || !res.value(QStringLiteral("ok")).toBool()) {
        m_token.clear();
        const QString e = res.value(QStringLiteral("error"))
                              .toString(QStringLiteral("无法连接服务器或账号密码错误"));
        m_lastError = e;
        if (err) *err = e;
        return QString();
    }
    m_token = res.value(QStringLiteral("token")).toString();
    return res.value(QStringLiteral("role")).toString();
}

// ---- JSON ↔ 数据模型 ----
QJsonObject RemoteApiClient::productToJson(const Product &p)
{
    QJsonObject o;
    o[QStringLiteral("id")] = p.id;
    o[QStringLiteral("category")] = p.category;
    o[QStringLiteral("material_code")] = p.materialCode;
    o[QStringLiteral("name")] = p.name;
    o[QStringLiteral("model")] = p.model;
    o[QStringLiteral("hw_version")] = p.hwVersion;
    o[QStringLiteral("sw_version")] = p.swVersion;
    o[QStringLiteral("remark")] = p.remark;
    o[QStringLiteral("creator")] = p.creator;
    o[QStringLiteral("created_at")] = p.createdAt.toString(Qt::ISODate);
    o[QStringLiteral("updated_at")] = p.updatedAt.toString(Qt::ISODate);
    o[QStringLiteral("drawing_date")] = p.drawingDate.toString(Qt::ISODate);
    o[QStringLiteral("has_drawing")] = p.hasDrawing;
    QJsonObject ext;
    for (auto it = p.extFields.constBegin(); it != p.extFields.constEnd(); ++it)
        ext[it.key()] = it.value();
    o[QStringLiteral("ext")] = ext;
    return o;
}

Product RemoteApiClient::productFromJson(const QJsonObject &o)
{
    Product p;
    p.id = o.value(QStringLiteral("id")).toInt(-1);
    p.category = o.value(QStringLiteral("category")).toString();
    p.materialCode = o.value(QStringLiteral("material_code")).toString();
    p.name = o.value(QStringLiteral("name")).toString();
    p.model = o.value(QStringLiteral("model")).toString();
    p.hwVersion = o.value(QStringLiteral("hw_version")).toString();
    p.swVersion = o.value(QStringLiteral("sw_version")).toString();
    p.remark = o.value(QStringLiteral("remark")).toString();
    p.creator = o.value(QStringLiteral("creator")).toString();
    p.qty = o.value(QStringLiteral("qty")).toInt();
    p.minQty = o.value(QStringLiteral("min_qty")).toInt();
    p.maxQty = o.value(QStringLiteral("max_qty")).toInt();
    p.createdAt = QDateTime::fromString(o.value(QStringLiteral("created_at")).toString(), Qt::ISODate);
    p.updatedAt = QDateTime::fromString(o.value(QStringLiteral("updated_at")).toString(), Qt::ISODate);
    p.drawingDate = QDate::fromString(o.value(QStringLiteral("drawing_date")).toString(), Qt::ISODate);
    p.hasDrawing = o.value(QStringLiteral("has_drawing")).toBool();
    const QJsonObject ext = o.value(QStringLiteral("ext")).toObject();
    for (auto it = ext.constBegin(); it != ext.constEnd(); ++it)
        p.extFields.insert(it.key(), it.value().toString());
    return p;
}

Drawing RemoteApiClient::drawingFromJson(const QJsonObject &o)
{
    Drawing d;
    d.id = o.value(QStringLiteral("id")).toInt(-1);
    d.productId = o.value(QStringLiteral("product_id")).toInt(-1);
    d.dtype = o.value(QStringLiteral("dtype")).toInt(0);
    d.format = o.value(QStringLiteral("format")).toString();
    d.filePath = o.value(QStringLiteral("file_path")).toString();
    d.drawingDate = QDate::fromString(o.value(QStringLiteral("drawing_date")).toString(), Qt::ISODate);
    d.createdAt = QDateTime::fromString(o.value(QStringLiteral("created_at")).toString(), Qt::ISODate);
    return d;
}

ChangeLog RemoteApiClient::changeLogFromJson(const QJsonObject &o)
{
    ChangeLog c;
    c.id = o.value(QStringLiteral("id")).toInt(-1);
    c.productId = o.value(QStringLiteral("product_id")).toInt(-1);
    c.content = o.value(QStringLiteral("content")).toString();
    c.oper = o.value(QStringLiteral("oper")).toString();
    c.createdAt = QDateTime::fromString(o.value(QStringLiteral("created_at")).toString(), Qt::ISODate);
    return c;
}

QString RemoteApiClient::cacheDirPath()
{
    return QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)
           + QStringLiteral("/drawing_cache");
}

QString RemoteApiClient::cacheDir()
{
    const QString dir = cacheDirPath();
    return QDir().mkpath(dir) ? dir : QString();
}

// 清理本机预览缓存：图纸文件只在服务器主机保存，这里都是临时下载件
int RemoteApiClient::clearDrawingCache(QString *err, qint64 *freedBytes)
{
    if (err) err->clear();
    if (freedBytes) *freedBytes = 0;

    QDir dir(cacheDirPath());
    if (!dir.exists())
        return 0; // 还没产生缓存

    int removed = 0;
    qint64 bytes = 0;
    const QFileInfoList files = dir.entryInfoList(QDir::Files | QDir::Hidden | QDir::System);
    for (const QFileInfo &fi : files) {
        bytes += fi.size();
        if (QFile::remove(fi.absoluteFilePath()))
            ++removed;
    }
    // 残留子目录一并清掉（正常情况下没有）
    const QFileInfoList subDirs = dir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot
                                                    | QDir::Hidden);
    for (const QFileInfo &fi : subDirs)
        QDir(fi.absoluteFilePath()).removeRecursively();

    if (freedBytes) *freedBytes = bytes;
    if (err && removed < files.size())
        *err = QStringLiteral("有 %1 个文件正被占用，关闭外部查看器后再试")
                   .arg(files.size() - removed);
    return removed;
}

// 图纸文件只保存在服务器主机，客户端按需下载到本机缓存供预览/打开
QString RemoteApiClient::ensureDrawingCached(const Drawing &d) const
{
    if (d.id < 0) return QString();
    const QString dir = cacheDir();
    if (dir.isEmpty()) return QString();
    const QString suffix = d.format.isEmpty() ? QStringLiteral("bin") : d.format;
    const QString path = QStringLiteral("%1/d%2_%3.%4")
        .arg(dir).arg(d.id)
        .arg(d.createdAt.isValid() ? d.createdAt.toMSecsSinceEpoch() : 0)
        .arg(suffix);
    if (QFileInfo::exists(path) && QFileInfo(path).size() > 0) return path;

    QString err;
    const QByteArray bytes = get(QStringLiteral("/drawing"),
                                 QStringLiteral("id=%1").arg(d.id), &err, 30000);
    if (bytes.isEmpty()) return QString();
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly)) return QString();
    f.write(bytes);
    f.close();
    return path;
}

// ---- 只读 ----
QList<FieldConfig> RemoteApiClient::fieldConfigs(const QString &category) const
{
    QJsonObject args;
    args[QStringLiteral("category")] = category;
    const QJsonObject res = readAction(QStringLiteral("field_configs"), args, nullptr);
    QList<FieldConfig> list;
    for (const QJsonValue &v : res.value(QStringLiteral("items")).toArray()) {
        const QJsonObject o = v.toObject();
        FieldConfig f;
        f.id = o.value(QStringLiteral("id")).toInt(-1);
        f.category = o.value(QStringLiteral("category")).toString();
        f.fieldKey = o.value(QStringLiteral("field_key")).toString();
        f.fieldLabel = o.value(QStringLiteral("field_label")).toString();
        f.sort = o.value(QStringLiteral("sort")).toInt();
        f.enabled = o.value(QStringLiteral("enabled")).toBool(true);
        list << f;
    }
    return list;
}

QList<Product> RemoteApiClient::searchProducts(const QString &category,
                                               const QString &keyword, int drawingType) const
{
    QJsonObject args;
    args[QStringLiteral("category")] = category;
    args[QStringLiteral("kw")] = keyword;
    args[QStringLiteral("dtype")] = drawingType;
    const QJsonObject res = readAction(QStringLiteral("search_products"), args, nullptr);
    QList<Product> list;
    for (const QJsonValue &v : res.value(QStringLiteral("items")).toArray())
        list << productFromJson(v.toObject());
    return list;
}

Product RemoteApiClient::productById(int id) const
{
    QJsonObject args;
    args[QStringLiteral("id")] = id;
    const QJsonObject res = readAction(QStringLiteral("product_by_id"), args, nullptr);
    if (res.isEmpty()) return Product();
    return productFromJson(res.value(QStringLiteral("item")).toObject());
}

int RemoteApiClient::productIdByModel(const QString &model) const
{
    QJsonObject args;
    args[QStringLiteral("model")] = model;
    const QJsonObject res = readAction(QStringLiteral("product_id_by_model"), args, nullptr);
    return res.isEmpty() ? -1 : res.value(QStringLiteral("id")).toInt(-1);
}

int RemoteApiClient::productIdByCategoryModel(const QString &category,
                                              const QString &model) const
{
    QJsonObject args;
    args[QStringLiteral("category")] = category;
    args[QStringLiteral("model")] = model;
    const QJsonObject res = readAction(QStringLiteral("product_id_by_category_model"), args, nullptr);
    return res.isEmpty() ? -1 : res.value(QStringLiteral("id")).toInt(-1);
}

QList<Drawing> RemoteApiClient::drawingsOf(int productId, int drawingType) const
{
    QJsonObject args;
    args[QStringLiteral("product_id")] = productId;
    args[QStringLiteral("dtype")] = drawingType;
    const QJsonObject res = readAction(QStringLiteral("drawings_of"), args, nullptr);
    QList<Drawing> list;
    for (const QJsonValue &v : res.value(QStringLiteral("items")).toArray()) {
        Drawing d = drawingFromJson(v.toObject());
        d.filePath = ensureDrawingCached(d); // 换成本机缓存路径，预览/双击打开照常可用
        list << d;
    }
    return list;
}

Drawing RemoteApiClient::drawingById(int id) const
{
    QJsonObject args;
    args[QStringLiteral("id")] = id;
    const QJsonObject res = readAction(QStringLiteral("drawing_by_id"), args, nullptr);
    if (res.isEmpty()) return Drawing();
    Drawing d = drawingFromJson(res.value(QStringLiteral("item")).toObject());
    if (d.id >= 0) d.filePath = ensureDrawingCached(d);
    return d;
}

QList<ChangeLog> RemoteApiClient::changeLogsOf(int productId) const
{
    QJsonObject args;
    args[QStringLiteral("product_id")] = productId;
    const QJsonObject res = readAction(QStringLiteral("change_logs_of"), args, nullptr);
    QList<ChangeLog> list;
    for (const QJsonValue &v : res.value(QStringLiteral("items")).toArray())
        list << changeLogFromJson(v.toObject());
    return list;
}

QList<QPair<int, QString>> RemoteApiClient::listUsers() const
{
    const QJsonObject res = readAction(QStringLiteral("list_users"), QJsonObject(), nullptr);
    QList<QPair<int, QString>> list;
    for (const QJsonValue &v : res.value(QStringLiteral("items")).toArray()) {
        const QJsonObject o = v.toObject();
        list.append({o.value(QStringLiteral("id")).toInt(-1),
                     o.value(QStringLiteral("name")).toString()});
    }
    return list;
}

bool RemoteApiClient::verifyUser(const QString &name, const QString &password) const
{
    return !login(name, password).isEmpty();
}

// ---- 写入 ----
int RemoteApiClient::saveProduct(Product &p, const QString &changeNote, QString *err,
                                 bool keepExistingOnEmpty)
{
    QJsonObject args;
    args[QStringLiteral("product")] = productToJson(p);
    args[QStringLiteral("change_note")] = changeNote;
    args[QStringLiteral("keep_existing_on_empty")] = keepExistingOnEmpty;
    const QJsonObject res = writeAction(QStringLiteral("save_product"), args, err);
    if (res.isEmpty()) return -1;
    const int id = res.value(QStringLiteral("id")).toInt(-1);
    p.id = id;
    return id;
}

bool RemoteApiClient::deleteProducts(const QList<int> &ids, QString *err)
{
    QJsonArray arr;
    for (int id : ids) arr.append(id);
    QJsonObject args;
    args[QStringLiteral("ids")] = arr;
    return !writeAction(QStringLiteral("delete_products"), args, err).isEmpty();
}

bool RemoteApiClient::updateMaterialCode(int productId, const QString &code, QString *err)
{
    QJsonObject args;
    args[QStringLiteral("id")] = productId;
    args[QStringLiteral("code")] = code;
    return !writeAction(QStringLiteral("update_material_code"), args, err).isEmpty();
}

bool RemoteApiClient::updateQuantities(int productId, int qty, int minQty, int maxQty, QString *err)
{
    QJsonObject args;
    args[QStringLiteral("id")] = productId;
    args[QStringLiteral("qty")] = qty;
    args[QStringLiteral("min_qty")] = minQty;
    args[QStringLiteral("max_qty")] = maxQty;
    return !writeAction(QStringLiteral("update_quantities"), args, err).isEmpty();
}

bool RemoteApiClient::addQtyLog(int productId, int qty, const QDate &date, QString *err)
{
    QJsonObject args;
    args[QStringLiteral("id")] = productId;
    args[QStringLiteral("qty")] = qty;
    args[QStringLiteral("date")] = date.toString(Qt::ISODate);
    return !writeAction(QStringLiteral("add_qty_log"), args, err).isEmpty();
}

QList<QtyLog> RemoteApiClient::qtyLogsOf(int productId) const
{
    QJsonObject args;
    args[QStringLiteral("product_id")] = productId;
    const QJsonObject res = readAction(QStringLiteral("list_qty_logs"), args, nullptr);
    QList<QtyLog> list;
    for (const QJsonValue &v : res.value(QStringLiteral("items")).toArray()) {
        const QJsonObject o = v.toObject();
        QtyLog l;
        l.id = o.value(QStringLiteral("id")).toInt(-1);
        l.productId = o.value(QStringLiteral("product_id")).toInt(-1);
        l.qty = o.value(QStringLiteral("qty")).toInt();
        l.recordDate = QDate::fromString(o.value(QStringLiteral("record_date")).toString(), Qt::ISODate);
        l.createdAt = QDateTime::fromString(o.value(QStringLiteral("created_at")).toString(), Qt::ISODate);
        list << l;
    }
    return list;
}

bool RemoteApiClient::updateQtyLog(int logId, int qty, QString *err)
{
    QJsonObject args;
    args[QStringLiteral("id")] = logId;
    args[QStringLiteral("qty")] = qty;
    return !writeAction(QStringLiteral("update_qty_log"), args, err).isEmpty();
}

bool RemoteApiClient::deleteQtyLog(int logId, QString *err)
{
    QJsonObject args;
    args[QStringLiteral("id")] = logId;
    return !writeAction(QStringLiteral("delete_qty_log"), args, err).isEmpty();
}

int RemoteApiClient::qtyLogForDate(int productId, const QDate &date) const
{
    QJsonObject args;
    args[QStringLiteral("product_id")] = productId;
    args[QStringLiteral("date")] = date.toString(Qt::ISODate);
    const QJsonObject res = readAction(QStringLiteral("qty_log_for_date"), args, nullptr);
    if (res.isEmpty())
        return -1;
    return res.value(QStringLiteral("qty")).toInt(-1);
}

bool RemoteApiClient::addDrawing(int productId, const QString &srcFile, int dtype,
                                 const QDate &drawingDate, QString *err)
{
    QFile f(srcFile);
    if (!f.open(QIODevice::ReadOnly)) {
        const QString e = QStringLiteral("无法读取图纸文件：%1").arg(srcFile);
        if (err) *err = e;
        return false;
    }
    const QByteArray bytes = f.readAll();
    f.close();
    if (bytes.isEmpty()) {
        if (err) *err = QStringLiteral("图纸文件为空");
        return false;
    }
    QJsonObject args;
    args[QStringLiteral("product_id")] = productId;
    args[QStringLiteral("dtype")] = dtype;
    args[QStringLiteral("drawing_date")] =
        drawingDate.isValid() ? drawingDate.toString(Qt::ISODate) : QString();
    args[QStringLiteral("filename")] = QFileInfo(srcFile).fileName();
    args[QStringLiteral("data")] = QString::fromLatin1(bytes.toBase64());
    return !writeAction(QStringLiteral("add_drawing"), args, err, 60000).isEmpty();
}

bool RemoteApiClient::updateDrawingMeta(int drawingId, int dtype,
                                        const QDate &drawingDate, QString *err)
{
    QJsonObject args;
    args[QStringLiteral("id")] = drawingId;
    args[QStringLiteral("dtype")] = dtype;
    args[QStringLiteral("drawing_date")] =
        drawingDate.isValid() ? drawingDate.toString(Qt::ISODate) : QString();
    return !writeAction(QStringLiteral("update_drawing_meta"), args, err).isEmpty();
}

bool RemoteApiClient::updateChangeLog(int id, const QString &content, QString *err)
{
    QJsonObject args;
    args[QStringLiteral("id")] = id;
    args[QStringLiteral("content")] = content;
    return !writeAction(QStringLiteral("update_change_log"), args, err).isEmpty();
}

bool RemoteApiClient::deleteChangeLog(int id)
{
    QJsonObject args;
    args[QStringLiteral("id")] = id;
    return !writeAction(QStringLiteral("delete_change_log"), args, nullptr).isEmpty();
}

bool RemoteApiClient::addFieldConfig(const QString &category, const QString &key,
                                     const QString &label, QString *err)
{
    QJsonObject args;
    args[QStringLiteral("category")] = category;
    args[QStringLiteral("key")] = key;
    args[QStringLiteral("label")] = label;
    return !writeAction(QStringLiteral("add_field_config"), args, err).isEmpty();
}

bool RemoteApiClient::deleteFieldConfig(int id)
{
    QJsonObject args;
    args[QStringLiteral("id")] = id;
    return !writeAction(QStringLiteral("delete_field_config"), args, nullptr).isEmpty();
}

bool RemoteApiClient::addUser(const QString &name, const QString &password, QString *err)
{
    QJsonObject args;
    args[QStringLiteral("name")] = name;
    args[QStringLiteral("password")] = password;
    return !writeAction(QStringLiteral("add_user"), args, err).isEmpty();
}

bool RemoteApiClient::deleteUser(int id, QString *err)
{
    QJsonObject args;
    args[QStringLiteral("id")] = id;
    return !writeAction(QStringLiteral("delete_user"), args, err).isEmpty();
}

bool RemoteApiClient::changeUserPassword(int id, const QString &newPassword, QString *err)
{
    QJsonObject args;
    args[QStringLiteral("id")] = id;
    args[QStringLiteral("password")] = newPassword;
    return !writeAction(QStringLiteral("change_user_password"), args, err).isEmpty();
}
