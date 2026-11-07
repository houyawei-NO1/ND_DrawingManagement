#include "lanhttpserver.h"
#include "db/databasemanager.h"

#include <QHttpServer>
#include <QHttpServerRequest>
#include <QHttpServerResponse>
#include <QTcpServer>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonDocument>
#include <QMutexLocker>
#include <QFileInfo>
#include <QDir>
#include <QUrl>
#include <QHttpHeaders>
#include <QNetworkInterface>
#include <QNetworkAddressEntry>
#include <QAbstractSocket>
#include <QSysInfo>
#include <QUuid>
#include <QMutexLocker>
#include <QJsonParseError>
#include <QFile>
#include <QIODevice>
#include <QDate>
#include <QMap>

// 同步数据格式版本：手机端据此判断是否需整体重建本地库
static const int kSyncSchemaVersion = 1;

namespace {
// 客户端接口统一返回 {"ok":true/false, ...}，便于客户端按 ok 判断而不依赖 HTTP 状态码
QHttpServerResponse jsonOk(const QJsonObject &extra = QJsonObject())
{
    QJsonObject o = extra;
    o[QStringLiteral("ok")] = true;
    return QHttpServerResponse("application/json; charset=utf-8",
                               QJsonDocument(o).toJson(QJsonDocument::Compact));
}

QHttpServerResponse jsonErr(const QString &msg)
{
    QJsonObject o;
    o[QStringLiteral("ok")] = false;
    o[QStringLiteral("error")] = msg;
    return QHttpServerResponse("application/json; charset=utf-8",
                               QJsonDocument(o).toJson(QJsonDocument::Compact));
}

bool parseBody(const QHttpServerRequest &req, QJsonObject *out)
{
    QJsonParseError pe;
    const QJsonDocument doc = QJsonDocument::fromJson(req.body(), &pe);
    if (pe.error != QJsonParseError::NoError || !doc.isObject())
        return false;
    *out = doc.object();
    return true;
}

QJsonObject productJson(const Product &p)
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
    o[QStringLiteral("qty")] = p.qty;
    o[QStringLiteral("min_qty")] = p.minQty;
    o[QStringLiteral("max_qty")] = p.maxQty;
    QJsonObject ext;
    for (auto it = p.extFields.constBegin(); it != p.extFields.constEnd(); ++it)
        ext[it.key()] = it.value();
    o[QStringLiteral("ext")] = ext;
    return o;
}

Product productFromJson(const QJsonObject &o)
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
    const QJsonObject ext = o.value(QStringLiteral("ext")).toObject();
    for (auto it = ext.constBegin(); it != ext.constEnd(); ++it)
        p.extFields.insert(it.key(), it.value().toString());
    return p;
}

QJsonObject drawingJson(const Drawing &d)
{
    QJsonObject o;
    o[QStringLiteral("id")] = d.id;
    o[QStringLiteral("product_id")] = d.productId;
    o[QStringLiteral("dtype")] = d.dtype;
    o[QStringLiteral("format")] = d.format;
    o[QStringLiteral("file_path")] = d.filePath;
    o[QStringLiteral("drawing_date")] = d.drawingDate.toString(Qt::ISODate);
    o[QStringLiteral("created_at")] = d.createdAt.toString(Qt::ISODate);
    return o;
}

QJsonObject changeLogJson(const ChangeLog &c)
{
    QJsonObject o;
    o[QStringLiteral("id")] = c.id;
    o[QStringLiteral("product_id")] = c.productId;
    o[QStringLiteral("content")] = c.content;
    o[QStringLiteral("oper")] = c.oper;
    o[QStringLiteral("created_at")] = c.createdAt.toString(Qt::ISODate);
    return o;
}

QJsonObject qtyLogJson(const QtyLog &l)
{
    QJsonObject o;
    o[QStringLiteral("id")] = l.id;
    o[QStringLiteral("product_id")] = l.productId;
    o[QStringLiteral("qty")] = l.qty;
    o[QStringLiteral("record_date")] = l.recordDate.isValid()
        ? l.recordDate.toString(Qt::ISODate) : QString();
    o[QStringLiteral("created_at")] = l.createdAt.isValid()
        ? l.createdAt.toString(Qt::ISODate) : QString();
    return o;
}

QJsonObject fieldJson(const FieldConfig &f)
{
    QJsonObject o;
    o[QStringLiteral("id")] = f.id;
    o[QStringLiteral("category")] = f.category;
    o[QStringLiteral("field_key")] = f.fieldKey;
    o[QStringLiteral("field_label")] = f.fieldLabel;
    o[QStringLiteral("sort")] = f.sort;
    o[QStringLiteral("enabled")] = f.enabled;
    return o;
}
} // namespace

LanHttpServer::LanHttpServer(QObject *parent) : QObject(parent)
{
    m_http = new QHttpServer(this);
    m_tcp = new QTcpServer(this);

    // 首页：内嵌只读查询网页
    m_http->route(QStringLiteral("/"), [this] {
        return QHttpServerResponse("text/html; charset=utf-8", buildIndexPage());
    });

    // 网页端 logo / 浏览器图标：直接输出 Qt 资源里的图片（与 PC 端同一份文件）
    m_http->route(QStringLiteral("/logo.png"), [] {
        QFile f(QStringLiteral(":/image/logo.png"));
        if (!f.open(QIODevice::ReadOnly))
            return QHttpServerResponse(QHttpServerResponder::StatusCode::NotFound);
        return QHttpServerResponse("image/png", f.readAll());
    });
    m_http->route(QStringLiteral("/logo.ico"), [] {
        QFile f(QStringLiteral(":/image/logo.ico"));
        if (!f.open(QIODevice::ReadOnly))
            return QHttpServerResponse(QHttpServerResponder::StatusCode::NotFound);
        return QHttpServerResponse("image/x-icon", f.readAll());
    });
    m_http->route(QStringLiteral("/favicon.ico"), [] {
        QFile f(QStringLiteral(":/image/logo.ico"));
        if (!f.open(QIODevice::ReadOnly))
            return QHttpServerResponse(QHttpServerResponder::StatusCode::NotFound);
        return QHttpServerResponse("image/x-icon", f.readAll());
    });

    // 产品列表
    m_http->route(QStringLiteral("/api/products"), [this](const QHttpServerRequest &req) {
        const auto q = req.query();
        return QHttpServerResponse("application/json; charset=utf-8",
                                   apiProducts(q.queryItemValue(QStringLiteral("category")),
                                               q.queryItemValue(QStringLiteral("kw")),
                                               q.queryItemValue(QStringLiteral("dtype")).toInt()));
    });

    // 分类特有字段列定义（网页端据此动态生成表头；「所有类别」即分类为空时返回空）
    m_http->route(QStringLiteral("/api/fields"), [this](const QHttpServerRequest &req) {
        return QHttpServerResponse("application/json; charset=utf-8",
                                   apiFields(req.query().queryItemValue(QStringLiteral("category"))));
    });

    // 产品详情
    m_http->route(QStringLiteral("/api/product"), [this](const QHttpServerRequest &req) {
        const int id = req.query().queryItemValue(QStringLiteral("id")).toInt();
        return QHttpServerResponse("application/json; charset=utf-8", apiProductDetail(id));
    });

    // 网页端库存页：全部产品的库存与预警阈值、单个产品的每日数量记录
    m_http->route(QStringLiteral("/api/stock"), [this] {
        return QHttpServerResponse("application/json; charset=utf-8", apiStock());
    });
    m_http->route(QStringLiteral("/api/qty_logs"), [this](const QHttpServerRequest &req) {
        const int id = req.query().queryItemValue(QStringLiteral("id")).toInt();
        return QHttpServerResponse("application/json; charset=utf-8", apiQtyLogs(id));
    });

    // 图纸文件（图片/PDF 原样输出）
    m_http->route(QStringLiteral("/drawing"), [](const QHttpServerRequest &req) {
        const int id = req.query().queryItemValue(QStringLiteral("id")).toInt();
        const Drawing d = DatabaseManager::instance().drawingById(id);
        if (d.id < 0 || !QFileInfo::exists(d.filePath))
            return QHttpServerResponse(QHttpServerResponder::StatusCode::NotFound);
        return QHttpServerResponse::fromFile(d.filePath);
    });

    // ===== 同步接口（手机端离线同步拉取） =====
    m_http->route(QStringLiteral("/api/sync/schema"), [this] {
        return QHttpServerResponse("application/json; charset=utf-8", apiSyncSchema());
    });
    m_http->route(QStringLiteral("/api/sync/meta"), [this] {
        return QHttpServerResponse("application/json; charset=utf-8", apiSyncMeta());
    });
    m_http->route(QStringLiteral("/api/sync/products"), [this](const QHttpServerRequest &req) {
        const QDateTime since = parseSince(req.query().queryItemValue(QStringLiteral("since")));
        recordActivity(req.remoteAddress().toString(), QStringLiteral("拉取产品列表"));
        appendLog(QStringLiteral("手机(IP=%1) 拉取产品列表（since=%2）")
                  .arg(req.remoteAddress().toString(),
                       since.isValid() ? since.toString(Qt::ISODate) : QStringLiteral("全量")));
        return QHttpServerResponse("application/json; charset=utf-8", apiSyncProducts(since));
    });
    m_http->route(QStringLiteral("/api/sync/drawings"), [this](const QHttpServerRequest &req) {
        const QDateTime since = parseSince(req.query().queryItemValue(QStringLiteral("since")));
        recordActivity(req.remoteAddress().toString(), QStringLiteral("拉取图纸清单"));
        appendLog(QStringLiteral("手机(IP=%1) 拉取图纸清单（since=%2）")
                  .arg(req.remoteAddress().toString(),
                       since.isValid() ? since.toString(Qt::ISODate) : QStringLiteral("全量")));
        return QHttpServerResponse("application/json; charset=utf-8", apiSyncDrawings(since));
    });
    m_http->route(QStringLiteral("/api/sync/categories"), [this] {
        return QHttpServerResponse("application/json; charset=utf-8", apiSyncCategories());
    });
    // 手机端特有字段定义：中文标签 + 显示顺序 + 所属分类，避免把 hw_version 等英文 key 直接显示出来
    m_http->route(QStringLiteral("/api/sync/fields"), [this] {
        return QHttpServerResponse("application/json; charset=utf-8", apiSyncFields());
    });
    // 手机端同步握手：上报设备名，标记设备上线（设置页「离线同步」据此显示）
    m_http->route(QStringLiteral("/api/sync/hello"), [this](const QHttpServerRequest &req) {
        const QString dev = req.query().queryItemValue(QStringLiteral("device"));
        const QString ip = req.remoteAddress().toString();
        recordActivity(ip, QStringLiteral("设备上线"));
        if (!dev.isEmpty())
            recordDevice(dev);
        appendLog(QStringLiteral("手机连接：%1（IP=%2）")
                  .arg(dev.isEmpty() ? QStringLiteral("未知设备") : dev, ip));
        QJsonObject o;
        o["ok"] = true;
        return QHttpServerResponse("application/json; charset=utf-8",
                                   QJsonDocument(o).toJson(QJsonDocument::Compact));
    });
    m_http->route(QStringLiteral("/api/sync/file"), [this](const QHttpServerRequest &req) {
        return syncFile(req.query().queryItemValue(QStringLiteral("path")), req);
    });

    // ===== PC 客户端接口：服务器主机是唯一写入点 =====
    // 登录：账号密码由服务器的 user 表校验，通过后发 token
    m_http->route(QStringLiteral("/api/auth/login"),
                  QHttpServerRequest::Method::Post,
                  [this](const QHttpServerRequest &req) { return handleLogin(req); });
    // 只读查询：网页端 / 手机端 / 客户端共用同一份数据，无需登录
    m_http->route(QStringLiteral("/api/data"),
                  QHttpServerRequest::Method::Post,
                  [this](const QHttpServerRequest &req) { return handleData(req); });
    // 写操作：必须携带登录 token，图纸文件也由服务器统一落盘
    m_http->route(QStringLiteral("/api/write"),
                  QHttpServerRequest::Method::Post,
                  [this](const QHttpServerRequest &req) { return handleWrite(req); });
}

