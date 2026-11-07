#ifndef LANHTTPSERVER_H
#define LANHTTPSERVER_H

#include <QObject>
#include <QHttpServerResponse>
#include <QMutex>
#include <QDateTime>
#include <QPair>
#include <QHash>
#include <QList>

class QHttpServer;
class QTcpServer;
class QHttpServerRequest;

// 局域网只读网页服务：基于官方 Qt HTTP Server
// 路由：
//   GET /                      → 内嵌查询网页（HTML）
//   GET /api/products?category=&kw=&dtype=  → JSON 产品列表
//   GET /api/product?id=       → JSON 产品详情（含更改记录/图纸列表）
//   GET /drawing?id=           → 图纸文件（图片/PDF 原样输出，浏览器直接预览）
// 同步接口（手机端离线同步拉取）：
//   GET /api/sync/schema                       → {"schema":版本号}
//   GET /api/sync/meta                         → 服务器时间/存储根（用于校验）
//   GET /api/sync/products?since=ISO时间      → 增量产品（含 ext 字段），since 空=全量
//   GET /api/sync/drawings?since=ISO时间      → 增量图纸记录（含相对存储根的路径）
//   GET /api/sync/categories                  → 产品分类
//   GET /api/sync/fields                      → 各分类特有字段定义（field_key → 中文标签、显示顺序）
//   GET|HEAD /api/sync/file?path=相对路径[&i=N&total=M] → 二进制图纸 / HEAD 仅返回大小
// 客户端（PC 程序）接口：服务器主机是唯一的数据写入点
//   POST /api/auth/login → 校验账号密码，返回 token 与角色（账号只在服务器主机维护）
//   POST /api/data       → 只读查询（action 分发，无需 token）
//   POST /api/write      → 写操作（action 分发，必须带有效 token，服务端统一落盘）
class LanHttpServer : public QObject
{
Q_OBJECT
public:
    explicit LanHttpServer(QObject *parent = nullptr);
    bool start(quint16 port);

    // 最近一次同步活动，供主窗口/设置页显示「哪个设备正在同步」
    struct SyncActivity {
        QString clientIp;
        QDateTime lastSeen;
        QString action;   // 人类可读，如 "下载图纸 12/126"
        bool active = false;
        QString deviceName;   // 手机端上报的设备名
    };
    SyncActivity activity() const;

    // 已连接 PC 客户端信息（客户端通过登录 / 查询 / 写入接口联系服务器，按 IP 记录最近活动）
    struct ClientConn {
        QString ip;
        QString account;     // 登录账号（未登录时为空）
        QDateTime lastSeen;
        QString lastAction;  // 最近一次动作，如 "search_products"
    };
    // 返回近 2 分钟内联系过服务器的 PC 客户端（视为在线）
    QList<ClientConn> clients() const;

    // 手机端在同步握手时上报设备名
    void recordDevice(const QString &device) const;
    // 本机侧同步传输日志（记录手机连接、IP、正在传输的文件等），供设置页「离线同步」展示
    QString transferLog() const;
    // 供设置页「离线同步」展示：本机名称与各网卡 IPv4 地址
    QString localHostName() const;
    QList<QPair<QString, QString>> localInterfaces() const;

private:
    static QDateTime parseSince(const QString &s);

    QByteArray apiSyncSchema() const;
    QByteArray apiSyncMeta() const;
    QByteArray apiSyncProducts(const QDateTime &since) const;
    QByteArray apiSyncDrawings(const QDateTime &since) const;
    QByteArray apiSyncCategories() const;   // 下发产品分类（与 PC DatabaseManager::categories 一致）
    QByteArray apiSyncFields() const;       // 下发各分类特有字段配置（field_key/label/sort）
    QHttpServerResponse syncFile(const QString &relPath, const QHttpServerRequest &req) const;
    void recordActivity(const QString &ip, const QString &action) const;
    void appendLog(const QString &line) const;   // 追加一行本机侧传输日志（带时间戳，限长）
    // 记录 PC 客户端连接（按 IP 去重，更新最近活动与账号）
    void recordClient(const QString &ip, const QString &account, const QString &action) const;
    QString accountFromToken(const QString &token) const;  // token → 账号（无效返回空）

    // 客户端接口：登录 token / 只读查询 / 带鉴权的写操作
    QHttpServerResponse handleLogin(const QHttpServerRequest &req);
    QHttpServerResponse handleData(const QHttpServerRequest &req);
    QHttpServerResponse handleWrite(const QHttpServerRequest &req);
    bool validToken(const QString &token) const;

    QByteArray buildIndexPage() const;
    QByteArray apiProducts(const QString &category, const QString &kw, int dtype) const;
    QByteArray apiProductDetail(int id) const;
    QByteArray apiFields(const QString &category) const; // 分类特有字段列（分类为空则无）
    QByteArray apiStock() const;                        // 网页端库存页：产品 + 最新数量 + 预警上下限
    QByteArray apiQtyLogs(int productId) const;         // 网页端库存页：每日数量记录

    QHttpServer *m_http = nullptr;
    QTcpServer *m_tcp = nullptr;
    mutable QMutex m_actMutex;
    mutable SyncActivity m_activity;
    mutable QStringList m_transferLog;   // 本机侧同步传输日志（按时间追加，限制长度）

    // 客户端登录 token：token → (账号, 过期时间)。仅内存保存，服务器程序重启后需重新登录
    mutable QMutex m_tokenMutex;
    QHash<QString, QPair<QString, QDateTime>> m_tokens;

    // PC 客户端连接记录：IP → 最近活动信息（仅内存）
    mutable QMutex m_clientMutex;
    mutable QHash<QString, ClientConn> m_clients;
};

#endif // LANHTTPSERVER_H
