#ifndef DATABASEMANAGER_H
#define DATABASEMANAGER_H

#include <QObject>
#include <QSqlDatabase>
#include <QList>
#include <QStringList>
#include "model/product.h"
#include "util/stocktypes.h"
#include "util/chiptypes.h"
#include "util/moldtypes.h"
#include "util/cuttypes.h"

// 数据库管理：SQLite 单实例，负责建表与 CRUD
class DatabaseManager : public QObject
{
    Q_OBJECT
public:
    static DatabaseManager &instance();

    bool init();                       // 打开数据库、建表、播种默认数据
    QString storageRoot() const;       // 图纸文件存储根目录
    QString dbDirectory() const;       // 数据库文件所在目录（storageRoot/数据库）

    // 工作路径配置：保存在 QSettings，默认 E:/HouYawei/0000图纸
    static QString defaultStorageRoot();
    static QString readStorageRoot();
    static void writeStorageRoot(const QString &root);

    // 服务器 / 客户端角色：同一份程序靠本机配置区分，不编译两个版本
    static QString systemRole();                 // "server" / "client"
    static void setSystemRole(const QString &role);
    static bool isServer();
    static QString serverHost();                 // 客户端连接的服务器 IP / 主机名
    static void setServerHost(const QString &host);
    static int serverPort();
    static void setServerPort(int port);

    // 数据库文件与备份目录（均位于 storageRoot/数据库 下）
    QString databaseFilePath() const;
    QString backupDirectory() const;
    // 生成一致性快照到 dstPath；还原会用 srcPath 覆盖当前库（还原前自动再备份一次）
    bool backupDatabase(const QString &dstPath, QString *err = nullptr) const;
    bool restoreDatabase(const QString &srcPath, QString *err = nullptr);

    // ---- ERP 库存档案（数据库文件夹下的 .xls + nd_drawings.db 中的库存表）----
    // 客户端模式下全部转由 RemoteApiClient 经 HTTP 完成，本机不落盘。
    QList<StockBatch> stockBatches(QString *err = nullptr) const;
    // 固定 13 列：行号/存货编号/存货全名/存货型号/存货规格/存货产地/
    // 缺省供应商/安全库存数量/建档人/建档时间/修改人/修改时间/封装
    QList<QStringList> stockSearch(const QString &batchKey, const QString &keyword,
                                   int limit, int *total = nullptr,
                                   QString *err = nullptr) const;
    bool importStockFile(const QString &srcPath, const QString &originalName = QString(),
                         QString *err = nullptr);
    bool deleteStockBatch(const QString &key, QString *err = nullptr);

    // ---- 芯片查询（重要芯片台账）----
    // 数据由 res 里的 chip_seed.json 首次启动导入，之后全靠本方法维护，不再导入表格。
    // 客户端模式下全部转由 RemoteApiClient 经 HTTP 完成。
    QStringList chipHardwares(QString *err = nullptr) const;
    QList<ChipItem> chipModelDict(QString *err = nullptr) const;      // 按型号去重的资料字典
    QList<ChipItem> chipSearch(const QString &mode, const QString &keyword,
                               int limit, int *total = nullptr,
                               QString *err = nullptr) const;          // mode: hardware / model
    bool chipSave(ChipItem *item, QString *err = nullptr);             // 新增时写回 id
    bool chipDelete(int id, QString *err = nullptr);

    // ---- 端子机模具台账 ----
    // status: all / incomplete(待完善) / complete / checked；keyword 匹配模具号/端子/线束/线径
    QList<MoldItem> moldSearch(const QString &keyword, const QString &status,
                               int limit, int *total = nullptr, QString *err = nullptr) const;
    bool moldSave(MoldItem *item, QString *err = nullptr);             // 新增时写回 id
    bool moldDelete(int id, QString *err = nullptr);