QDateTime LanHttpServer::parseSince(const QString &s)
{
    if (s.isEmpty())
        return QDateTime();
    QDateTime dt = QDateTime::fromString(s, Qt::ISODate);
    if (!dt.isValid())
        dt = QDateTime::fromString(s, Qt::ISODateWithMs);
    return dt;
}

// ==================== PC 客户端接口 ====================
// 账号密码只在服务器主机的 user 表里校验，客户端拿到 token 后才能调用写接口。
// token 仅保存在内存中：服务器程序重启、或超过 12 小时后，客户端需重新登录。
QHttpServerResponse LanHttpServer::handleLogin(const QHttpServerRequest &req)
{
    QJsonObject b;
    if (!parseBody(req, &b))
        return jsonErr(QStringLiteral("请求格式错误"));
    const QString user = b.value(QStringLiteral("user")).toString().trimmed();
    const QString pwd = b.value(QStringLiteral("pwd")).toString();
    const QString role = DatabaseManager::instance().userRole(user, pwd);
    const QString ip = req.remoteAddress().toString();
    if (role.isEmpty()) {
        appendLog(QStringLiteral("客户端登录失败：%1（IP=%2）").arg(user, ip));
        return jsonErr(QStringLiteral("账号或密码错误"));
    }

    const QString token = QUuid::createUuid().toString(QUuid::WithoutBraces);
    {
        QMutexLocker lk(&m_tokenMutex);
        const QDateTime now = QDateTime::currentDateTime();
        for (auto it = m_tokens.begin(); it != m_tokens.end();) {
            if (it.value().second <= now)
                it = m_tokens.erase(it);
            else
                ++it;
        }
        m_tokens.insert(token, qMakePair(user, now.addSecs(12 * 3600)));
    }
    appendLog(QStringLiteral("客户端登录成功：%1（IP=%2）").arg(user, ip));
    recordClient(ip, user, QStringLiteral("登录"));

    QJsonObject extra;
    extra[QStringLiteral("token")] = token;
    extra[QStringLiteral("role")] = role;
    extra[QStringLiteral("user")] = user;
    return jsonOk(extra);
}

bool LanHttpServer::validToken(const QString &token) const
{
    if (token.isEmpty()) return false;
    QMutexLocker lk(&m_tokenMutex);
    const auto it = m_tokens.constFind(token);
    return it != m_tokens.constEnd()
        && it.value().second > QDateTime::currentDateTime();
}

// 只读查询：未登录的客户端、网页端、手机端都从这里取同一份数据
QHttpServerResponse LanHttpServer::handleData(const QHttpServerRequest &req)
{
    QJsonObject b;
    if (!parseBody(req, &b))
        return jsonErr(QStringLiteral("请求格式错误"));
    const QString action = b.value(QStringLiteral("action")).toString();
    recordClient(req.remoteAddress().toString(),
                 accountFromToken(b.value(QStringLiteral("token")).toString()), action);
    DatabaseManager &db = DatabaseManager::instance();
    QJsonObject extra;

    if (action == QLatin1String("ping")) {
        extra[QStringLiteral("server")] = true;
        extra[QStringLiteral("server_time")] = QDateTime::currentDateTime().toString(Qt::ISODate);
        extra[QStringLiteral("storage_root")] = db.storageRoot();
        return jsonOk(extra);
    }
    if (action == QLatin1String("categories")) {
        QJsonArray arr;
        const QStringList cats = DatabaseManager::categories();
        for (const QString &c : cats) arr.append(c);
        extra[QStringLiteral("items")] = arr;
        return jsonOk(extra);
    }
    if (action == QLatin1String("field_configs")) {
        QJsonArray arr;
        const QList<FieldConfig> fields = db.fieldConfigs(b.value(QStringLiteral("category")).toString());
        for (const FieldConfig &f : fields) arr.append(fieldJson(f));
        extra[QStringLiteral("items")] = arr;
        return jsonOk(extra);
    }
    if (action == QLatin1String("search_products")) {
        QJsonArray arr;
        const QList<Product> list = db.searchProducts(
            b.value(QStringLiteral("category")).toString(),
            b.value(QStringLiteral("kw")).toString(),
            b.value(QStringLiteral("dtype")).toInt(-1));
        for (const Product &p : list) arr.append(productJson(p));
        extra[QStringLiteral("items")] = arr;
        return jsonOk(extra);
    }
    if (action == QLatin1String("product_by_id")) {
        extra[QStringLiteral("item")] = productJson(db.productById(b.value(QStringLiteral("id")).toInt(-1)));
        return jsonOk(extra);
    }
    if (action == QLatin1String("product_id_by_model")) {
        extra[QStringLiteral("id")] = db.productIdByModel(b.value(QStringLiteral("model")).toString());
        return jsonOk(extra);
    }
    if (action == QLatin1String("product_id_by_category_model")) {
        extra[QStringLiteral("id")] = db.productIdByCategoryModel(
            b.value(QStringLiteral("category")).toString(),
            b.value(QStringLiteral("model")).toString());
        return jsonOk(extra);
    }
    if (action == QLatin1String("drawings_of")) {
        QJsonArray arr;
        const QList<Drawing> list = db.drawingsOf(
            b.value(QStringLiteral("product_id")).toInt(-1),
            b.value(QStringLiteral("dtype")).toInt(-1));
        for (const Drawing &d : list) arr.append(drawingJson(d));
        extra[QStringLiteral("items")] = arr;
        return jsonOk(extra);
    }
    if (action == QLatin1String("drawing_by_id")) {
        extra[QStringLiteral("item")] = drawingJson(db.drawingById(b.value(QStringLiteral("id")).toInt(-1)));
        return jsonOk(extra);
    }
    if (action == QLatin1String("change_logs_of")) {
        QJsonArray arr;
        const QList<ChangeLog> list = db.changeLogsOf(b.value(QStringLiteral("product_id")).toInt(-1));
        for (const ChangeLog &c : list) arr.append(changeLogJson(c));
        extra[QStringLiteral("items")] = arr;
        return jsonOk(extra);
    }
    if (action == QLatin1String("qty_log_for_date")) {
        extra[QStringLiteral("qty")] = db.qtyLogForDate(
            b.value(QStringLiteral("product_id")).toInt(-1),
            QDate::fromString(b.value(QStringLiteral("date")).toString(), Qt::ISODate));
        return jsonOk(extra);
    }
    if (action == QLatin1String("list_users")) {
        QJsonArray arr;
        const auto users = db.listUsers();
        for (const auto &u : users) {
            QJsonObject o;
            o[QStringLiteral("id")] = u.first;
            o[QStringLiteral("name")] = u.second;
            arr.append(o);
        }
        extra[QStringLiteral("items")] = arr;
        return jsonOk(extra);
    }
    return jsonErr(QStringLiteral("未知的查询操作：%1").arg(action));
}

