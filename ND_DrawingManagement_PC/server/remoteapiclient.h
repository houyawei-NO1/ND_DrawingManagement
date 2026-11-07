#ifndef REMOTEAPICLIENT_H
#define REMOTEAPICLIENT_H

#include <QObject>
#include <QJsonObject>
#include <QList>
#include <QPair>
#include <QString>
#include "model/product.h"

class QNetworkAccessManager;
class QNetworkReply;

// 客户端数据访问层：所有读写都通过服务器主机的 HTTP 接口完成，本机不保存图纸和数据库。
// 方法签名与 DatabaseManager 对齐，DatabaseManager 在客户端模式下直接转发，
// 因此录入页 / 管理页 / 主界面 / 设置页的调用代码无需改动。
//
// 接口约定（均由 LanHttpServer 提供）：
//   POST /api/auth/login  → 校验服务器 user 表，返回 token 与角色
//   POST /api/data        → 只读查询（action 分发，无需 token）
//   POST /api/write       → 写操作（action 分发，必须带有效 token）
//   GET  /drawing?id=     → 下载图纸文件（复用网页/手机端已有路由）
class RemoteApiClient : public QObject
{
    Q_OBJECT
public:
    static RemoteApiClient &instance();

    void configure(const QString &host, int port);
    QString baseUrl() const;
    bool reachable() const { return m_reachable; }
    QString lastError() const { return m_lastError; }

    bool ping(QString *err = nullptr);                                  // 启动时探测服务器
    QString login(const QString &user, const QString &pwd, QString *err = nullptr) const;
    void logout() { m_token.clear(); }
    QString token() const { return m_token; }

    // ---- 本机预览缓存（仅客户端）：图纸只保存在服务器，本机缓存可随时清理 ----
    static QString cacheDirPath();
    // 删除本机缓存的全部图纸文件；返回删除的文件数，freedBytes 返回释放的字节数
    static int clearDrawingCache(QString *err = nullptr, qint64 *freedBytes = nullptr);

    // ---- 只读（对应 DatabaseManager 的同名方法）----
    QList<FieldConfig> fieldConfigs(const QString &category) const;
    QList<Product> searchProducts(const QString &category, const QString &keyword,
                                  int drawingType) const;
    Product productById(int id) const;
    int productIdByModel(const QString &model) const;
    int productIdByCategoryModel(const QString &category, const QString &model) const;
    QList<Drawing> drawingsOf(int productId, int drawingType) const;
    Drawing drawingById(int id) const;
    QList<ChangeLog> changeLogsOf(int productId) const;
    QList<QPair<int, QString>> listUsers() const;
    bool verifyUser(const QString &name, const QString &password) const;

    // ---- 写入（对应 DatabaseManager 的同名方法）----
    int saveProduct(Product &p, const QString &changeNote, QString *err,
                    bool keepExistingOnEmpty);
    bool deleteProducts(const QList<int> &ids, QString *err);
    bool updateMaterialCode(int productId, const QString &code, QString *err);
    bool updateQuantities(int productId, int qty, int minQty, int maxQty, QString *err);
    bool addQtyLog(int productId, int qty, const QDate &date, QString *err);
    QList<QtyLog> qtyLogsOf(int productId) const;
    bool updateQtyLog(int logId, int qty, QString *err);
    bool deleteQtyLog(int logId, QString *err);
    int qtyLogForDate(int productId, const QDate &date) const;
    bool addDrawing(int productId, const QString &srcFile, int dtype,
                    const QDate &drawingDate, QString *err);
    bool updateDrawingMeta(int drawingId, int dtype, const QDate &drawingDate, QString *err);
    bool updateChangeLog(int id, const QString &content, QString *err);
    bool deleteChangeLog(int id);
    bool addFieldConfig(const QString &category, const QString &key,
                        const QString &label, QString *err);
    bool deleteFieldConfig(int id);
    bool addUser(const QString &name, const QString &password, QString *err);
    bool deleteUser(int id, QString *err);
    bool changeUserPassword(int id, const QString &newPassword, QString *err);

private:
    explicit RemoteApiClient(QObject *parent = nullptr);

    // 每次请求都从 QSettings 读取最新的服务器地址，设置页改地址后立即生效
    void syncEndpoint() const;

    // 同步 HTTP：局域网内延迟低，用嵌套事件循环保持与现有同步 UI 调用一致
    QJsonObject post(const QString &path, const QJsonObject &payload, QString *err,
                     int timeoutMs = 15000) const;
    QByteArray get(const QString &path, const QString &query, QString *err,
                   int timeoutMs = 30000) const;
    QJsonObject readAction(const QString &action, const QJsonObject &args, QString *err) const;
    QJsonObject writeAction(const QString &action, const QJsonObject &args, QString *err,
                            int timeoutMs = 15000) const;

    // 客户端预览：把服务器图纸下载到本机缓存目录，返回可直接打开的本地路径
    QString ensureDrawingCached(const Drawing &d) const;
    static QString cacheDir();

    static QJsonObject productToJson(const Product &p);
    static Product productFromJson(const QJsonObject &o);
    static Drawing drawingFromJson(const QJsonObject &o);
    static ChangeLog changeLogFromJson(const QJsonObject &o);

    QNetworkAccessManager *m_nam = nullptr;
    mutable QString m_host;
    mutable int m_port = 8080;
    mutable QString m_token;
    mutable bool m_reachable = false;
    mutable QString m_lastError;
};

#endif // REMOTEAPICLIENT_H