    // ---- 线束下线：批次/明细/任务/机器 ----
    QList<CutBatch> cutBatches(QString *err = nullptr) const;
    QList<CutItem> cutItems(int batchId, QString *err = nullptr) const;
    // 导入：以批次名（型号）新建批次并写入明细；返回批次 id，失败 -1
    int cutImportBatch(const CutBatch &batch, const QList<CutItem> &items, QString *err = nullptr);
    bool cutDeleteBatch(int batchId, QString *err = nullptr);
    QList<CutMachine> cutMachines(QString *err = nullptr) const;
    bool cutSaveMachine(CutMachine *m, QString *err = nullptr);
    // 派单规则：约束线径/端子/防水栓只能发到指定机台（如 PP2011101 → 一号机）
    QList<CutRule> cutRules(QString *err = nullptr) const;
    bool cutSaveRule(CutRule *r, QString *err = nullptr);   // 新增时写回 id
    bool cutDeleteRule(int id, QString *err = nullptr);
    // 派工：把某批次的明细按 machineId 生成任务（每条明细一条任务）
    bool cutDispatch(int batchId, int machineId, const QList<int> &itemIds,
                     int quantity, QString *err = nullptr);
    // 智能分配：防水栓/端子等规则先锁定机台，其余按线径能力 + 线色归组均衡分配。
    // 返回是否成功；通过 fixedCount/balancedCount 回传分配统计。
    bool cutAutoDispatch(int batchId, int quantity, int *fixedCount, int *balancedCount,
                         QString *err = nullptr);
    // 智能分配预览：只计算建议机台（规则锁定 + 线径线色归组），不生成任务
    bool cutAutoDispatchPreview(int batchId, QList<CutTask> *tasks,
                                int *fixedCount, int *balancedCount, QString *err = nullptr) const;
    // 确认派工：把预览建议（用户可改机台）一次性写成正式任务
    bool cutCommitDispatch(int batchId, const QList<CutAssign> &assigns,
                           int quantity, QString *err = nullptr);
    // 已派工但仍是「待生产」的任务，允许手动改派到另一台机台
    bool cutRetask(int taskId, int machineId, QString *err = nullptr);
    QList<CutTask> cutTasks(int batchId, int machineId, const QString &status,
                            QString *err = nullptr) const;
    // 客户端回传：完成/部分完成一条任务，写回完成数量与操作员
    bool cutFinishTask(int taskId, int doneQty, const QString &operatorName,
                       QString *err = nullptr);

    // 下线机设备端
    bool cutDeviceAuth(int machineNo, const QString &deviceKey,
                       int *machineId, QString *machineName, QString *err = nullptr) const;
    bool cutStartTask(int taskId, int machineId, QString *err = nullptr);
    // step：本次报产条数（默认 1）。设备端点「已完成」传整条剩余数量，一次完成
    bool cutConfirmTask(int taskId, int machineId, const QString &operatorName,
                        int *doneQty, int *quantity, int *status, bool *finished,
                        QString *err = nullptr, int step = 1);
    bool cutLogPrint(int taskId, int machineId, const QString &operatorName,
                     const QString &labelNo, int copies, const QString &result,
                     QString *err = nullptr);

    static QStringList categories();   // 固定 7 大产品分类

    // 字段配置
    QList<FieldConfig> fieldConfigs(const QString &category) const;
    bool addFieldConfig(const QString &category, const QString &key,
                        const QString &label, QString *err = nullptr);
    bool deleteFieldConfig(int id);

    // 产品查询：keyword 模糊匹配 型号/物料编码/名称；drawingType -1 不过滤
    QList<Product> searchProducts(const QString &category, const QString &keyword,
                                  int drawingType = -1) const;
    Product productById(int id) const;
    // 按型号查产品 id（精确匹配、忽略大小写）；未找到返回 -1
    int productIdByModel(const QString &model) const;
    // 补充/更新物料编码（仅改这一列，刷新 updated_at）
    bool updateMaterialCode(int productId, const QString &code, QString *err = nullptr);
    // 设置产品的数量/最小数量/最大数量（库存），刷新 updated_at
    // logOp=false 用于批量导入：库存同步是附带动作，只由 addQtyLog 记一条每日数量日志
    bool updateQuantities(int productId, int qty, int minQty, int maxQty,
                          QString *err = nullptr, bool logOp = true);
    // 追加一条数量录入记录（带日期，供后续日/周/月/年用量统计）
    bool addQtyLog(int productId, int qty, const QDate &date, QString *err = nullptr);
    // 查询某产品的数量日志（按日期倒序）；更新/删除某条数量日志
    QList<QtyLog> qtyLogsOf(int productId, QString *err = nullptr);
    bool updateQtyLog(int logId, int qty, QString *err = nullptr);
    bool deleteQtyLog(int logId, QString *err = nullptr);
    // 查询某产品在指定日期已录入的数量（取该日最新一条），无记录返回 -1
    int qtyLogForDate(int productId, const QDate &date, QString *err = nullptr);
    QList<Drawing> drawingsOf(int productId, int drawingType = -1) const;
    Drawing drawingById(int id) const;
    QList<ChangeLog> changeLogsOf(int productId) const;

    // 同步增量（手机端离线同步）：since 无效时返回全部；drawing.file_path 为绝对路径
    QList<Product> productsChangedSince(const QDateTime &since) const;
    QList<Drawing> drawingsChangedSince(const QDateTime &since) const;
    // 每日结存数量日志（手机端离线算用量与趋势图）：since 无效时返回全部
    QList<QtyLog> qtyLogsChangedSince(const QDateTime &since) const;