// 写操作：全部在服务器进程内执行，落到服务器工作路径下的 SQLite 与图纸目录。
// Qt HTTP Server 在主线程串行处理请求，SQLite 连接也在主线程，天然避免并发写冲突。
QHttpServerResponse LanHttpServer::handleWrite(const QHttpServerRequest &req)
{
    QJsonObject b;
    if (!parseBody(req, &b))
        return jsonErr(QStringLiteral("请求格式错误"));
    if (!validToken(b.value(QStringLiteral("token")).toString()))
        return jsonErr(QStringLiteral("登录已失效，请重新登录后再修改数据"));

    const QString action = b.value(QStringLiteral("action")).toString();
    const QString who = accountFromToken(b.value(QStringLiteral("token")).toString());
    recordClient(req.remoteAddress().toString(), who, action);
    DatabaseManager &db = DatabaseManager::instance();
    db.setCurrentOperator(who); // 客户端写操作按登录账号记入用户日志
    QString err;
    appendLog(QStringLiteral("客户端修改：%1（IP=%2）")
                  .arg(action, req.remoteAddress().toString()));

    if (action == QLatin1String("save_product")) {
        Product p = productFromJson(b.value(QStringLiteral("product")).toObject());
        const int id = db.saveProduct(p, b.value(QStringLiteral("change_note")).toString(),
                                      &err, b.value(QStringLiteral("keep_existing_on_empty")).toBool(true));
        if (id < 0)
            return jsonErr(err.isEmpty() ? QStringLiteral("产品保存失败") : err);
        QJsonObject extra;
        extra[QStringLiteral("id")] = id;
        return jsonOk(extra);
    }
    if (action == QLatin1String("delete_products")) {
        QList<int> ids;
        for (const QJsonValue &v : b.value(QStringLiteral("ids")).toArray())
            ids << v.toInt();
        if (!db.deleteProducts(ids, &err))
            return jsonErr(err.isEmpty() ? QStringLiteral("删除失败") : err);
        return jsonOk();
    }
    if (action == QLatin1String("update_material_code")) {
        if (!db.updateMaterialCode(b.value(QStringLiteral("id")).toInt(-1),
                                   b.value(QStringLiteral("code")).toString(), &err))
            return jsonErr(err.isEmpty() ? QStringLiteral("物料编码更新失败") : err);
        return jsonOk();
    }
    if (action == QLatin1String("update_quantities")) {
        if (!db.updateQuantities(b.value(QStringLiteral("id")).toInt(-1),
                                 b.value(QStringLiteral("qty")).toInt(),
                                 b.value(QStringLiteral("min_qty")).toInt(),
                                 b.value(QStringLiteral("max_qty")).toInt(), &err))
            return jsonErr(err.isEmpty() ? QStringLiteral("数量更新失败") : err);
        return jsonOk();
    }
    if (action == QLatin1String("add_qty_log")) {
        if (!db.addQtyLog(b.value(QStringLiteral("id")).toInt(-1),
                          b.value(QStringLiteral("qty")).toInt(),
                          QDate::fromString(b.value(QStringLiteral("date")).toString(), Qt::ISODate),
                          &err))
            return jsonErr(err.isEmpty() ? QStringLiteral("数量记录失败") : err);
        return jsonOk();
    }
    if (action == QLatin1String("list_qty_logs")) {
        QJsonArray arr;
        const QList<QtyLog> list = db.qtyLogsOf(b.value(QStringLiteral("product_id")).toInt(-1));
        for (const QtyLog &l : list) arr.append(qtyLogJson(l));
        QJsonObject extra;
        extra[QStringLiteral("items")] = arr;
        return jsonOk(extra);
    }
    if (action == QLatin1String("update_qty_log")) {
        if (!db.updateQtyLog(b.value(QStringLiteral("id")).toInt(-1),
                             b.value(QStringLiteral("qty")).toInt(), &err))
            return jsonErr(err.isEmpty() ? QStringLiteral("数量日志更新失败") : err);
        return jsonOk();
    }
    if (action == QLatin1String("delete_qty_log")) {
        if (!db.deleteQtyLog(b.value(QStringLiteral("id")).toInt(-1), &err))
            return jsonErr(err.isEmpty() ? QStringLiteral("数量日志删除失败") : err);
        return jsonOk();
    }
    if (action == QLatin1String("add_drawing")) {
        const QByteArray data = QByteArray::fromBase64(
            b.value(QStringLiteral("data")).toString().toLatin1());
        if (data.isEmpty())
            return jsonErr(QStringLiteral("图纸内容为空"));
        const QString upName = b.value(QStringLiteral("filename")).toString();
        QString suffix = QFileInfo(upName).suffix().toLower();
        static const QStringList allowed = {QStringLiteral("jpg"), QStringLiteral("jpeg"),
                                            QStringLiteral("png"), QStringLiteral("bmp"),
                                            QStringLiteral("pdf")};
        if (!allowed.contains(suffix)) suffix = QStringLiteral("jpg");
        // 先写成临时文件，再复用服务端原有的 addDrawing（含去重、命名归档、数据库记录）
        const QString tmp = QDir::tempPath() + QStringLiteral("/nd_upload_%1.%2")
            .arg(QUuid::createUuid().toString(QUuid::WithoutBraces), suffix);
        QFile f(tmp);
        if (!f.open(QIODevice::WriteOnly))
            return jsonErr(QStringLiteral("服务器无法写入临时文件"));
        f.write(data);
        f.close();
        const QDate date = QDate::fromString(b.value(QStringLiteral("drawing_date")).toString(),
                                             Qt::ISODate);
        const bool ok = db.addDrawing(b.value(QStringLiteral("product_id")).toInt(-1), tmp,
                                      b.value(QStringLiteral("dtype")).toInt(0), date, &err,
                                      upName); // 客户端上传：用客户端那边的原文件名归档
        QFile::remove(tmp);
        if (!ok)
            return jsonErr(err.isEmpty() ? QStringLiteral("图纸保存失败") : err);
        return jsonOk();
    }
    if (action == QLatin1String("update_drawing_meta")) {
        const QDate date = QDate::fromString(b.value(QStringLiteral("drawing_date")).toString(),
                                             Qt::ISODate);
        if (!db.updateDrawingMeta(b.value(QStringLiteral("id")).toInt(-1),
                                  b.value(QStringLiteral("dtype")).toInt(0), date, &err))
            return jsonErr(err.isEmpty() ? QStringLiteral("图纸信息更新失败") : err);
        return jsonOk();
    }
    if (action == QLatin1String("update_change_log")) {
        if (!db.updateChangeLog(b.value(QStringLiteral("id")).toInt(-1),
                                b.value(QStringLiteral("content")).toString(), &err))
            return jsonErr(err.isEmpty() ? QStringLiteral("更改记录更新失败") : err);
        return jsonOk();
    }
    if (action == QLatin1String("delete_change_log")) {
        if (!db.deleteChangeLog(b.value(QStringLiteral("id")).toInt(-1)))
            return jsonErr(QStringLiteral("更改记录删除失败"));
        return jsonOk();
    }
    if (action == QLatin1String("add_field_config")) {
        if (!db.addFieldConfig(b.value(QStringLiteral("category")).toString(),
                               b.value(QStringLiteral("key")).toString(),
                               b.value(QStringLiteral("label")).toString(), &err))
            return jsonErr(err.isEmpty() ? QStringLiteral("字段添加失败") : err);
        return jsonOk();
    }
    if (action == QLatin1String("delete_field_config")) {
        if (!db.deleteFieldConfig(b.value(QStringLiteral("id")).toInt(-1)))
            return jsonErr(QStringLiteral("字段删除失败"));
        return jsonOk();
    }
    if (action == QLatin1String("add_user")) {
        if (!db.addUser(b.value(QStringLiteral("name")).toString(),
                        b.value(QStringLiteral("password")).toString(), &err))
            return jsonErr(err.isEmpty() ? QStringLiteral("账号添加失败") : err);
        return jsonOk();
    }
    if (action == QLatin1String("delete_user")) {
        if (!db.deleteUser(b.value(QStringLiteral("id")).toInt(-1), &err))
            return jsonErr(err.isEmpty() ? QStringLiteral("账号删除失败") : err);
        return jsonOk();
    }
    if (action == QLatin1String("change_user_password")) {
        if (!db.changeUserPassword(b.value(QStringLiteral("id")).toInt(-1),
                                   b.value(QStringLiteral("password")).toString(), &err))
            return jsonErr(err.isEmpty() ? QStringLiteral("密码修改失败") : err);
        return jsonOk();
    }
    return jsonErr(QStringLiteral("未知的修改操作：%1").arg(action));
}

QByteArray LanHttpServer::apiSyncSchema() const
{
    QJsonObject o;
    o["schema"] = kSyncSchemaVersion;
    return QJsonDocument(o).toJson(QJsonDocument::Compact);
}

QByteArray LanHttpServer::apiSyncMeta() const
{
    QJsonObject o;
    o["schema"] = kSyncSchemaVersion;
    o["server_time"] = QDateTime::currentDateTime().toString(Qt::ISODate);
    o["storage_root"] = DatabaseManager::instance().storageRoot();
    return QJsonDocument(o).toJson(QJsonDocument::Compact);
}

QByteArray LanHttpServer::apiSyncProducts(const QDateTime &since) const
{
    DatabaseManager &db = DatabaseManager::instance();
    QJsonArray arr;
    for (const Product &p : db.productsChangedSince(since)) {
        QJsonObject o;
        o["id"] = p.id;
        o["category"] = p.category;
        o["material_code"] = p.materialCode;
        o["name"] = p.name;
        o["model"] = p.model;
        o["hw_version"] = p.hwVersion;
        o["sw_version"] = p.swVersion;
        o["remark"] = p.remark;
        o["creator"] = p.creator;
        o["created_at"] = p.createdAt.toString(Qt::ISODate);
        o["updated_at"] = p.updatedAt.toString(Qt::ISODate);
        o["qty"] = p.qty;
        o["min_qty"] = p.minQty;
        o["max_qty"] = p.maxQty;
        QJsonObject ext;
        for (auto it = p.extFields.constBegin(); it != p.extFields.constEnd(); ++it)
            ext[it.key()] = it.value();
        o["ext"] = ext;
        arr.append(o);
    }
    QJsonObject root;
    root["schema"] = kSyncSchemaVersion;
    root["items"] = arr;
    return QJsonDocument(root).toJson(QJsonDocument::Compact);
}

QByteArray LanHttpServer::apiSyncDrawings(const QDateTime &since) const
{
    DatabaseManager &db = DatabaseManager::instance();
    const QString root = QDir::cleanPath(db.storageRoot());
    QJsonArray arr;
    for (const Drawing &d : db.drawingsChangedSince(since)) {
        // 文件已丢失的记录不下发，避免手机端出现“图片未下载或文件缺失”的幽灵条目
        if (!QFileInfo::exists(d.filePath)) continue;
        QJsonObject o;
        o["id"] = d.id;
        o["product_id"] = d.productId;
        o["dtype"] = d.dtype;
        o["format"] = d.format;
        o["drawing_date"] = d.drawingDate.isValid()
                                ? d.drawingDate.toString(Qt::ISODate) : QString();
        o["created_at"] = d.createdAt.toString(Qt::ISODate);
        // 相对 storageRoot 的路径，供 /api/sync/file?path= 拉取，手机端再按自身存储根拼接
        const QString rel = QDir(root).relativeFilePath(QDir::cleanPath(d.filePath));
        o["rel_path"] = rel;
        o["file_name"] = QFileInfo(d.filePath).fileName();
        arr.append(o);
    }
    QJsonObject rootObj;
    rootObj["schema"] = kSyncSchemaVersion;
    rootObj["items"] = arr;
    return QJsonDocument(rootObj).toJson(QJsonDocument::Compact);
}

QByteArray LanHttpServer::apiSyncCategories() const
{
    QJsonArray arr;
    for (const QString &c : DatabaseManager::categories())
        arr.append(c);
    QJsonObject root;
    root["schema"] = kSyncSchemaVersion;
    root["categories"] = arr;
    return QJsonDocument(root).toJson(QJsonDocument::Compact);
}

// 分类特有字段定义：与网页端 /api/fields 同源（field_config）。
// 手机端据此把 product_ext 里的 field_key 显示成中文标签，并按分类、按 PC 设置的顺序渲染；
// 硬件/软件版本号只属于「控制器」，其它分类不会拿到这两项。
QByteArray LanHttpServer::apiSyncFields() const
{
    QJsonArray arr;
    DatabaseManager &db = DatabaseManager::instance();
    for (const QString &cat : DatabaseManager::categories()) {
        for (const FieldConfig &f : db.fieldConfigs(cat)) {
            QJsonObject o;
            o["category"] = cat;
            o["key"] = f.fieldKey;
            o["label"] = f.fieldLabel;
            o["sort"] = f.sort;
            arr.append(o);
        }
    }
    QJsonObject root;
    root["schema"] = kSyncSchemaVersion;
    root["fields"] = arr;
    return QJsonDocument(root).toJson(QJsonDocument::Compact);
}

QHttpServerResponse LanHttpServer::syncFile(const QString &relPath, const QHttpServerRequest &req) const
{
    if (relPath.isEmpty())
        return QHttpServerResponse(QHttpServerResponder::StatusCode::BadRequest);
    // 手机端用 URLEncoder(application/x-www-form-urlencoded) 编码查询参数：
    //   空格 -> '+'，'/' -> %2F，中文 -> %XX。
    // Qt 的 QUrlQuery::queryItemValue 只解码了中文，'+' 与 %2F 仍原样保留，
    // 因此这里先把 '+' 还原为空格，再做百分号解码（%2F -> '/'）。
    // 否则含空格的路径会变成 "TCU控制器+LNM..."，磁盘上找不到文件，返回 404
    // （表现为：手机端建好了空文件夹，但图纸下载全部失败）。
    QString raw = relPath;
    raw.replace(QLatin1Char('+'), QLatin1Char(' '));
    // 先还原 percent 编码（含 %2F 形式的路径分隔符），再规范化
    const QString relRaw = QUrl::fromPercentEncoding(raw.toUtf8());
    // 规范化相对路径，禁止任何形式的目录回退（含 %2F 编码的 ../）
    const QString rel = QDir::cleanPath(relRaw);
    if (rel.startsWith(QLatin1String("..")) || rel.contains(QLatin1String("/..")))
        return QHttpServerResponse(QHttpServerResponder::StatusCode::Forbidden);
    const QString root = QDir::cleanPath(DatabaseManager::instance().storageRoot());
    const QString abs = QDir::cleanPath(QDir(root).absoluteFilePath(rel));
    // 二次校验：最终绝对路径必须仍在 storageRoot 内部
    if (!abs.startsWith(root))
        return QHttpServerResponse(QHttpServerResponder::StatusCode::Forbidden);
    // 数据库目录位于 storageRoot 内，禁止通过同步接口暴露
    const QString dbDir = QDir::cleanPath(DatabaseManager::instance().dbDirectory());
    if (!dbDir.isEmpty() && abs.startsWith(dbDir))
        return QHttpServerResponse(QHttpServerResponder::StatusCode::Forbidden);
    if (!QFileInfo::exists(abs))
        return QHttpServerResponse(QHttpServerResponder::StatusCode::NotFound);

    const QString ip = req.remoteAddress().toString();
    const int idx = req.query().queryItemValue(QStringLiteral("i")).toInt();
    const int total = req.query().queryItemValue(QStringLiteral("total")).toInt();
    const QString fname = QFileInfo(abs).fileName();
    recordActivity(ip, total > 0
        ? QStringLiteral("下载图纸 %1/%2：%3").arg(idx).arg(total).arg(fname)
        : QStringLiteral("下载图纸：%3").arg(fname));
    // 仅在真正下发数据（GET）时记录，避免 HEAD 探测也计一行
    if (req.method() != QHttpServerRequest::Method::Head) {
        appendLog(QStringLiteral("正在给手机传输：%1（%2/%3，IP=%4）")
                  .arg(fname).arg(idx).arg(total).arg(ip));
    }

    // HEAD：仅返回大小，供客户端判断是否需下载（不必每次都重传）
    if (req.method() == QHttpServerRequest::Method::Head) {
        const qint64 size = QFileInfo(abs).size();
        QHttpHeaders hdr;
        hdr.append(QHttpHeaders::WellKnownHeader::ContentLength, QByteArray::number(size));
        const QByteArray mime = abs.endsWith(QLatin1String(".pdf"), Qt::CaseInsensitive)
            ? QByteArrayLiteral("application/pdf") : QByteArrayLiteral("image/*");
        hdr.append(QHttpHeaders::WellKnownHeader::ContentType, mime);
        // 关闭持久连接：避免 QHttpServer 在同一连接上处理后续请求时的卡死
        hdr.append(QHttpHeaders::WellKnownHeader::Connection, QByteArrayLiteral("close"));
        QHttpServerResponse resp(QHttpServerResponder::StatusCode::Ok);
        resp.setHeaders(hdr);
        return resp;
    }
    // GET：同样关闭持久连接，确保每张图都是独立连接
    QHttpServerResponse resp = QHttpServerResponse::fromFile(abs);
    QHttpHeaders hdr = resp.headers();
    hdr.append(QHttpHeaders::WellKnownHeader::Connection, QByteArrayLiteral("close"));
    resp.setHeaders(hdr);
    return resp;
}

bool LanHttpServer::start(quint16 port)
{
    // 监听所有网卡，局域网可访问
    if (!m_tcp->listen(QHostAddress::AnyIPv4, port) || !m_http->bind(m_tcp))
        return false;
    return true;
}

LanHttpServer::SyncActivity LanHttpServer::activity() const
{
    QMutexLocker lock(&m_actMutex);
    return m_activity;
}

void LanHttpServer::recordActivity(const QString &ip, const QString &action) const
{
    QMutexLocker lock(&m_actMutex);
    m_activity.clientIp = ip;
    m_activity.lastSeen = QDateTime::currentDateTime();
    m_activity.action = action;
    m_activity.active = true;
}

void LanHttpServer::recordDevice(const QString &device) const
{
    QMutexLocker lock(&m_actMutex);
    m_activity.deviceName = device;
}

void LanHttpServer::recordClient(const QString &ip, const QString &account, const QString &action) const
{
    // 归一化 IPv4-mapped IPv6 地址（::ffff:192.168.x.x → 192.168.x.x）
    const QString k = ip.startsWith(QStringLiteral("::ffff:")) ? ip.mid(7) : ip;
    QMutexLocker lock(&m_clientMutex);
    ClientConn &c = m_clients[k];
    c.ip = k;
    if (!account.isEmpty())
        c.account = account;
    c.lastSeen = QDateTime::currentDateTime();
    c.lastAction = action;
}

QString LanHttpServer::accountFromToken(const QString &token) const
{
    if (token.isEmpty())
        return QString();
    QMutexLocker lk(&m_tokenMutex);
    auto it = m_tokens.constFind(token);
    return it != m_tokens.constEnd() ? it.value().first : QString();
}

QList<LanHttpServer::ClientConn> LanHttpServer::clients() const
{
    QMutexLocker lock(&m_clientMutex);
    QList<ClientConn> out;
    const QDateTime now = QDateTime::currentDateTime();
    for (auto it = m_clients.begin(); it != m_clients.end(); ++it) {
        if (it.value().lastSeen.isValid() && it.value().lastSeen.secsTo(now) <= 120)
            out.append(it.value());
    }
    return out;
}