    // 按「分类+型号」查产品 id（与 saveProduct 判重口径一致），-1 表示不存在
    int productIdByCategoryModel(const QString &category, const QString &model) const;

    // 保存（分类+型号 已存在则更新，否则新增），返回产品 id，失败返回 -1。
    // keepExistingOnEmpty=true：本次留空的字段保留库中已有值（补充式更新，不会删掉已有信息）
    int saveProduct(Product &p, const QString &changeNote, QString *err = nullptr,
                    bool keepExistingOnEmpty = true);
    bool deleteProducts(const QList<int> &ids, QString *err = nullptr); // 批量删除（含扩展字段/图纸记录/更改记录）

    // 更改记录管理
    bool updateChangeLog(int id, const QString &content, QString *err = nullptr);
    bool deleteChangeLog(int id);
    // 给产品追加一张图纸（拷贝文件入存储目录）
    // srcName 为图纸原始文件名；不传时取 srcFile 自身的文件名。
    // 一个产品的多张子图纸按各自原名归档，避免都叫 "<名称 型号>"。
    bool addDrawing(int productId, const QString &srcFile, int dtype,
                    const QDate &drawingDate, QString *err = nullptr,
                    const QString &srcName = QString());
    // 编辑模式下回写已挂接图纸的日期/类型（不新增记录、不动文件）
    bool updateDrawingMeta(int drawingId, int dtype, const QDate &drawingDate,
                           QString *err = nullptr);
    // 删除单张误录图纸：同时删除数据库记录与归档文件（编辑页逐张清理用）
    bool deleteDrawing(int drawingId, QString *err = nullptr);

    // 用户操作日志（只在服务器主机落库，客户端的写操作由服务器统一记录）
    void setCurrentOperator(const QString &name);
    QString currentOperator() const;      // 空时回退为「本机」
    QString currentOperatorRaw() const;   // 未加工原值，供请求级作用域保存/还原
    bool addOpLog(const QString &action, const QString &model, const QString &name,
                  const QString &detail);
    QList<OpLog> opLogs(const QDateTime &since) const; // 近 N 天的操作，按时间倒序
    bool deleteOpLog(int id);
    bool clearOpLogs();

    // 账号
    bool verifyUser(const QString &name, const QString &password) const;
    QString userRole(const QString &name, const QString &password) const; // 验证通过返回角色，否则空
    QList<QPair<int, QString>> listUsers() const;   // id + 用户名
    bool addUser(const QString &name, const QString &password, QString *err = nullptr);
    bool deleteUser(int id, QString *err = nullptr); // admin 不可删
    bool changeUserPassword(int id, const QString &newPassword, QString *err = nullptr);

private:
    explicit DatabaseManager(QObject *parent = nullptr);
    void createTables();
    void ensureProductColumns(); // 为已存在的库补齐新增列（数量三字段）
    void ensureMoldColumns();    // 为已存在的 mold_item 表补齐新增列
    void migrateLegacyProductTable();  // 旧库 UNIQUE(category,material_code) 迁移为 UNIQUE(category,model)
    void renameDrawingsByNameModel();    // 存量图纸归档目录修正（保留原文件名，幂等）
    void dedupeDrawings();               // 清理内容完全相同的重复图纸记录（保留最早一条）
    void dedupeQtyLogsByDay();           // 清理同日重复的数量记录并加每日唯一约束
    void pruneMissingDrawings();         // 清理文件已不存在的孤儿图纸记录
    void relocateDrawingPaths(const QString &oldRoot); // 整个根目录搬迁后重写 drawing.file_path 前缀
    void syncStoredRoot();               // 把当前工作路径记入数据库（随库一起被复制，供搬迁时比对）
    void seedDefaults();
    void seedChipData();               // 首次启动把 chip_seed.json 灌入 chip_item（幂等）
    void fixChipBrandCountry();        // 一次性订正初始表格把恩智浦（NXP）误记成德国的数据
    void migrateHwSwToExt();             // 历史控制器 product 列的硬件/软件版本迁移进 product_ext
    void loadExtFields(Product &p) const;
    // 智能分配公共计算：规则锁定机台 + 线径线色归组，供预览与正式派工复用
    bool cutComputeAssignments(int batchId, QList<CutAssign> *out,
                               int *fixedCount, int *balancedCount, QString *err) const;

    QSqlDatabase m_db;
    QString m_storageRoot;
    QString m_dbDir;
    QString m_currentOperator; // 当前登录账号，写操作日志时取用
};

#endif // DATABASEMANAGER_H