void LanHttpServer::appendLog(const QString &line) const
{
    QMutexLocker lock(&m_actMutex);
    const QString ts = QDateTime::currentDateTime().toString(QStringLiteral("hh:mm:ss"));
    m_transferLog.append(QStringLiteral("[%1] %2").arg(ts, line));
    // 限制长度，避免长期运行无限增长
    while (m_transferLog.size() > 500)
        m_transferLog.removeFirst();
}

QString LanHttpServer::transferLog() const
{
    QMutexLocker lock(&m_actMutex);
    return m_transferLog.join(QLatin1Char('\n'));
}

QString LanHttpServer::localHostName() const
{
    return QSysInfo::machineHostName();
}

QList<QPair<QString, QString>> LanHttpServer::localInterfaces() const
{
    QList<QPair<QString, QString>> out;
    for (const QNetworkInterface &iface : QNetworkInterface::allInterfaces()) {
        if (iface.flags() & QNetworkInterface::IsLoopBack)
            continue;
        if (!(iface.flags() & QNetworkInterface::IsUp))
            continue;
        if (iface.addressEntries().isEmpty())
            continue;
        const QString name = iface.humanReadableName();
        for (const QNetworkAddressEntry &e : iface.addressEntries()) {
            const QHostAddress a = e.ip();
            if (a.protocol() == QAbstractSocket::IPv4Protocol)
                out.append({name, a.toString()});
        }
    }
    return out;
}

QByteArray LanHttpServer::apiProducts(const QString &category, const QString &kw, int dtype) const
{
    QJsonArray arr;
    QStringList cats = category.isEmpty() ? DatabaseManager::categories()
                                          : QStringList{category};
    for (const QString &cat : cats) {
        const QList<Product> ps = DatabaseManager::instance().searchProducts(cat, kw, dtype);
        const QList<FieldConfig> extCols = DatabaseManager::instance().fieldConfigs(cat);
        for (const Product &p : ps) {
            QJsonObject o;
            o["id"] = p.id;
            o["category"] = p.category;
            o["material_code"] = p.materialCode;
            o["name"] = p.name;
            o["model"] = p.model;
            o["hw_version"] = p.hwVersion;
            o["sw_version"] = p.swVersion;
            o["remark"] = p.remark;
            o["has_drawing"] = p.hasDrawing;
            // 图纸时间优先，无图纸显示录入日期
            o["drawing_date"] = p.drawingDate.isValid()
                                    ? p.drawingDate.toString(QStringLiteral("yyyy-MM-dd"))
                                    : p.createdAt.toString(QStringLiteral("yyyy-MM-dd"));
            o["created_at"] = p.createdAt.toString(QStringLiteral("yyyy-MM-dd hh:mm"));
            QJsonObject ext;
            for (const FieldConfig &f : extCols)
                ext[f.fieldLabel] = p.extFields.value(f.fieldKey);
            o["ext"] = ext;
            arr.append(o);
        }
    }
    return QJsonDocument(arr).toJson(QJsonDocument::Compact);
}

// 分类特有字段列：「所有类别」（category 为空）不返回任何字段，具体分类返回其已启用字段
QByteArray LanHttpServer::apiFields(const QString &category) const
{
    QJsonArray arr;
    if (!category.isEmpty()) {
        for (const FieldConfig &f : DatabaseManager::instance().fieldConfigs(category)) {
            QJsonObject o;
            o["key"] = f.fieldKey;
            o["label"] = f.fieldLabel;
            arr.append(o);
        }
    }
    QJsonObject root;
    root["fields"] = arr;
    return QJsonDocument(root).toJson(QJsonDocument::Compact);
}

// 库存页列表：按分类遍历全部产品，带出库存数量与预警阈值。
// 最新数量优先取最近的每日数量记录，没有记录时用产品表里的数量。
QByteArray LanHttpServer::apiStock() const
{
    DatabaseManager &db = DatabaseManager::instance();
    QJsonArray arr;
    for (const QString &cat : DatabaseManager::categories()) {
        const QList<Product> ps = db.searchProducts(cat, QString(), -1);
        for (const Product &p : ps) {
            int latest = p.qty;
            const QList<QtyLog> logs = db.qtyLogsOf(p.id, nullptr);
            if (!logs.isEmpty())
                latest = logs.first().qty; // qtyLogsOf 按日期倒序，首条即最新
            QJsonObject o;
            o[QStringLiteral("id")] = p.id;
            o[QStringLiteral("material_code")] = p.materialCode;
            o[QStringLiteral("name")] = p.name;
            o[QStringLiteral("model")] = p.model;
            o[QStringLiteral("category")] = p.category;
            o[QStringLiteral("qty")] = latest;
            o[QStringLiteral("min_qty")] = p.minQty;
            o[QStringLiteral("max_qty")] = p.maxQty;
            arr.append(o);
        }
    }
    return QJsonDocument(arr).toJson(QJsonDocument::Compact);
}

// 每日数量记录：同一天多条只取最新一条，按日期升序返回，供网页端画趋势图与用量统计
QByteArray LanHttpServer::apiQtyLogs(int productId) const
{
    DatabaseManager &db = DatabaseManager::instance();
    const Product p = db.productById(productId);
    QMap<QString, int> byDay;
    const QList<QtyLog> logs = db.qtyLogsOf(productId, nullptr);
    for (const QtyLog &l : logs) {
        const QString day = l.recordDate.toString(Qt::ISODate);
        if (!day.isEmpty() && !byDay.contains(day))
            byDay.insert(day, l.qty);
    }
    QJsonArray arr;
    for (auto it = byDay.constBegin(); it != byDay.constEnd(); ++it) {
        QJsonObject o;
        o[QStringLiteral("date")] = it.key();
        o[QStringLiteral("qty")] = it.value();
        arr.append(o);
    }
    QJsonObject root;
    root[QStringLiteral("id")] = p.id;
    root[QStringLiteral("model")] = p.model;
    root[QStringLiteral("name")] = p.name;
    root[QStringLiteral("qty")] = p.qty;
    root[QStringLiteral("min_qty")] = p.minQty;
    root[QStringLiteral("max_qty")] = p.maxQty;
    root[QStringLiteral("logs")] = arr;
    return QJsonDocument(root).toJson(QJsonDocument::Compact);
}

QByteArray LanHttpServer::apiProductDetail(int id) const
{
    DatabaseManager &db = DatabaseManager::instance();
    const Product p = db.productById(id);
    QJsonObject o;
    o["id"] = p.id;
    o["category"] = p.category;
    o["material_code"] = p.materialCode;
    o["name"] = p.name;
    o["model"] = p.model;
    o["hw_version"] = p.hwVersion;
    o["sw_version"] = p.swVersion;
    o["remark"] = p.remark;
    o["creator"] = p.creator;
    o["created_at"] = p.createdAt.toString(QStringLiteral("yyyy-MM-dd hh:mm"));
    // 特有字段（按该分类配置的字段标签；硬件/软件版本号已归入 ext 机制）
    const QList<FieldConfig> extCols = db.fieldConfigs(p.category);
    QJsonObject ext;
    for (const FieldConfig &f : extCols)
        ext[f.fieldLabel] = p.extFields.value(f.fieldKey);
    o["ext"] = ext;

    QJsonArray drawings;
    for (const Drawing &d : db.drawingsOf(id)) {
        QJsonObject dj;
        dj["id"] = d.id;
        dj["type"] = d.dtype == TypeIssued ? QStringLiteral("下发图") : QStringLiteral("会签图");
        dj["format"] = d.format;
        dj["drawing_date"] = d.drawingDate.toString(QStringLiteral("yyyy-MM-dd"));
        dj["url"] = QStringLiteral("/drawing?id=%1").arg(d.id);
        drawings.append(dj);
    }
    o["drawings"] = drawings;

    QJsonArray logs;
    for (const ChangeLog &c : db.changeLogsOf(id)) {
        QJsonObject cj;
        cj["content"] = c.content;
        cj["oper"] = c.oper;
        cj["time"] = c.createdAt.toString(QStringLiteral("yyyy-MM-dd"));
        logs.append(cj);
    }
    o["change_logs"] = logs;
    return QJsonDocument(o).toJson(QJsonDocument::Compact);
}

QByteArray LanHttpServer::buildIndexPage() const
{
    // 内嵌只读查询页：分类 + 关键词搜索 + 列表 + 详情/图纸
    // 整页 HTML 拆成两段运行时拼接：MSVC 对单个字符串字面量有长度上限(C2026)
    QByteArray page;
    page += R"HTML(<!DOCTYPE html>
<html lang="zh">
<head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>南都图纸查询</title><link rel="icon" type="image/png" href="/logo.png">
<style>
html,body{height:100%}
/* 页面固定一个显示窗口：header 固定，section 占满剩余高度，左右栏各自内部滚动 */
body{margin:0;background:#2B3046;color:#fff;font-family:"Microsoft YaHei",sans-serif;font-size:14px;display:flex;flex-direction:column;overflow:hidden}
header.hd{display:flex;align-items:center;gap:14px;background:#22293C;padding:10px 18px;border-bottom:1px solid rgba(255,255,255,.08);flex:none}
section{flex:1;min-height:0;display:flex;flex-direction:column}
::-webkit-scrollbar{width:8px;height:8px}
::-webkit-scrollbar-track{background:transparent}
::-webkit-scrollbar-thumb{background:rgba(255,255,255,.2);border-radius:6px}
::-webkit-scrollbar-thumb:hover{background:rgba(255,255,255,.34)}
.logo{width:46px;height:46px;flex:none;object-fit:contain;background:transparent}
.brand{font-size:21px;font-weight:bold;white-space:nowrap}
button.btn-ghost{background:rgba(255,255,255,.12);color:#fff;font-weight:normal}
button.btn-ghost:hover{background:rgba(255,255,255,.2)}
#stockKw{min-width:260px}
.nav{display:flex;gap:6px;margin-left:24px}
.nav div{padding:9px 22px;color:rgba(255,255,255,.6);cursor:pointer;border-bottom:2px solid transparent;font-size:15px}
.nav div.on{color:#fff;border-bottom-color:#1673db;background:rgba(22,115,219,.14)}
.nav div:hover{color:#fff}
.hd-tip{margin-left:auto;color:rgba(255,255,255,.4);font-size:12px}
.bar{display:flex;gap:8px;padding:10px 16px;flex-wrap:wrap;align-items:center;flex:none}
input,select{background:#22293C;border:1px solid rgba(255,255,255,.2);border-radius:8px;color:#fff;padding:7px 10px}
button{background:#43CF7C;border:none;border-radius:8px;color:#0B1F14;padding:8px 18px;font-weight:bold;cursor:pointer}
.tabs{display:flex;gap:4px;padding:0 16px;flex-wrap:wrap;flex:none}
.tabs div{padding:7px 14px;color:rgba(255,255,255,.6);cursor:pointer;border-bottom:2px solid transparent}
.tabs div.on{color:#fff;border-bottom-color:#43CF7C}
main{display:grid;grid-template-columns:1fr 1fr;gap:10px;padding:10px 16px;flex:1;min-height:0;overflow:hidden}
main > div{min-height:0;overflow:auto}
main > div:first-child th{position:sticky;top:0;background:#2B3046;z-index:1}
table{width:100%;border-collapse:collapse;font-size:13px}
th,td{padding:6px 8px;border-bottom:1px solid rgba(255,255,255,.08);text-align:left;white-space:nowrap}
th{color:rgba(255,255,255,.5);font-weight:normal}
tr.sel td{background:rgba(67,207,124,.16)}
tbody tr{cursor:pointer}
#detail{background:#22293C;border-radius:8px;padding:12px;min-height:0}
#detail img{max-width:100%;border-radius:4px;background:#fff}
#detail iframe{width:100%;height:420px;border:none;background:#fff;border-radius:4px}
.muted{color:rgba(255,255,255,.5)}
#detail img{cursor:zoom-in}
.ovl{position:fixed;inset:0;background:rgba(0,0,0,.85);display:none;align-items:center;justify-content:center;z-index:99;cursor:zoom-out}
.ovl img{max-width:96%;max-height:94%;border-radius:4px;background:#fff}
section[hidden]{display:none}
.stock-wrap{display:grid;grid-template-columns:minmax(360px,42%) 1fr;gap:12px;padding:12px 16px;align-items:stretch;flex:1;min-height:0}
.panel{background:#22293C;border:1px solid rgba(255,255,255,.07);border-radius:12px;overflow:hidden;box-shadow:0 8px 24px rgba(0,0,0,.18)}
.panel-head{padding:12px 14px;font-weight:bold;border-bottom:1px solid rgba(255,255,255,.08);display:flex;justify-content:space-between;align-items:center}
.stock-left{display:flex;flex-direction:column;min-height:0}
.panel-body{flex:1;min-height:0;overflow:auto}
#stockTbl th{position:sticky;top:0;background:#22293C;z-index:1}
#stockTbl tbody tr{cursor:pointer}
#stockTbl tbody tr td:first-child{border-left:3px solid transparent}
#stockTbl tr.row-none td{background:rgba(255,255,255,.045);color:#fff}
#stockTbl tr.row-ok td{background:rgba(67,207,124,.14);color:#8ef0bb}
#stockTbl tr.row-ok td:first-child{border-left-color:#43CF7C}
#stockTbl tr.row-low td{background:rgba(224,108,117,.16);color:#ff9aa3}
#stockTbl tr.row-low td:first-child{border-left-color:#E06C75}
#stockTbl tr.row-over td{background:rgba(229,192,123,.16);color:#f5d18a}
#stockTbl tr.row-over td:first-child{border-left-color:#E5C07B}
#stockTbl tr.sel td{outline:1px solid rgba(255,255,255,.3);outline-offset:-1px}
.qty-num{font-size:16px;font-weight:bold}
.stock-right{padding:14px;min-height:0;overflow:auto}
.empty{color:rgba(255,255,255,.45);padding:70px 10px;text-align:center;line-height:1.8}
.stock-title{font-size:18px;font-weight:bold}
.stock-sub{color:rgba(255,255,255,.5);font-size:12px;margin-top:5px}
.badge{display:inline-block;padding:3px 11px;border-radius:20px;font-size:12px;margin-left:10px;vertical-align:middle}
.kpis{display:grid;grid-template-columns:repeat(5,1fr);gap:10px;margin:16px 0}
.kpi{background:linear-gradient(160deg,rgba(255,255,255,.075),rgba(255,255,255,.02));border:1px solid rgba(255,255,255,.07);border-radius:10px;padding:12px 10px}
.kpi .lab{color:rgba(255,255,255,.55);font-size:12px}
.kpi .val{font-size:25px;font-weight:bold;margin-top:6px;letter-spacing:.5px}
.kpi .sub{color:rgba(255,255,255,.35);font-size:11px;margin-top:3px}
.kpi.g .val{color:#43CF7C}.kpi.b .val{color:#5aa9ff}.kpi.o .val{color:#E5C07B}.kpi.p .val{color:#c79bff}
.chart{background:rgba(0,0,0,.14);border:1px solid rgba(255,255,255,.06);border-radius:10px;padding:12px;margin-top:12px}
.chart h3{margin:0 0 8px;font-size:14px;font-weight:bold;color:rgba(255,255,255,.85)}
.chart svg{width:100%;height:auto;display:block}
.chart .none{color:rgba(255,255,255,.4);text-align:center;padding:40px 0}
.legend{display:flex;gap:16px;flex-wrap:wrap;color:rgba(255,255,255,.45);font-size:11px;margin-top:8px}
@media(max-width:900px){.stock-wrap{grid-template-columns:1fr;grid-template-rows:minmax(0,1fr) minmax(0,1fr)}main{grid-template-columns:1fr;grid-template-rows:minmax(0,1fr) minmax(0,1fr)}.kpis{grid-template-columns:repeat(2,1fr)}}
</style></head>
<body>
<div class="ovl" id="ovl" onclick="this.style.display='none'"><img src="" alt=""></div>
<header class="hd">
  <img class="logo" src="/logo.png" alt="南都">
  <div class="brand">南都图纸查询</div>
  <nav class="nav">
    <div id="navQuery" class="on" onclick="go('query')">查询</div>
    <div id="navStock" onclick="go('stock')">库存</div>
  </nav>
  <span class="hd-tip">只读</span>
</header>
<section id="pageQuery">
<div class="bar">
  <input id="kw" placeholder="型号 / 物料编码 / 名称…" size="30">
  <button onclick="loadList()">搜 索</button>
  <select id="dtype"><option value="-1">图纸：全部</option><option value="0">下发图</option><option value="1">会签图</option></select>
</div>
<div class="tabs" id="tabs"></div>
<main>
  <div><table id="tbl"></table></div>
  <div id="detail"><span class="muted">点击左侧行查看详情与图纸</span></div>
</main>
</section>
<section id="pageStock" hidden>
  <div class="bar">
    <input id="stockKw" placeholder="物料编码 / 名称 / 型号 / 分类…" size="34">
    <button onclick="applyStockFilter()">搜 索</button>
    <button class="btn-ghost" onclick="clearStockFilter()">重 置</button>
    <span class="muted" id="stockHint">输入关键词可快速定位产品并查看数量统计</span>
  </div>
  <div class="stock-wrap">
    <div class="panel stock-left">
      <div class="panel-head"><span>库存清单</span><span class="muted" id="stockCount"></span></div>
      <div class="panel-body"><table id="stockTbl"></table></div>
    </div>
    <div class="panel stock-right" id="stockRight">
      <div class="empty">点击左侧任意产品<br>查看库存趋势与用量统计</div>
    </div>
  </div>
</section>
<script>
// 与 PC / 手机端一致：搜索行在上、分类行在下；分类首位「所有类别」= 不限分类；
// 搜索范围由当前页签决定，「所有类别」下不显示各分类的特有字段。
const FALLBACK=["控制器","电器盒","开关","传感器","仪表","线束","方向盘"];
let cats=[],curCat="",cols=[];
const tabs=document.getElementById("tabs");
function buildTabs(){
 tabs.innerHTML="";
 const labels=["所有类别"].concat(cats);
 labels.forEach((lb,idx)=>{
  const val=idx===0?"":cats[idx-1];
  const d=document.createElement("div");
  d.textContent=lb;
  if(val===curCat)d.className="on";
  d.onclick=()=>{curCat=val;loadList();buildTabs();};
  tabs.appendChild(d);
 });
}
async function loadList(){
 const kw=document.getElementById("kw").value,dt=document.getElementById("dtype").value;
 const r=await fetch(`/api/products?category=${encodeURIComponent(curCat)}&kw=${encodeURIComponent(kw)}&dtype=${dt}`);
 const list=await r.json();
 // 「所有类别」不返回任何特有字段；具体分类返回该分类配置的字段
 const f=await(await fetch(`/api/fields?category=${encodeURIComponent(curCat)}`)).json();
 cols=(f.fields||[]).map(x=>x.label);
 const all=!curCat;
 const t=document.getElementById("tbl");
 let h="<tr><th>序号</th><th>物料编码</th><th>名称</th><th>型号</th>";
 if(all)h+="<th>分类</th>";
 cols.forEach(c=>h+=`<th>${c}</th>`);
 h+="<th>图纸时间</th><th>备注</th></tr>";
 list.forEach((p,i)=>{
  h+=`<tr onclick="loadDetail(${p.id},this)"><td>${i+1}</td><td>${p.material_code}</td><td>${p.name}</td><td>${p.model}</td>`;
  if(all)h+=`<td>${p.category||""}</td>`;
  cols.forEach(c=>h+=`<td>${(p.ext||{})[c]||""}</td>`);
  h+=`<td>${p.drawing_date||""}</td><td>${p.remark||""}</td></tr>`;
 });
 t.innerHTML=h;
}
async function loadDetail(id,tr){
 document.querySelectorAll("tr").forEach(x=>x.classList.remove("sel"));tr.classList.add("sel");
 const p=await(await fetch(`/api/product?id=${id}`)).json();
 let h=`<b>${p.name}　${p.model}</b><br><span class="muted">物料编码：${p.material_code}　分类：${p.category||"-"}</span>`;
 const ext=p.ext||{},ek=Object.keys(ext);
 if(ek.length)h+="<br><br><b>特有字段</b><br>"+ek.map(k=>`${k}：${ext[k]||"-"}`).join("<br>");
 h+=`<br><br><b>备注</b><br>${p.remark||"-"}<br><br><b>更改记录</b><br>`;
 (p.change_logs||[]).forEach(c=>h+=`${c.time}　${c.content} —— ${c.oper}<br>`);
 if(!p.change_logs||!p.change_logs.length)h+="<span class='muted'>暂无</span>";
 h+="<br><b>图纸</b>（点击图片可放大查看）<br>";
 (p.drawings||[]).forEach(d=>{h+=`<div>${d.type} ${d.drawing_date}　`;
  if(d.format==="pdf")h+=`<a style="color:#43CF7C" href="${d.url}" target="_blank">新窗口打开PDF</a><iframe src="${d.url}"></iframe>`;
  else h+=`<img src="${d.url}" onclick="document.getElementById('ovl').querySelector('img').src='${d.url}';document.getElementById('ovl').style.display='flex'">`;
  h+="</div>"});
 document.getElementById("detail").innerHTML=h;
 // 详情面板自身滚动，加载后回到顶部
 document.getElementById("detail").scrollTop=0;}
document.getElementById("kw").addEventListener("keydown",e=>{if(e.key==="Enter")loadList();});

// ==================== 库存页 ====================
let stockRows=[];
function go(pg){
 const q=pg==="query";
 document.getElementById("pageQuery").hidden=!q;
 document.getElementById("pageStock").hidden=q;
 document.getElementById("navQuery").className=q?"on":"";
 document.getElementById("navStock").className=q?"":"on";
 // 每次切到库存页都重新取数：PC 端刚改过安全值时，切一下就能看到最新预警
 if(!q)loadStock();
}
function esc(s){return String(s==null?"":s).replace(/[&<>"]/g,c=>({"&":"&amp;","<":"&lt;",">":"&gt;",'"':"&quot;"}[c]));}
function fmt(d){return d.getFullYear()+"-"+String(d.getMonth()+1).padStart(2,"0")+"-"+String(d.getDate()).padStart(2,"0");}
function dayOffset(n){const d=new Date();d.setDate(d.getDate()-n);return fmt(d);}

// 库存清单顺序：低于下限 → 高于上限 → 安全区间内(绿色) → 有数量但未设预警 → 其余；
// 组内保持接口返回的原始顺序（分类 / 物料编码）。
function stockRank(p){
 const q=Number(p.qty)||0,mn=Number(p.min_qty)||0,mx=Number(p.max_qty)||0;
 const warn=(mn!==0||mx!==0);
 if(warn&&q<mn)return 0;
 if(warn&&q>mx)return 1;
 if(warn)return 2;   // 已设预警且数量在安全区间内（绿色）
 if(q>0)return 3;    // 有数量但未设预警
 return 4;           // 无数量且未设预警
}
)HTML";
    page += R"PAGE2(async function loadStock(){
 let list=[];
 try{list=await(await fetch("/api/stock",{cache:"no-store"})).json();}catch(e){}
 stockRows=Array.isArray(list)?list:[];
 stockRows.sort((a,b)=>stockRank(a)-stockRank(b));
 renderStockTable();
}
function getStockKw(){return (document.getElementById("stockKw").value||"").trim();}
function stockFiltered(){
 const kw=getStockKw().toLowerCase();
 if(!kw)return stockRows.slice();
 return stockRows.filter(p=>[p.material_code,p.name,p.model,p.category]
  .some(v=>String(v==null?"":v).toLowerCase().indexOf(kw)>=0));
}
function renderStockTable(){
 const rows=stockFiltered(),kw=getStockKw();
 const alarm=stockRows.filter(p=>stockRank(p)<=1).length;
 document.getElementById("stockCount").textContent=kw?`匹配 ${rows.length} / 共 ${stockRows.length} 项`:`共 ${stockRows.length} 项 · 报警 ${alarm} 项`;
 document.getElementById("stockHint").textContent=kw?`已筛出 ${rows.length} 项，点击行查看数量统计`:"输入关键词可快速定位产品并查看数量统计";
 let h="<thead><tr><th>物料编码</th><th>名称</th><th>型号</th><th>最新数量</th></tr></thead><tbody>";
 if(!rows.length)
  h+="<tr><td colspan='4' style='text-align:center;padding:36px;color:rgba(255,255,255,.45)'>"+(kw?"没有匹配的产品":"暂无产品数据")+"</td></tr>";
 rows.forEach(p=>{
  let cls="row-none";
  const mn=Number(p.min_qty)||0,mx=Number(p.max_qty)||0,q=Number(p.qty)||0;
  if(mn!==0||mx!==0){
   if(q<mn)cls="row-low";
   else if(q>mx)cls="row-over";
   else cls="row-ok";
  }
  h+=`<tr data-id="${p.id}" class="${cls}" onclick="loadStockDetail(${p.id},this)"><td>${esc(p.material_code)}</td><td>${esc(p.name)}</td><td>${esc(p.model)}</td><td class="qty-num">${p.qty}</td></tr>`;
 });
 document.getElementById("stockTbl").innerHTML=h+"</tbody>";
}
function applyStockFilter(){
 renderStockTable();
 const rows=[...document.querySelectorAll("#stockTbl tbody tr[data-id]")];
 if(rows.length===1)loadStockDetail(Number(rows[0].dataset.id),rows[0]);
}
function clearStockFilter(){
 document.getElementById("stockKw").value="";
 renderStockTable();
}
document.getElementById("stockKw").addEventListener("keydown",e=>{if(e.key==="Enter")applyStockFilter();});

async function loadStockDetail(id,tr){
 document.querySelectorAll("#stockTbl tr").forEach(x=>x.classList.remove("sel"));
 tr.classList.add("sel");
 const p=stockRows.find(x=>x.id===id)||{};
 let d={logs:[]};
 try{d=await(await fetch("/api/qty_logs?id="+id)).json();}catch(e){}
 renderStock(p,d);
 document.getElementById("stockRight").scrollTop=0; // 切换产品时右侧图表回到顶部
}

// 数据库只存「每日实际结存数量」，用量只能由相邻两次结存推导：
// 用量 = 上一条数量 − 当日数量（减少算消耗）；若数量上升，说明当日有入库/补货，
// 记为 inc，用量按 0 计，不产生负用量、也不冲抵其它日期的用量。
// 差值归属到「当日录入日」，今日/周/月/年用量按该日期汇总。
function usageOf(logs){
 const u=[];
 for(let i=1;i<logs.length;i++){
  const diff=(Number(logs[i-1].qty)||0)-(Number(logs[i].qty)||0);
  u.push({date:logs[i].date,val:Math.max(0,diff),inc:Math.max(0,-diff)});
 }
 return u;
}

// 通用折线图：data=[{date,qty}]，opt 指定颜色/渐变 id/月度标签/取值条数
function lineChart(logs,opt){
 opt=opt||{};
 const data=logs.slice(-(opt.limit||60));
 if(!data.length)return "<div class='none'>"+(opt.empty||"暂无每日数量记录")+"</div>";
 const color=opt.color||"#43CF7C",gid=opt.gid||"ga";
 const lab=x=>opt.monthly?x.date.slice(2):x.date.slice(5);
 const W=640,H=220,L=46,R=14,T=16,B=30;
 const vals=data.map(x=>Number(x.qty)||0);
 let mx=Math.max(...vals),mn=Math.min(...vals);
 if(mx===mn){mx=mx+1;mn=Math.max(0,mn-1);}
 const px=i=>L+(data.length===1?0:i*(W-L-R)/(data.length-1));
 const py=v=>T+(mx-v)*(H-T-B)/(mx-mn);
 const pts=data.map((x,i)=>`${px(i).toFixed(1)},${py(Number(x.qty)||0).toFixed(1)}`).join(" ");
 const area=`${L},${H-B} ${pts} ${px(data.length-1).toFixed(1)},${H-B}`;
 let grid="",xlab="";
 for(let k=0;k<=4;k++){
  const gy=T+k*(H-T-B)/4,raw=mx-(mx-mn)*k/4,v=(mx-mn<5)?raw.toFixed(1):Math.round(raw);
  grid+=`<line x1="${L}" y1="${gy}" x2="${W-R}" y2="${gy}" stroke="rgba(255,255,255,.08)"/>`;
  grid+=`<text x="${L-8}" y="${gy+4}" fill="rgba(255,255,255,.45)" font-size="10" text-anchor="end">${v}</text>`;
 }
 const step=Math.max(1,Math.ceil(data.length/6));
 data.forEach((x,i)=>{if(i%step===0||i===data.length-1)
  xlab+=`<text x="${px(i).toFixed(1)}" y="${H-8}" fill="rgba(255,255,255,.4)" font-size="10" text-anchor="middle">${lab(x)}</text>`;});
 const dots=data.map((x,i)=>`<circle cx="${px(i).toFixed(1)}" cy="${py(Number(x.qty)||0).toFixed(1)}" r="3" fill="${color}"><title>${x.date}：${x.qty}</title></circle>`).join("");
 return `<svg viewBox="0 0 ${W} ${H}" xmlns="http://www.w3.org/2000/svg">
 <defs><linearGradient id="${gid}" x1="0" y1="0" x2="0" y2="1"><stop offset="0%" stop-color="${color}" stop-opacity=".4"/><stop offset="100%" stop-color="${color}" stop-opacity="0"/></linearGradient></defs>
 ${grid}<polygon points="${area}" fill="url(#${gid})"/><polyline points="${pts}" fill="none" stroke="${color}" stroke-width="2.5" stroke-linejoin="round" stroke-linecap="round"/>${dots}${xlab}</svg>`;
}

// 每月数量趋势：每月取最后一次录入的结存数量（月末库存）
function monthlyQty(logs){
 const m=new Map();
 logs.forEach(x=>m.set(x.date.slice(0,7),Number(x.qty)||0));
 return [...m.entries()].map(e=>({date:e[0],qty:e[1]}));
}
// 每月用量趋势：按日期所属月份累加每日用量（入库/补货不计入）
function monthlyUsage(usage){
 const m=new Map();
 usage.forEach(x=>{const k=x.date.slice(0,7);m.set(k,(m.get(k)||0)+(x.val||0));});
 return [...m.entries()].map(e=>({date:e[0],qty:e[1]}));
}

function barChart(usage){
 const data=usage.slice(-30);
 if(!data.length)return "<div class='none'>暂无每日用量记录（至少需要两天的数量）</div>";
 const W=640,H=180,L=46,R=14,T=16,B=28;
 const mx=Math.max(1,...data.map(x=>Math.max(x.val||0,x.inc||0)));
 const slot=(W-L-R)/data.length,bw=Math.max(2,slot*.34);
 const bx=i=>L+i*slot+(slot-bw*2-2)/2;
 let grid="",bars="",xlab="";
 for(let k=0;k<=3;k++){
  const gy=T+k*(H-T-B)/3,raw=mx*(1-k/3),v=(mx<5)?raw.toFixed(1):Math.round(raw);
  grid+=`<line x1="${L}" y1="${gy}" x2="${W-R}" y2="${gy}" stroke="rgba(255,255,255,.08)"/>`;
  grid+=`<text x="${L-8}" y="${gy+4}" fill="rgba(255,255,255,.45)" font-size="10" text-anchor="end">${v}</text>`;
 }
 const step=Math.max(1,Math.ceil(data.length/6));
 data.forEach((u,i)=>{
  const h1=(u.val||0)/mx*(H-T-B),h2=(u.inc||0)/mx*(H-T-B);
  if(h1>0)
   bars+=`<rect x="${bx(i).toFixed(1)}" y="${(H-B-h1).toFixed(1)}" width="${bw.toFixed(1)}" height="${h1.toFixed(1)}" rx="2" fill="url(#gb)"><title>${u.date} 用量 ${u.val}</title></rect>`;
  if(h2>0)
   bars+=`<rect x="${(bx(i)+bw+2).toFixed(1)}" y="${(H-B-h2).toFixed(1)}" width="${bw.toFixed(1)}" height="${h2.toFixed(1)}" rx="2" fill="#43CF7C"><title>${u.date} 入库/补货 +${u.inc}</title></rect>`;
  if(i%step===0||i===data.length-1)
   xlab+=`<text x="${(L+i*slot+slot/2).toFixed(1)}" y="${H-8}" fill="rgba(255,255,255,.4)" font-size="10" text-anchor="middle">${u.date.slice(5)}</text>`;
 });
 return `<svg viewBox="0 0 ${W} ${H}" xmlns="http://www.w3.org/2000/svg">
 <defs><linearGradient id="gb" x1="0" y1="0" x2="0" y2="1"><stop offset="0%" stop-color="#5aa9ff"/><stop offset="100%" stop-color="#1673db" stop-opacity=".55"/></linearGradient></defs>
 ${grid}${bars}${xlab}</svg>`;
}

function renderStock(p,d){
 const logs=(Array.isArray(d.logs)?d.logs:[]).slice().sort((a,b)=>a.date<b.date?-1:1);
 const usage=usageOf(logs);
 const today=fmt(new Date()),week=dayOffset(6),month=today.slice(0,7),year=today.slice(0,4);
 const sum=f=>usage.reduce((s,u)=>s+(f(u.date)?u.val:0),0);
 const dayUse=sum(dt=>dt===today),weekUse=sum(dt=>dt>=week);
 const monthUse=sum(dt=>dt.slice(0,7)===month),yearUse=sum(dt=>dt.slice(0,4)===year);
 const lastDay=logs.length?logs[logs.length-1].date:"暂无记录";
 const inTotal=usage.slice(-30).reduce((s,u)=>s+(u.inc||0),0);
 // 详情接口用 productById 取数，安全值始终准确；列表可能是旧缓存，优先用详情值兜底
 const mn=Number(d.min_qty!=null?d.min_qty:p.min_qty)||0;
 const mx=Number(d.max_qty!=null?d.max_qty:p.max_qty)||0;
 const q=Number(p.qty)||0;
 const noWarn=(mn===0&&mx===0);
 let state="未设预警",color="rgba(255,255,255,.65)";
 if(!noWarn){
  if(q<mn){state="低于下限";color="#ff9aa3";}
  else if(q>mx){state="高于上限";color="#f5d18a";}
  else{state="库存正常";color="#8ef0bb";}
 }
 // 状态徽标里直接带出安全区间，低于下限/高于上限时一眼能看出差多少
 const rangeText=noWarn?"":"<span style='font-weight:400;opacity:.85'> · 安全区间 "+mn+"~"+mx+"</span>";
 const kpis=`<div class="kpis">
  <div class="kpi g"><div class="lab">最新数量</div><div class="val">${q}</div><div class="sub">${lastDay} 结存　${noWarn?"未设预警":"安全区间 "+mn+"~"+mx}</div></div>
  <div class="kpi b"><div class="lab">今日用量</div><div class="val">${dayUse}</div><div class="sub">${today} 录入差值</div></div>
  <div class="kpi o"><div class="lab">周用量</div><div class="val">${weekUse}</div><div class="sub">近 7 天累计（${week} 起）</div></div>
  <div class="kpi p"><div class="lab">月用量</div><div class="val">${monthUse}</div><div class="sub">${month} 累计</div></div>
  <div class="kpi"><div class="lab">年用量</div><div class="val">${yearUse}</div><div class="sub">${year} 年累计</div></div>
 </div>`;
 document.getElementById("stockRight").innerHTML=`
  <div class="stock-title">${esc(p.name)} <span class="muted">${esc(p.model)}</span>
   <span class="badge" style="background:${color}26;color:${color}">${state}${rangeText}</span></div>
  <div class="stock-sub">物料编码：${esc(p.material_code)}　分类：${esc(p.category||"-")}　数量记录：${logs.length} 天</div>
  ${kpis}
  <div class="chart"><h3>每日数量趋势</h3>${lineChart(logs)}
   <div class="legend"><span>数据库每天只存实际结存数量</span><span>最近 60 条记录</span><span>鼠标悬停可看当天数值</span></div></div>
  <div class="chart"><h3>每月数量趋势</h3>${lineChart(monthlyQty(logs),{color:"#5aa9ff",gid:"gmq",monthly:true,limit:120,empty:"暂无每月数量记录"})}
   <div class="legend"><span>每月最后一次录入的结存数量（月末库存）</span><span>最多展示 120 个月</span><span>鼠标悬停可看当月数值</span></div></div>
  <div class="chart"><h3>每日用量 / 入库</h3>${barChart(usage)}
   <div class="legend"><span>蓝：用量 = 上一条数量 − 当日数量</span><span>绿：数量增加按入库/补货计，不算用量</span><span>近 30 天入库合计 ${inTotal}</span><span>最近 30 天</span></div></div>
  <div class="chart"><h3>每月用量趋势</h3>${lineChart(monthlyUsage(usage),{color:"#c79bff",gid:"gmu",monthly:true,limit:120,empty:"暂无每月用量记录"})}
   <div class="legend"><span>当月每日用量合计</span><span>入库/补货不计入用量</span><span>最多展示 120 个月</span></div></div>`;
}

(async function init(){
 try{const o=await(await fetch("/api/sync/categories")).json();
  cats=(o.categories&&o.categories.length)?o.categories:FALLBACK;}catch(e){cats=FALLBACK;}
 buildTabs();
 await loadList();
 // 支持直达库存页与指定产品：/?page=stock&id=产品ID
 const qs=location.search;
 if(qs.indexOf("stock")>=0){
  go("stock");
  const m=qs.match(/id=(\d+)/);
  if(m){
   const id=Number(m[1]);
   setTimeout(()=>{
    const tr=document.querySelector(`#stockTbl tbody tr[data-id="${id}"]`);
    if(tr)loadStockDetail(id,tr);
   },900);
  }
 }
})();
</script>
</body></html>)PAGE2";
    return page;
}
