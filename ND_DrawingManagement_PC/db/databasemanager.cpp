#include "databasemanager.h"
#include "server/remoteapiclient.h"
#include <QSqlQuery>
#include <QSqlError>
#include <QVariant>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDebug>
#include <algorithm>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QSettings>
#include <QDateTime>

// 操作日志明细：一行「字段 / 修改前 / 修改后」，打包成 JSON 供设置页弹窗渲染表格
static QJsonObject opRow(const QString &field, const QString &before, const QString &after)
{
    QJsonObject o;
    o[QStringLiteral("field")] = field;
    o[QStringLiteral("before")] = before;
    o[QStringLiteral("after")] = after;
    return o;
}

static QString opRowsJson(const QJsonArray &rows)
{
    return QString::fromUtf8(QJsonDocument(rows).toJson(QJsonDocument::Compact));
}

DatabaseManager &DatabaseManager::instance()
{
    static DatabaseManager inst;
    return inst;
}

DatabaseManager::DatabaseManager(QObject *parent) : QObject(parent) {}

QStringList DatabaseManager::categories()
{
    return {QStringLiteral("控制器"), QStringLiteral("电器盒"), QStringLiteral("开关"),
            QStringLiteral("传感器"), QStringLiteral("仪表"),   QStringLiteral("线束"),
            QStringLiteral("方向盘")};
}

QString DatabaseManager::defaultStorageRoot()
{
    return QStringLiteral("E:/HouYawei/0000图纸");
}

QString DatabaseManager::readStorageRoot()
{
    QSettings s;
    const QString v = s.value(QStringLiteral("storage/root"), defaultStorageRoot())
                          .toString().trimmed();
    return QDir::cleanPath(v.isEmpty() ? defaultStorageRoot() : v);
}

void DatabaseManager::writeStorageRoot(const QString &root)
{
    QSettings s;
    s.setValue(QStringLiteral("storage/root"), QDir::cleanPath(root));
    s.sync();
}

// ---- 服务器 / 客户端角色（本机 QSettings 配置）----
QString DatabaseManager::systemRole()
{
    const QString r = QSettings().value(QStringLiteral("system/role"),
                                        QStringLiteral("server")).toString();
    return r == QLatin1String("client") ? QStringLiteral("client") : QStringLiteral("server");
}

void DatabaseManager::setSystemRole(const QString &role)
{
    QSettings s;
    s.setValue(QStringLiteral("system/role"),
               role == QLatin1String("client") ? QStringLiteral("client") : QStringLiteral("server"));
    s.sync();
}

bool DatabaseManager::isServer()
{
    return systemRole() == QLatin1String("server");
}

QString DatabaseManager::serverHost()
{
    return QSettings().value(QStringLiteral("server/host"), QString()).toString().trimmed();
}

void DatabaseManager::setServerHost(const QString &host)
{
    QSettings s;
    s.setValue(QStringLiteral("server/host"), host.trimmed());
    s.sync();
}

int DatabaseManager::serverPort()
{
    return QSettings().value(QStringLiteral("server/port"), 8080).toInt();
}

void DatabaseManager::setServerPort(int port)
{
    QSettings s;
    s.setValue(QStringLiteral("server/port"), port > 0 ? port : 8080);
    s.sync();
}

QString DatabaseManager::storageRoot() const { return m_storageRoot; }

QString DatabaseManager::dbDirectory() const { return m_dbDir; }

QString DatabaseManager::databaseFilePath() const
{
    return m_dbDir.isEmpty() ? QString()
        : QDir::cleanPath(m_dbDir + QStringLiteral("/nd_drawings.db"));
}

QString DatabaseManager::backupDirectory() const
{
    return m_dbDir.isEmpty() ? QString()
        : QDir::cleanPath(m_dbDir + QStringLiteral("/备份"));
}

bool DatabaseManager::init()
{
    // 客户端：本机不创建、不打开 SQLite，也不建图纸目录，全部数据通过服务器主机读写
    if (!isServer()) {
        m_storageRoot.clear();
        m_dbDir.clear();
        RemoteApiClient::instance().configure(serverHost(), serverPort());
        QString err;
        RemoteApiClient::instance().ping(&err); // 连不上也允许启动，界面显示空列表，服务器恢复后即可刷新
        if (!err.isEmpty()) qWarning() << "client ping failed:" << err;
        return true;
    }

    // 工作路径可在「设置 → 工作路径」选择，保存在 QSettings；
    // 目录不可写或盘符失效时自动回退默认路径，避免程序启动即失败。
    QString root = readStorageRoot();
    if (!QDir().mkpath(root) || !QFileInfo(root).isWritable())
        root = defaultStorageRoot();
    m_storageRoot = QDir::cleanPath(root);
    QDir().mkpath(m_storageRoot);

    // 数据库统一存放在「图纸根目录/数据库」下
    m_dbDir = QDir::cleanPath(m_storageRoot + QStringLiteral("/数据库"));
    QDir().mkpath(m_dbDir);
    const QString newDb = m_dbDir + QStringLiteral("/nd_drawings.db");

    // 历史兼容：原库在程序目录 data/nd_drawings.db，首次启动自动迁移到新位置（含 WAL/SHM）
    const QString oldDb = QDir::cleanPath(
        QCoreApplication::applicationDirPath() + QStringLiteral("/data/nd_drawings.db"));
    if (!QFile::exists(newDb) && QFile::exists(oldDb)) {
        const auto migrateOne = [&](const QString &src, const QString &dst) {
            if (QFile::exists(src) && QFile::copy(src, dst)) QFile::remove(src);
        };
        migrateOne(oldDb, newDb);
        migrateOne(oldDb + QStringLiteral("-wal"), newDb + QStringLiteral("-wal"));
        migrateOne(oldDb + QStringLiteral("-shm"), newDb + QStringLiteral("-shm"));
        QDir().rmdir(QCoreApplication::applicationDirPath() + QStringLiteral("/data"));
    }

    m_db = QSqlDatabase::addDatabase("QSQLITE");
    m_db.setDatabaseName(newDb);
    if (!m_db.open()) {
        qWarning() << "open db failed:" << m_db.lastError().text();
        return false;
    }
    createTables();
    // 整个根目录被搬到新位置后，按库里记录的上次根目录重写图纸绝对路径，
    // 避免 pruneMissingDrawings() 把有效记录当成孤儿删掉
    QSqlQuery rootQ(m_db);
    QString oldRoot;
    if (rootQ.exec(QStringLiteral("SELECT value FROM app_meta WHERE key='storage_root'"))
        && rootQ.next())
        oldRoot = rootQ.value(0).toString();
    if (!oldRoot.isEmpty() && oldRoot != QDir::fromNativeSeparators(m_storageRoot))
        relocateDrawingPaths(oldRoot);
    syncStoredRoot();

    migrateLegacyProductTable();
    // 旧的两段改名迁移已合并：只修正目录、保留原文件名，收敛后幂等，
    // 不再每次启动用「当前时间戳」重算目标名导致反复重命名。
    renameDrawingsByNameModel();
    dedupeDrawings();
    dedupeQtyLogsByDay(); // 同一产品同一天只保留一条数量记录（历史重复清理 + 唯一约束）
    pruneMissingDrawings(); // 清理文件已丢失的孤儿记录（放在重命名之后，先尽量修复路径）
    seedDefaults();
    migrateHwSwToExt();
    return true;
}

// 整个根目录搬到新路径时，把 drawing.file_path 的旧根前缀换成新根
void DatabaseManager::relocateDrawingPaths(const QString &oldRoot)
{
    const QString oldPrefix = QDir::fromNativeSeparators(QDir::cleanPath(oldRoot));
    const QString newPrefix = QDir::fromNativeSeparators(m_storageRoot);
    if (oldPrefix.isEmpty() || oldPrefix == newPrefix)
        return;
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "UPDATE drawing SET file_path=? || substr(file_path, length(?) + 1) "
        "WHERE instr(file_path, ?)=1"));
    q.addBindValue(newPrefix);
    q.addBindValue(oldPrefix);
    q.addBindValue(oldPrefix);
    if (q.exec())
        qInfo() << "工作路径变更，已重定位图纸记录" << q.numRowsAffected()
                << "条：" << oldPrefix << "→" << newPrefix;
}

// 把当前工作路径记进数据库（随库一起复制，搬迁后才知道旧根是什么）
void DatabaseManager::syncStoredRoot()
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "INSERT INTO app_meta(key,value) VALUES('storage_root',?) "
        "ON CONFLICT(key) DO UPDATE SET value=excluded.value"));
    q.addBindValue(QDir::fromNativeSeparators(m_storageRoot));
    q.exec();
}

// 用 SQLite 自身的 VACUUM INTO 生成一致性快照；旧版本 SQLite 不支持时回退为文件复制
bool DatabaseManager::backupDatabase(const QString &dstPath, QString *err) const
{
    const QString src = databaseFilePath();
    if (src.isEmpty() || !QFileInfo::exists(src)) {
        if (err) *err = QStringLiteral("当前数据库文件不存在");
        return false;
    }
    const QString dst = QDir::cleanPath(dstPath);
    if (!QDir().mkpath(QFileInfo(dst).absolutePath())) {
        if (err) *err = QStringLiteral("无法创建备份目录");
        return false;
    }
    if (QFile::exists(dst) && !QFile::remove(dst)) {
        if (err) *err = QStringLiteral("无法覆盖同名备份文件");
        return false;
    }
    // 先让 WAL（若启用）落盘；再尝试 VACUUM INTO 生成一致性快照。
    // 连接上还有其它未结束的语句时 VACUUM INTO 会失败
    // （SQLite: cannot VACUUM - SQL statements in progress），此时回退为直接复制主库文件。
    QSqlQuery cp(m_db);
    cp.exec(QStringLiteral("PRAGMA wal_checkpoint(TRUNCATE)"));
    cp.finish();
    QString sqlPath = QDir::fromNativeSeparators(dst);
    sqlPath.replace(QLatin1Char('\''), QStringLiteral("''"));
    QSqlQuery q(m_db);
    if (!q.exec(QStringLiteral("VACUUM INTO '%1'").arg(sqlPath))) {
        const QString sqlErr = q.lastError().text();
        q.finish();
        QFile::remove(dst);
        if (!QFile::copy(src, dst)) {
            if (err) *err = QStringLiteral("备份失败：%1").arg(sqlErr);
            return false;
        }
    }
    if (!QFileInfo::exists(dst) || QFileInfo(dst).size() == 0) {
        if (err) *err = QStringLiteral("备份文件生成失败");
        return false;
    }
    return true;
}

// 还原：先校验备份库、给当前库留一份安全备份，再替换文件并重开连接
bool DatabaseManager::restoreDatabase(const QString &srcPath, QString *err)
{
    const QString src = QDir::cleanPath(srcPath);
    const QString cur = databaseFilePath();
    if (src.isEmpty() || !QFileInfo::exists(src) || cur.isEmpty()) {
        if (err) *err = QStringLiteral("备份文件或当前数据库不存在");
        return false;
    }
    if (QFileInfo(src).canonicalFilePath() == QFileInfo(cur).canonicalFilePath()) {
        if (err) *err = QStringLiteral("不能选择当前正在使用的数据库文件");
        return false;
    }

    const QString connName = QStringLiteral("nd_restore_check");
    bool valid = false;
    QString checkErr;
    {
        QSqlDatabase chk = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connName);
        chk.setDatabaseName(src);
        if (chk.open()) {
            QSqlQuery q(chk);
            valid = q.exec(QStringLiteral(
                        "SELECT count(*) FROM sqlite_master WHERE type='table' AND name='product'"))
                    && q.next() && q.value(0).toInt() > 0;
            if (!valid)
                checkErr = QStringLiteral("所选文件缺少 product 表，不是图纸数据库");
        } else {
            checkErr = QStringLiteral("所选文件无法打开：") + chk.lastError().text();
        }
        chk.close();
    }
    QSqlDatabase::removeDatabase(connName);
    if (!valid) {
        if (err) *err = checkErr;
        return false;
    }

    QDir().mkpath(backupDirectory());
    const QString stamp = QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd_HHmmss"));
    const QString safety = backupDirectory()
        + QStringLiteral("/nd_drawings_还原前_") + stamp + QStringLiteral(".db");
    QString backupErr;
    if (!backupDatabase(safety, &backupErr)) {
        if (err) *err = QStringLiteral("还原前自动备份失败，已取消还原：%1").arg(backupErr);
        return false;
    }

    const QString tmp = cur + QStringLiteral(".restore_tmp");
    QFile::remove(tmp);
    if (!QFile::copy(cur, tmp)) {
        if (err) *err = QStringLiteral("无法暂存当前数据库，已取消还原");
        return false;
    }

    bool ok = false;
    m_db.close();
    QFile::remove(cur + QStringLiteral("-wal"));
    QFile::remove(cur + QStringLiteral("-shm"));
    if (QFile::remove(cur) && QFile::copy(src, cur))
        ok = m_db.open();
    if (!ok) {
        m_db.close();
        QFile::remove(cur);
        QFile::copy(tmp, cur); // 回滚到还原前的库
        m_db.open();
        QFile::remove(tmp);
        if (err) *err = QStringLiteral("还原失败，已恢复原数据库");
        return false;
    }
    QFile::remove(tmp);
    createTables(); // 幂等，补齐可能缺失的表
    // 还原进来的库可能记着另一个工作路径，同样重定位后再写回当前根
    QSqlQuery rq(m_db);
    QString restoredRoot;
    if (rq.exec(QStringLiteral("SELECT value FROM app_meta WHERE key='storage_root'"))
        && rq.next())
        restoredRoot = rq.value(0).toString();
    if (!restoredRoot.isEmpty() && restoredRoot != QDir::fromNativeSeparators(m_storageRoot))
        relocateDrawingPaths(restoredRoot);
    syncStoredRoot();
    return true;
}

void DatabaseManager::createTables()
{
    QSqlQuery q(m_db);
    q.exec(QStringLiteral(
        "CREATE TABLE IF NOT EXISTS product("
        " id INTEGER PRIMARY KEY AUTOINCREMENT,"
        " category TEXT NOT NULL,"
        " material_code TEXT NOT NULL,"
        " name TEXT, model TEXT,"
        " hw_version TEXT, sw_version TEXT,"
        " remark TEXT,"
        " creator TEXT,"
        " created_at TEXT, updated_at TEXT,"
        " qty INTEGER NOT NULL DEFAULT 0,"
        " min_qty INTEGER NOT NULL DEFAULT 0,"
        " max_qty INTEGER NOT NULL DEFAULT 0,"
        " UNIQUE(category, model))"));
    q.exec(QStringLiteral(
        "CREATE TABLE IF NOT EXISTS drawing("
        " id INTEGER PRIMARY KEY AUTOINCREMENT,"
        " product_id INTEGER NOT NULL,"
        " dtype INTEGER NOT NULL,"          // 0 下发图 1 会签图
        " format TEXT,"
        " file_path TEXT,"
        " drawing_date TEXT,"               // 图纸时间
        " created_at TEXT)"));              // 录入时间
    q.exec(QStringLiteral(
        "CREATE TABLE IF NOT EXISTS product_ext("
        " product_id INTEGER NOT NULL,"
        " field_key TEXT NOT NULL,"
        " field_value TEXT,"
        " UNIQUE(product_id, field_key))"));
    q.exec(QStringLiteral(
        "CREATE TABLE IF NOT EXISTS field_config("
        " id INTEGER PRIMARY KEY AUTOINCREMENT,"
        " category TEXT NOT NULL,"
        " field_key TEXT NOT NULL,"
        " field_label TEXT NOT NULL,"
        " sort INTEGER DEFAULT 0,"
        " enabled INTEGER DEFAULT 1)"));
    q.exec(QStringLiteral(
        "CREATE TABLE IF NOT EXISTS change_log("
        " id INTEGER PRIMARY KEY AUTOINCREMENT,"
        " product_id INTEGER NOT NULL,"
        " content TEXT, oper TEXT, created_at TEXT)"));
    q.exec(QStringLiteral(
        "CREATE TABLE IF NOT EXISTS user("
        " id INTEGER PRIMARY KEY AUTOINCREMENT,"
        " username TEXT UNIQUE, password TEXT, role TEXT)"));
    // 记录数据库当前所属的工作路径根目录（随库一起复制，供整体搬迁时重定位图纸路径）
    q.exec(QStringLiteral(
        "CREATE TABLE IF NOT EXISTS app_meta("
        " key TEXT PRIMARY KEY, value TEXT)"));
    // 数量录入记录：每次录入都带日期落一条，后续按日/周/月/年统计用量
    q.exec(QStringLiteral(
        "CREATE TABLE IF NOT EXISTS product_qty_log("
        " id INTEGER PRIMARY KEY AUTOINCREMENT,"
        " product_id INTEGER NOT NULL,"
        " qty INTEGER NOT NULL,"
        " record_date TEXT NOT NULL,"   // 录入日期 yyyy-MM-dd
        " created_at TEXT)"));
    // 用户操作日志：谁在什么时候把哪个型号的什么字段从什么改成了什么
    q.exec(QStringLiteral(
        "CREATE TABLE IF NOT EXISTS op_log("
        " id INTEGER PRIMARY KEY AUTOINCREMENT,"
        " oper TEXT, action TEXT, model TEXT, name TEXT,"
        " detail TEXT, created_at TEXT)"));
    ensureProductColumns();
}

// ==================== 用户操作日志 ====================
void DatabaseManager::setCurrentOperator(const QString &name)
{
    m_currentOperator = name.trimmed();
}

QString DatabaseManager::currentOperator() const
{
    return m_currentOperator.isEmpty() ? QStringLiteral("本机") : m_currentOperator;
}

bool DatabaseManager::addOpLog(const QString &action, const QString &model,
                               const QString &name, const QString &detail)
{
    if (!m_db.isOpen())
        return false;
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "INSERT INTO op_log(oper,action,model,name,detail,created_at) VALUES(?,?,?,?,?,?)"));
    q.addBindValue(currentOperator());
    q.addBindValue(action);
    q.addBindValue(model);
    q.addBindValue(name);
    q.addBindValue(detail);
    q.addBindValue(QDateTime::currentDateTime().toString(Qt::ISODate));
    return q.exec();
}

QList<OpLog> DatabaseManager::opLogs(const QDateTime &since) const
{
    QList<OpLog> list;
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "SELECT id,oper,action,model,name,detail,created_at FROM op_log"
        " WHERE created_at>=? ORDER BY id DESC"));
    q.addBindValue(since.toString(Qt::ISODate));
    if (!q.exec())
        return list;
    while (q.next()) {
        OpLog l;
        l.id = q.value(0).toInt();
        l.oper = q.value(1).toString();
        l.action = q.value(2).toString();
        l.model = q.value(3).toString();
        l.name = q.value(4).toString();
        l.detail = q.value(5).toString();
        l.createdAt = QDateTime::fromString(q.value(6).toString(), Qt::ISODate);
        list << l;
    }
    return list;
}

bool DatabaseManager::deleteOpLog(int id)
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("DELETE FROM op_log WHERE id=?"));
    q.addBindValue(id);
    return q.exec();
}

bool DatabaseManager::clearOpLogs()
{
    QSqlQuery q(m_db);
    return q.exec(QStringLiteral("DELETE FROM op_log"));
}

// 为已存在（已迁移过）的库补齐新增列；建表时 CREATE 已含这些列，这里只处理旧库
void DatabaseManager::ensureProductColumns()
{
    QSqlQuery q(m_db);
    q.exec(QStringLiteral("PRAGMA table_info(product)"));
    QStringList have;
    while (q.next())
        have << q.value(1).toString();
    const QList<QPair<QString, QString>> cols = {
        {QStringLiteral("qty"), QStringLiteral("INTEGER NOT NULL DEFAULT 0")},
        {QStringLiteral("min_qty"), QStringLiteral("INTEGER NOT NULL DEFAULT 0")},
        {QStringLiteral("max_qty"), QStringLiteral("INTEGER NOT NULL DEFAULT 0")}};
    for (const auto &c : cols) {
        if (have.contains(c.first))
            continue;
        QSqlQuery a(m_db);
        a.exec(QStringLiteral("ALTER TABLE product ADD COLUMN %1 %2").arg(c.first, c.second));
    }
}

// 早期版本的 product 表唯一约束为 UNIQUE(category, material_code)，
// 现行逻辑按 UNIQUE(category, model) 查重。CREATE TABLE IF NOT EXISTS 不会
// 修改已存在的表，这里检测到旧约束时重建该表，避免保存时报 UNIQUE 冲突。
// 注意：旧表的列集可能与当前代码不一致（早期版本可能缺列），
// 因此按 PRAGMA table_info 的实际列迁移，缺的列迁移后补齐。
void DatabaseManager::migrateLegacyProductTable()
{
    QSqlQuery q(m_db);
    q.exec(QStringLiteral(
        "SELECT sql FROM sqlite_master WHERE type='table' AND name='product'"));
    if (!q.next())
        return; // 表不存在，交给 createTables
    QString compact;
    const QString sql = q.value(0).toString();
    for (const QChar &c : sql)
        if (!c.isSpace())
            compact += c;
    if (compact.contains(QStringLiteral("UNIQUE(category,model)")))
        return; // 已是最新结构

    // 旧表实际拥有的列
    QStringList oldCols;
    q.exec(QStringLiteral("PRAGMA table_info(product)"));
    while (q.next()) oldCols << q.value(1).toString();

    // 新表列定义（与 createTables 保持一致）
    const QStringList want = {
        QStringLiteral("id"), QStringLiteral("category"),
        QStringLiteral("material_code"), QStringLiteral("name"),
        QStringLiteral("model"), QStringLiteral("hw_version"),
        QStringLiteral("sw_version"), QStringLiteral("remark"),
        QStringLiteral("creator"), QStringLiteral("created_at"),
        QStringLiteral("updated_at"), QStringLiteral("qty"),
        QStringLiteral("min_qty"), QStringLiteral("max_qty")};
    QStringList common; // 新旧都有的列，按新表顺序
    for (const QString &c : want)
        if (oldCols.contains(c)) common << c;
    if (!common.contains(QStringLiteral("category")))
        return; // 连分类列都没有的表无法迁移

    q.exec(QStringLiteral("BEGIN"));
    QSqlQuery n(m_db);
    bool ok = n.exec(QStringLiteral(
        "CREATE TABLE product_new("
        " id INTEGER PRIMARY KEY AUTOINCREMENT,"
        " category TEXT NOT NULL,"
        " material_code TEXT NOT NULL,"
        " name TEXT, model TEXT,"
        " hw_version TEXT, sw_version TEXT,"
        " remark TEXT,"
        " creator TEXT,"
        " created_at TEXT, updated_at TEXT,"
        " qty INTEGER NOT NULL DEFAULT 0,"
        " min_qty INTEGER NOT NULL DEFAULT 0,"
        " max_qty INTEGER NOT NULL DEFAULT 0,"
        " UNIQUE(category, model))"));
    if (ok) {
        const QString cols = common.join(QStringLiteral(","));
        ok = n.exec(QStringLiteral(
            "INSERT OR IGNORE INTO product_new(%1) SELECT %1 FROM product ORDER BY id")
            .arg(cols));
    }
    ok = ok && n.exec(QStringLiteral(
        // 旧库可能存在 (category,model) 相同但物料编码不同的重复记录，
        // 未迁入的关联数据并到保留记录上（找不到对应记录时保持原值）
        "UPDATE drawing SET product_id=COALESCE("
        " (SELECT n.id FROM product_new n WHERE n.category="
        "  (SELECT category FROM product WHERE id=drawing.product_id)"
        "  AND n.model=(SELECT model FROM product WHERE id=drawing.product_id)),"
        " product_id)"
        " WHERE product_id NOT IN (SELECT id FROM product_new)"))
        && n.exec(QStringLiteral(
        "UPDATE product_ext SET product_id=COALESCE("
        " (SELECT n.id FROM product_new n WHERE n.category="
        "  (SELECT category FROM product WHERE id=product_ext.product_id)"
        "  AND n.model=(SELECT model FROM product WHERE id=product_ext.product_id)),"
        " product_id)"
        " WHERE product_id NOT IN (SELECT id FROM product_new)"))
        && n.exec(QStringLiteral(
        "UPDATE change_log SET product_id=COALESCE("
        " (SELECT n.id FROM product_new n WHERE n.category="
        "  (SELECT category FROM product WHERE id=change_log.product_id)"
        "  AND n.model=(SELECT model FROM product WHERE id=change_log.product_id)),"
        " product_id)"
        " WHERE product_id NOT IN (SELECT id FROM product_new)"))
        && n.exec(QStringLiteral("DROP TABLE product"))
        && n.exec(QStringLiteral("ALTER TABLE product_new RENAME TO product"));
    if (!ok) {
        n.exec(QStringLiteral("ROLLBACK"));
        qWarning() << "product 表迁移失败:" << n.lastError().text();
        return;
    }
    n.exec(QStringLiteral("COMMIT"));
    // 补齐旧表缺失的列（新代码会读写它们）
    for (const QString &c : want) {
        if (oldCols.contains(c)) continue;
        QSqlQuery a(m_db);
        if (c == QStringLiteral("material_code"))
            a.exec(QStringLiteral(
                "ALTER TABLE product ADD COLUMN material_code TEXT NOT NULL DEFAULT ''"));
        else if (c == QStringLiteral("qty") || c == QStringLiteral("min_qty")
                 || c == QStringLiteral("max_qty"))
            a.exec(QStringLiteral(
                "ALTER TABLE product ADD COLUMN %1 INTEGER NOT NULL DEFAULT 0").arg(c));
        else
            a.exec(QStringLiteral("ALTER TABLE product ADD COLUMN %1 TEXT").arg(c));
    }
    qInfo() << "product 表已迁移为 UNIQUE(category, model)";
}

void DatabaseManager::seedDefaults()
{
    QSqlQuery q(m_db);
    q.exec(QStringLiteral("SELECT COUNT(*) FROM field_config"));
    if (q.next() && q.value(0).toInt() == 0) {
        // 控制器默认特有字段：护套针数、硬件版本号、软件版本号（走 ext 机制，不再对所有分类显示）
        // 每次插入新建 QSqlQuery，避免复用同一查询导致绑定值累积
        auto addField = [&](const QString &key, const QString &label, int sort) {
            QSqlQuery ins(m_db);
            ins.prepare(QStringLiteral(
                "INSERT INTO field_config(category,field_key,field_label,sort) VALUES(?,?,?,?)"));
            ins.addBindValue(QStringLiteral("控制器"));
            ins.addBindValue(key);
            ins.addBindValue(label);
            ins.addBindValue(sort);
            ins.exec();
        };
        addField(QStringLiteral("护套针数"), QStringLiteral("护套针数"), 0);
        addField(QStringLiteral("hw_version"), QStringLiteral("硬件版本号"), 10);
        addField(QStringLiteral("sw_version"), QStringLiteral("软件版本号"), 11);
    }
    q.exec(QStringLiteral("SELECT COUNT(*) FROM user"));
    if (q.next() && q.value(0).toInt() == 0) {
        q.prepare(QStringLiteral("INSERT INTO user(username,password,role) VALUES(?,?,?)"));
        q.addBindValue(QStringLiteral("admin"));
        q.addBindValue(QStringLiteral("123456")); // 框架阶段明文，后续改哈希
        q.addBindValue(QStringLiteral("admin"));
        q.exec();
    }
}

// 历史数据迁移：把控制器 product 表的硬件版本号/软件版本号列搬入 product_ext
// （字段键 hw_version/sw_version），搬完后版本号即作为控制器“可配置特有字段”存在，不再对所有分类显示。
void DatabaseManager::migrateHwSwToExt()
{
    // 确保“控制器”的硬件/软件版本两个特有字段配置存在（老库可能只有“护套针数”，
    // seedDefaults 只在表为空时播种，无法覆盖已有库，故在此幂等补齐）
    {
        const auto ensureCfg = [&](const QString &key, const QString &label, int sort) {
            QSqlQuery chk(m_db);
            chk.prepare(QStringLiteral(
                "SELECT COUNT(*) FROM field_config WHERE category=? AND field_key=?"));
            chk.addBindValue(QStringLiteral("控制器"));
            chk.addBindValue(key);
            if (chk.exec() && chk.next() && chk.value(0).toInt() == 0) {
                QSqlQuery ins(m_db);
                ins.prepare(QStringLiteral(
                    "INSERT INTO field_config(category,field_key,field_label,sort) VALUES(?,?,?,?)"));
                ins.addBindValue(QStringLiteral("控制器"));
                ins.addBindValue(key);
                ins.addBindValue(label);
                ins.addBindValue(sort);
                ins.exec();
            }
        };
        ensureCfg(QStringLiteral("hw_version"), QStringLiteral("硬件版本号"), 10);
        ensureCfg(QStringLiteral("sw_version"), QStringLiteral("软件版本号"), 11);
    }

    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "SELECT id,hw_version,sw_version FROM product"
        " WHERE category=? AND (hw_version<>'' OR sw_version<>'')"));
    q.addBindValue(QStringLiteral("控制器"));
    if (!q.exec()) return;

    const auto ensure = [&](int id, const QString &key, const QString &val) {
        if (val.isEmpty()) return;
        QSqlQuery chk(m_db);
        chk.prepare(QStringLiteral(
            "SELECT 1 FROM product_ext WHERE product_id=? AND field_key=?"));
        chk.addBindValue(id); chk.addBindValue(key);
        if (chk.exec() && chk.next()) return;
        QSqlQuery ins(m_db);
        ins.prepare(QStringLiteral(
            "INSERT OR IGNORE INTO product_ext(product_id,field_key,field_value) VALUES(?,?,?)"));
        ins.addBindValue(id); ins.addBindValue(key); ins.addBindValue(val);
        ins.exec();
    };
    while (q.next()) {
        const int id = q.value(0).toInt();
        ensure(id, QStringLiteral("hw_version"), q.value(1).toString());
        ensure(id, QStringLiteral("sw_version"), q.value(2).toString());
    }
}

QList<FieldConfig> DatabaseManager::fieldConfigs(const QString &category) const
{
    if (!isServer()) return RemoteApiClient::instance().fieldConfigs(category);
    QList<FieldConfig> list;
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "SELECT id,category,field_key,field_label,sort,enabled FROM field_config"
        " WHERE category=? AND enabled=1 ORDER BY sort,id"));
    q.addBindValue(category);
    q.exec();
    while (q.next()) {
        FieldConfig f;
        f.id = q.value(0).toInt();
        f.category = q.value(1).toString();
        f.fieldKey = q.value(2).toString();
        f.fieldLabel = q.value(3).toString();
        f.sort = q.value(4).toInt();
        f.enabled = q.value(5).toInt() != 0;
        list << f;
    }
    return list;
}

QList<Product> DatabaseManager::searchProducts(const QString &category, const QString &keyword,
                                               int drawingType) const
{
    if (!isServer()) return RemoteApiClient::instance().searchProducts(category, keyword, drawingType);
    QList<Product> list;
    // 全位置占位符，避免 arg() 与命名占位符混用的兼容问题
    QString sql = QStringLiteral(
        "SELECT p.id,p.category,p.material_code,p.name,p.model,p.hw_version,p.sw_version,"
        " p.remark,p.creator,p.created_at,p.updated_at,p.qty,p.min_qty,p.max_qty,"
        " (SELECT MAX(d.drawing_date) FROM drawing d WHERE d.product_id=p.id");
    if (drawingType >= 0)
        sql += QStringLiteral(" AND d.dtype=?");
    sql += QStringLiteral(
        ") AS ddate,"
        " (SELECT COUNT(*) FROM drawing d2 WHERE d2.product_id=p.id) AS dcount"
        " FROM product p WHERE 1=1");
    if (!category.isEmpty()) // 分类为空表示跨全部分类搜索
        sql += QStringLiteral(" AND p.category=?");
    if (drawingType >= 0)
        sql += QStringLiteral(
            " AND EXISTS(SELECT 1 FROM drawing d3 WHERE d3.product_id=p.id AND d3.dtype=?)");
    const QString kw = keyword.trimmed();
    if (!kw.isEmpty())
        sql += QStringLiteral(
            " AND (p.model LIKE ? OR p.material_code LIKE ? OR p.name LIKE ? OR p.remark LIKE ?"
            " OR EXISTS(SELECT 1 FROM product_ext e WHERE e.product_id=p.id"
            " AND e.field_value LIKE ?))"); // 同时匹配各产品的分类特有字段
    sql += QStringLiteral(" ORDER BY ddate DESC, p.created_at DESC"); // 图纸时间优先

    QSqlQuery q(m_db);
    q.prepare(sql);
    if (drawingType >= 0)
        q.addBindValue(drawingType);
    if (!category.isEmpty())
        q.addBindValue(category);
    if (drawingType >= 0)
        q.addBindValue(drawingType);
    if (!kw.isEmpty()) {
        const QString like = QStringLiteral("%%1%").arg(kw);
        q.addBindValue(like);
        q.addBindValue(like);
        q.addBindValue(like);
        q.addBindValue(like);
        q.addBindValue(like); // 特有字段 field_value 的 LIKE
    }
    if (!q.exec())
        qWarning() << "searchProducts failed:" << q.lastError().text();
    while (q.next()) {
        Product p;
        p.id = q.value(0).toInt();
        p.category = q.value(1).toString();
        p.materialCode = q.value(2).toString();
        p.name = q.value(3).toString();
        p.model = q.value(4).toString();
        p.hwVersion = q.value(5).toString();
        p.swVersion = q.value(6).toString();
        p.remark = q.value(7).toString();
        p.creator = q.value(8).toString();
        p.createdAt = QDateTime::fromString(q.value(9).toString(), Qt::ISODate);
        p.updatedAt = QDateTime::fromString(q.value(10).toString(), Qt::ISODate);
        p.qty = q.value(11).toInt();
        p.minQty = q.value(12).toInt();
        p.maxQty = q.value(13).toInt();
        p.drawingDate = QDate::fromString(q.value(14).toString(), Qt::ISODate);
        p.hasDrawing = q.value(15).toInt() > 0;
        loadExtFields(p);
        list << p;
    }
    return list;
}

void DatabaseManager::loadExtFields(Product &p) const
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("SELECT field_key,field_value FROM product_ext WHERE product_id=?"));
    q.addBindValue(p.id);
    q.exec();
    while (q.next())
        p.extFields.insert(q.value(0).toString(), q.value(1).toString());
}

Product DatabaseManager::productById(int id) const
{
    if (!isServer()) return RemoteApiClient::instance().productById(id);
    Product p;
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "SELECT id,category,material_code,name,model,hw_version,sw_version,remark,creator,"
        "created_at,updated_at,qty,min_qty,max_qty FROM product WHERE id=?"));
    q.addBindValue(id);
    if (q.exec() && q.next()) {
        p.id = q.value(0).toInt();
        p.category = q.value(1).toString();
        p.materialCode = q.value(2).toString();
        p.name = q.value(3).toString();
        p.model = q.value(4).toString();
        p.hwVersion = q.value(5).toString();
        p.swVersion = q.value(6).toString();
        p.remark = q.value(7).toString();
        p.creator = q.value(8).toString();
        p.createdAt = QDateTime::fromString(q.value(9).toString(), Qt::ISODate);
        p.updatedAt = QDateTime::fromString(q.value(10).toString(), Qt::ISODate);
        p.qty = q.value(11).toInt();
        p.minQty = q.value(12).toInt();
        p.maxQty = q.value(13).toInt();
        loadExtFields(p);
    }
    return p;
}

// 按型号精确查产品（忽略大小写，与 SQLite COLLATE NOCASE 一致）
int DatabaseManager::productIdByModel(const QString &model) const
{
    if (!isServer()) return RemoteApiClient::instance().productIdByModel(model);
    const QString m = model.trimmed();
    if (m.isEmpty())
        return -1;
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "SELECT id FROM product WHERE model=? COLLATE NOCASE ORDER BY id LIMIT 1"));
    q.addBindValue(m);
    if (q.exec() && q.next())
        return q.value(0).toInt();
    return -1;
}

// 与 saveProduct 判重口径一致：分类+型号精确匹配（大小写敏感，与 UNIQUE(category,model) 一致）
int DatabaseManager::productIdByCategoryModel(const QString &category, const QString &model) const
{
    if (!isServer()) return RemoteApiClient::instance().productIdByCategoryModel(category, model);
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "SELECT id FROM product WHERE category=? AND model=? ORDER BY id LIMIT 1"));
    q.addBindValue(category);
    q.addBindValue(model);
    if (q.exec() && q.next())
        return q.value(0).toInt();
    return -1;
}

bool DatabaseManager::updateMaterialCode(int productId, const QString &code, QString *err)
{
    if (!isServer()) return RemoteApiClient::instance().updateMaterialCode(productId, code, err);
    const Product old = productById(productId);
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("UPDATE product SET material_code=?, updated_at=? WHERE id=?"));
    q.addBindValue(code.trimmed());
    q.addBindValue(QDateTime::currentDateTime().toString(Qt::ISODate));
    q.addBindValue(productId);
    if (!q.exec()) {
        if (err) *err = q.lastError().text();
        return false;
    }
    QJsonArray rows;
    rows.append(opRow(QStringLiteral("物料编码"), old.materialCode, code.trimmed()));
    addOpLog(QStringLiteral("补录物料编码"), old.model, old.name, opRowsJson(rows));
    return true;
}

bool DatabaseManager::updateQuantities(int productId, int qty, int minQty, int maxQty, QString *err)
{
    if (!isServer())
        return RemoteApiClient::instance().updateQuantities(productId, qty, minQty, maxQty, err);
    const Product old = productById(productId);
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "UPDATE product SET qty=?,min_qty=?,max_qty=?,updated_at=? WHERE id=?"));
    q.addBindValue(qty);
    q.addBindValue(minQty);
    q.addBindValue(maxQty);
    q.addBindValue(QDateTime::currentDateTime().toString(Qt::ISODate));
    q.addBindValue(productId);
    if (!q.exec()) {
        if (err) *err = q.lastError().text();
        return false;
    }
    QJsonArray rows;
    rows.append(opRow(QStringLiteral("数量"), QString::number(old.qty), QString::number(qty)));
    rows.append(opRow(QStringLiteral("最小数量"), QString::number(old.minQty), QString::number(minQty)));
    rows.append(opRow(QStringLiteral("最大数量"), QString::number(old.maxQty), QString::number(maxQty)));
    addOpLog(QStringLiteral("修改库存数量"), old.model, old.name, opRowsJson(rows));
    return true;
}

// 历史版本每次录入都 INSERT，导致同一产品同一天存在多条数量记录。
// 启动时清理：同一 (product_id, record_date) 只保留录入时间最新的一条，
// 并建唯一索引，从数据库层面保证「一天只对应一条数据」。
void DatabaseManager::dedupeQtyLogsByDay()
{
    QSqlQuery q(m_db);
    if (!q.exec(QStringLiteral(
            "SELECT 1 FROM sqlite_master WHERE type='index' AND name='idx_qty_log_day'")))
        return;
    if (q.next())
        return; // 已迁移过
    if (!q.exec(QStringLiteral(
            "DELETE FROM product_qty_log WHERE id IN ("
            " SELECT l.id FROM product_qty_log l WHERE EXISTS ("
            "  SELECT 1 FROM product_qty_log b"
            "  WHERE b.product_id=l.product_id AND b.record_date=l.record_date"
            "  AND (b.created_at>l.created_at"
            "       OR (b.created_at=l.created_at AND b.id>l.id))))"))) {
        qWarning() << "dedupeQtyLogsByDay failed:" << q.lastError().text();
        return;
    }
    const int removed = q.numRowsAffected();
    if (!q.exec(QStringLiteral(
            "CREATE UNIQUE INDEX IF NOT EXISTS idx_qty_log_day"
            " ON product_qty_log(product_id,record_date)"))) {
        qWarning() << "create qty log unique index failed:" << q.lastError().text();
        return;
    }
    if (removed > 0)
        qInfo() << "已清理同日重复的数量记录" << removed << "条";
}

bool DatabaseManager::addQtyLog(int productId, int qty, const QDate &date, QString *err)
{
    if (!isServer())
        return RemoteApiClient::instance().addQtyLog(productId, qty, date, err);
    const Product old = productById(productId);
    const QString day = (date.isValid() ? date : QDate::currentDate()).toString(Qt::ISODate);
    const QString now = QDateTime::currentDateTime().toString(Qt::ISODate);

    // 一个产品一天只对应一条数量记录：当天再次录入视为「修改」，覆盖原值而不是追加一条
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "SELECT id,qty FROM product_qty_log WHERE product_id=? AND record_date=? "
        "ORDER BY created_at DESC,id DESC LIMIT 1"));
    q.addBindValue(productId);
    q.addBindValue(day);
    if (!q.exec()) {
        if (err) *err = q.lastError().text();
        return false;
    }
    int existingId = -1, oldQty = 0;
    if (q.next()) {
        existingId = q.value(0).toInt();
        oldQty = q.value(1).toInt();
    }
    if (existingId >= 0) {
        q.prepare(QStringLiteral("UPDATE product_qty_log SET qty=?,created_at=? WHERE id=?"));
        q.addBindValue(qty);
        q.addBindValue(now);
        q.addBindValue(existingId);
    } else {
        q.prepare(QStringLiteral(
            "INSERT INTO product_qty_log(product_id,qty,record_date,created_at) VALUES(?,?,?,?)"));
        q.addBindValue(productId);
        q.addBindValue(qty);
        q.addBindValue(day);
        q.addBindValue(now);
    }
    if (!q.exec()) {
        if (err) *err = q.lastError().text();
        return false;
    }
    const bool updated = (existingId >= 0);
    QJsonArray rows;
    rows.append(opRow(QStringLiteral("每日数量（%1）").arg(day),
                      updated ? QString::number(oldQty) : QStringLiteral("（无）"),
                      QString::number(qty)));
    addOpLog(updated ? QStringLiteral("修改每日数量") : QStringLiteral("录入每日数量"),
             old.model, old.name, opRowsJson(rows));
    return true;
}

QList<QtyLog> DatabaseManager::qtyLogsOf(int productId, QString *err)
{
    if (!isServer())
        return RemoteApiClient::instance().qtyLogsOf(productId);
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "SELECT id, qty, record_date, created_at FROM product_qty_log "
        "WHERE product_id=? ORDER BY record_date DESC, created_at DESC"));
    q.addBindValue(productId);
    QList<QtyLog> list;
    if (!q.exec()) {
        if (err) *err = q.lastError().text();
        return list;
    }
    while (q.next()) {
        QtyLog l;
        l.id = q.value(0).toInt();
        l.productId = productId;
        l.qty = q.value(1).toInt();
        l.recordDate = QDate::fromString(q.value(2).toString(), Qt::ISODate);
        l.createdAt = QDateTime::fromString(q.value(3).toString(), Qt::ISODate);
        list << l;
    }
    return list;
}

bool DatabaseManager::updateQtyLog(int logId, int qty, QString *err)
{
    if (!isServer())
        return RemoteApiClient::instance().updateQtyLog(logId, qty, err);
    QSqlQuery s(m_db);
    s.prepare(QStringLiteral(
        "SELECT product_id,qty,record_date FROM product_qty_log WHERE id=?"));
    s.addBindValue(logId);
    s.exec();
    int pid = -1, oldQty = 0;
    QString day;
    if (s.next()) {
        pid = s.value(0).toInt();
        oldQty = s.value(1).toInt();
        day = s.value(2).toString();
    }
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("UPDATE product_qty_log SET qty=? WHERE id=?"));
    q.addBindValue(qty);
    q.addBindValue(logId);
    if (!q.exec()) {
        if (err) *err = q.lastError().text();
        return false;
    }
    const Product p = productById(pid);
    QJsonArray rows;
    rows.append(opRow(QStringLiteral("每日数量（%1）").arg(day), QString::number(oldQty),
                      QString::number(qty)));
    addOpLog(QStringLiteral("修改每日数量"), p.model, p.name, opRowsJson(rows));
    return true;
}

bool DatabaseManager::deleteQtyLog(int logId, QString *err)
{
    if (!isServer())
        return RemoteApiClient::instance().deleteQtyLog(logId, err);
    QSqlQuery s(m_db);
    s.prepare(QStringLiteral(
        "SELECT product_id,qty,record_date FROM product_qty_log WHERE id=?"));
    s.addBindValue(logId);
    s.exec();
    int pid = -1, oldQty = 0;
    QString day;
    if (s.next()) {
        pid = s.value(0).toInt();
        oldQty = s.value(1).toInt();
        day = s.value(2).toString();
    }
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("DELETE FROM product_qty_log WHERE id=?"));
    q.addBindValue(logId);
    if (!q.exec()) {
        if (err) *err = q.lastError().text();
        return false;
    }
    const Product p = productById(pid);
    QJsonArray rows;
    rows.append(opRow(QStringLiteral("每日数量（%1）").arg(day), QString::number(oldQty),
                      QStringLiteral("（已删除）")));
    addOpLog(QStringLiteral("删除每日数量"), p.model, p.name, opRowsJson(rows));
    return true;
}

int DatabaseManager::qtyLogForDate(int productId, const QDate &date, QString *err)
{
    if (!isServer())
        return RemoteApiClient::instance().qtyLogForDate(productId, date);
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "SELECT qty FROM product_qty_log WHERE product_id=? AND record_date=? "
        "ORDER BY created_at DESC, id DESC LIMIT 1"));
    q.addBindValue(productId);
    q.addBindValue(date.toString(Qt::ISODate));
    if (!q.exec()) {
        if (err) *err = q.lastError().text();
        return -1;
    }
    if (!q.next())
        return -1; // 该日期尚未录入
    return q.value(0).toInt();
}

QList<Drawing> DatabaseManager::drawingsOf(int productId, int drawingType) const
{
    if (!isServer()) return RemoteApiClient::instance().drawingsOf(productId, drawingType);
    QList<Drawing> list;
    QSqlQuery q(m_db);
    QString sql = QStringLiteral(
        "SELECT id,product_id,dtype,format,file_path,drawing_date,created_at"
        " FROM drawing WHERE product_id=?");
    if (drawingType >= 0)
        sql += QStringLiteral(" AND dtype=%1").arg(drawingType);
    sql += QStringLiteral(" ORDER BY drawing_date DESC");
    q.prepare(sql);
    q.addBindValue(productId);
    q.exec();
    while (q.next()) {
        Drawing d;
        d.id = q.value(0).toInt();
        d.productId = q.value(1).toInt();
        d.dtype = q.value(2).toInt();
        d.format = q.value(3).toString();
        d.filePath = q.value(4).toString();
        d.drawingDate = QDate::fromString(q.value(5).toString(), Qt::ISODate);
        d.createdAt = QDateTime::fromString(q.value(6).toString(), Qt::ISODate);
        list << d;
    }
    return list;
}

Drawing DatabaseManager::drawingById(int id) const
{
    if (!isServer()) return RemoteApiClient::instance().drawingById(id);
    Drawing d;
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "SELECT id,product_id,dtype,format,file_path,drawing_date,created_at"
        " FROM drawing WHERE id=?"));
    q.addBindValue(id);
    if (q.exec() && q.next()) {
        d.id = q.value(0).toInt();
        d.productId = q.value(1).toInt();
        d.dtype = q.value(2).toInt();
        d.format = q.value(3).toString();
        d.filePath = q.value(4).toString();
        d.drawingDate = QDate::fromString(q.value(5).toString(), Qt::ISODate);
        d.createdAt = QDateTime::fromString(q.value(6).toString(), Qt::ISODate);
    }
    return d;
}

QList<Product> DatabaseManager::productsChangedSince(const QDateTime &since) const
{
    QList<Product> list;
    QSqlQuery q(m_db);
    QString sql = QStringLiteral(
        "SELECT id,category,material_code,name,model,hw_version,sw_version,remark,creator,"
        "created_at,updated_at,qty,min_qty,max_qty FROM product");
    if (since.isValid())
        sql += QStringLiteral(" WHERE updated_at > ?");
    sql += QStringLiteral(" ORDER BY updated_at DESC, id");
    q.prepare(sql);
    if (since.isValid())
        q.addBindValue(since.toString(Qt::ISODate));
    if (!q.exec())
        qWarning() << "productsChangedSince failed:" << q.lastError().text();
    while (q.next()) {
        Product p;
        p.id = q.value(0).toInt();
        p.category = q.value(1).toString();
        p.materialCode = q.value(2).toString();
        p.name = q.value(3).toString();
        p.model = q.value(4).toString();
        p.hwVersion = q.value(5).toString();
        p.swVersion = q.value(6).toString();
        p.remark = q.value(7).toString();
        p.creator = q.value(8).toString();
        p.createdAt = QDateTime::fromString(q.value(9).toString(), Qt::ISODate);
        p.updatedAt = QDateTime::fromString(q.value(10).toString(), Qt::ISODate);
        p.qty = q.value(11).toInt();
        p.minQty = q.value(12).toInt();
        p.maxQty = q.value(13).toInt();
        loadExtFields(p);
        list << p;
    }
    return list;
}

QList<Drawing> DatabaseManager::drawingsChangedSince(const QDateTime &since) const
{
    QList<Drawing> list;
    QSqlQuery q(m_db);
    QString sql = QStringLiteral(
        "SELECT id,product_id,dtype,format,file_path,drawing_date,created_at FROM drawing");
    if (since.isValid())
        sql += QStringLiteral(" WHERE created_at > ?");
    sql += QStringLiteral(" ORDER BY id");
    q.prepare(sql);
    if (since.isValid())
        q.addBindValue(since.toString(Qt::ISODate));
    if (!q.exec())
        qWarning() << "drawingsChangedSince failed:" << q.lastError().text();
    while (q.next()) {
        Drawing d;
        d.id = q.value(0).toInt();
        d.productId = q.value(1).toInt();
        d.dtype = q.value(2).toInt();
        d.format = q.value(3).toString();
        d.filePath = q.value(4).toString();
        d.drawingDate = QDate::fromString(q.value(5).toString(), Qt::ISODate);
        d.createdAt = QDateTime::fromString(q.value(6).toString(), Qt::ISODate);
        list << d;
    }
    return list;
}

QList<ChangeLog> DatabaseManager::changeLogsOf(int productId) const
{
    if (!isServer()) return RemoteApiClient::instance().changeLogsOf(productId);
    QList<ChangeLog> list;
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "SELECT id,product_id,content,oper,created_at FROM change_log"
        " WHERE product_id=? ORDER BY id DESC"));
    q.addBindValue(productId);
    q.exec();
    while (q.next()) {
        ChangeLog c;
        c.id = q.value(0).toInt();
        c.productId = q.value(1).toInt();
        c.content = q.value(2).toString();
        c.oper = q.value(3).toString();
        c.createdAt = QDateTime::fromString(q.value(4).toString(), Qt::ISODate);
        list << c;
    }
    return list;
}

int DatabaseManager::saveProduct(Product &p, const QString &changeNote, QString *err,
                                 bool keepExistingOnEmpty)
{
    if (!isServer()) return RemoteApiClient::instance().saveProduct(p, changeNote, err, keepExistingOnEmpty);

    // 整个保存放进事务：改分类时先删 product_ext、再改 product，中途任一步失败都要回滚，
    // 否则会留下「特有字段被删、记录却没改」的半更新状态
    const bool tx = m_db.transaction();
    auto fail = [&](const QString &msg) {
        if (tx) m_db.rollback();
        if (err) *err = msg;
        return -1;
    };

    // 生成「字段 / 修改前 / 修改后」明细（JSON 数组），供设置页用户日志查看
    auto makeDetail = [this](const Product &before, const Product &after, bool created) {
        QJsonArray arr;
        auto add = [&](const QString &field, const QString &b, const QString &a) {
            if (!created && b == a)
                return;
            QJsonObject o;
            o[QStringLiteral("field")] = field;
            o[QStringLiteral("before")] = b;
            o[QStringLiteral("after")] = a;
            arr.append(o);
        };
        add(QStringLiteral("分类"), before.category, after.category);
        add(QStringLiteral("物料编码"), before.materialCode, after.materialCode);
        add(QStringLiteral("名称"), before.name, after.name);
        add(QStringLiteral("型号"), before.model, after.model);
        add(QStringLiteral("备注"), before.remark, after.remark);
        add(QStringLiteral("数量"), QString::number(before.qty), QString::number(after.qty));
        add(QStringLiteral("最小数量"), QString::number(before.minQty), QString::number(after.minQty));
        add(QStringLiteral("最大数量"), QString::number(before.maxQty), QString::number(after.maxQty));
        // 特有字段用中文标签显示
        QMap<QString, QString> labels;
        for (const FieldConfig &f : fieldConfigs(before.category))
            labels.insert(f.fieldKey, f.fieldLabel);
        for (const FieldConfig &f : fieldConfigs(after.category))
            labels.insert(f.fieldKey, f.fieldLabel);
        QMap<QString, QString> merged = before.extFields;
        for (auto it = after.extFields.constBegin(); it != after.extFields.constEnd(); ++it)
            merged.insert(it.key(), it.value());
        for (auto it = merged.constBegin(); it != merged.constEnd(); ++it) {
            add(labels.value(it.key(), it.key()), before.extFields.value(it.key()),
                after.extFields.value(it.key()));
        }
        return QString::fromUtf8(QJsonDocument(arr).toJson(QJsonDocument::Compact));
    };

    // 判重：编辑模式（p.id 有效）按 id 更新，允许改分类——否则改分类后会另建一条、
    // 原记录留在旧分类；新增模式仍按「分类+型号」判重（型号唯一；物料编码可能后补，不做判重依据）
    const int byCatModel = productIdByCategoryModel(p.category, p.model);
    int existId = byCatModel;
    if (p.id >= 0) {
        if (productById(p.id).id >= 0) {
            existId = p.id;
            // 改到新分类后，若该分类下已有同型号的其它记录，属于冲突
            if (byCatModel >= 0 && byCatModel != p.id) {
                return fail(QStringLiteral("「%1」分类下已存在型号「%2」，请先修改型号")
                                .arg(p.category, p.model));
            }
        } else {
            p.id = -1; // 库中已无此记录（被删除），按新增处理
        }
    }
    const bool exists = existId >= 0;
    const QString now = QDateTime::currentDateTime().toString(Qt::ISODate);
    Product old; // 修改前的产品，用于生成操作日志明细

    if (exists) {
        p.id = existId;
        old = productById(p.id);
        // 改了分类：原分类的特有字段（如控制器的护套针数/版本号）在新分类下没有对应项，
        // 必须整体清掉，否则会一直残留、在详情里显示成不该有的字段
        const bool categoryChanged = !old.category.isEmpty() && old.category != p.category;
        if (categoryChanged) {
            QSqlQuery delExt(m_db);
            delExt.prepare(QStringLiteral("DELETE FROM product_ext WHERE product_id=?"));
            delExt.addBindValue(p.id);
            delExt.exec();
        }
        // 补充式更新：本次没填 / OCR 没识别到的字段保留库中已有值。
        // 例如没读出物料编码时，不能把之前已补录的编码覆盖成空串。
        // 编辑模式（keepExistingOnEmpty=false）才允许主动清空字段。
        if (keepExistingOnEmpty) {
            if (p.materialCode.trimmed().isEmpty()) p.materialCode = old.materialCode;
            if (p.name.trimmed().isEmpty()) p.name = old.name;
            if (p.model.trimmed().isEmpty()) p.model = old.model;
            if (p.hwVersion.trimmed().isEmpty()) p.hwVersion = old.hwVersion;
            if (p.swVersion.trimmed().isEmpty()) p.swVersion = old.swVersion;
            if (p.remark.trimmed().isEmpty()) p.remark = old.remark;
            if (p.creator.trimmed().isEmpty()) p.creator = old.creator;
            // 数量三字段：补充式更新时，本次未给定（<=0）则保留库中已有值
            if (p.qty <= 0 && old.qty > 0) p.qty = old.qty;
            if (p.minQty <= 0 && old.minQty > 0) p.minQty = old.minQty;
            if (p.maxQty <= 0 && old.maxQty > 0) p.maxQty = old.maxQty;
            // 分类没变时才沿用旧特有字段；变了就只认本次新分类填写的值
            if (!categoryChanged) {
                for (auto it = old.extFields.constBegin(); it != old.extFields.constEnd(); ++it) {
                    if (!p.extFields.contains(it.key()) || p.extFields.value(it.key()).trimmed().isEmpty())
                        p.extFields.insert(it.key(), it.value());
                }
            }
        }
        if (p.materialCode.isNull())
            p.materialCode = QStringLiteral(""); // 列是 NOT NULL
        p.createdAt = old.createdAt;
        // 首次录入时间保持不变，只刷新修改时间
        const QString createdAt = old.createdAt.isValid()
            ? old.createdAt.toString(Qt::ISODate) : now;
        QSqlQuery u(m_db);
        u.prepare(QStringLiteral(
            "UPDATE product SET category=?,material_code=?,name=?,model=?,hw_version=?,sw_version=?,"
            "remark=?,creator=?,created_at=?,updated_at=?,qty=?,min_qty=?,max_qty=? WHERE id=?"));
        u.addBindValue(p.category);
        u.addBindValue(p.materialCode);
        u.addBindValue(p.name);
        u.addBindValue(p.model);
        u.addBindValue(p.hwVersion);
        u.addBindValue(p.swVersion);
        u.addBindValue(p.remark);
        u.addBindValue(p.creator);
        u.addBindValue(createdAt);
        u.addBindValue(now); // updated_at
        u.addBindValue(p.qty);
        u.addBindValue(p.minQty);
        u.addBindValue(p.maxQty);
        u.addBindValue(p.id);
        if (!u.exec())
            return fail(u.lastError().text());
    } else {
        p.createdAt = QDateTime::currentDateTime();
        if (p.materialCode.isNull())
            p.materialCode = QStringLiteral(""); // 列为 NOT NULL，空值写空串不写 NULL
        QSqlQuery ins(m_db);
        ins.prepare(QStringLiteral(
            "INSERT INTO product(category,material_code,name,model,hw_version,sw_version,"
            "remark,creator,created_at,updated_at,qty,min_qty,max_qty)"
            " VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?)"));
        ins.addBindValue(p.category);
        ins.addBindValue(p.materialCode); // 空值写空串，material_code 为 NOT NULL
        ins.addBindValue(p.name);
        ins.addBindValue(p.model);
        ins.addBindValue(p.hwVersion);
        ins.addBindValue(p.swVersion);
        ins.addBindValue(p.remark);
        ins.addBindValue(p.creator);
        ins.addBindValue(now);
        ins.addBindValue(now);
        ins.addBindValue(p.qty);
        ins.addBindValue(p.minQty);
        ins.addBindValue(p.maxQty);
        if (!ins.exec())
            return fail(ins.lastError().text());
        p.id = ins.lastInsertId().toInt();
    }

    // 特有字段：先清掉不属于当前分类配置的历史残留（含以前存错分类留下的字段），
    // 这样打开旧记录直接保存一次也能把多余字段删干净
    if (exists) {
        QStringList validKeys;
        for (const FieldConfig &f : fieldConfigs(p.category))
            validKeys << f.fieldKey;
        QSqlQuery sel(m_db);
        sel.prepare(QStringLiteral("SELECT field_key FROM product_ext WHERE product_id=?"));
        sel.addBindValue(p.id);
        if (sel.exec()) {
            QStringList stale;
            while (sel.next()) {
                const QString k = sel.value(0).toString();
                if (!validKeys.contains(k))
                    stale << k;
            }
            for (const QString &k : stale) {
                // 每次新建查询，避免多条删除时绑定值累积导致 Parameter count mismatch
                QSqlQuery del(m_db);
                del.prepare(QStringLiteral(
                    "DELETE FROM product_ext WHERE product_id=? AND field_key=?"));
                del.addBindValue(p.id);
                del.addBindValue(k);
                del.exec();
            }
        }
    }

    QSqlQuery ext(m_db);
    ext.prepare(QStringLiteral(
        "INSERT INTO product_ext(product_id,field_key,field_value) VALUES(?,?,?)"
        " ON CONFLICT(product_id,field_key) DO UPDATE SET field_value=excluded.field_value"));
    for (auto it = p.extFields.constBegin(); it != p.extFields.constEnd(); ++it) {
        // 补充式更新时，留空的特有字段既不写入、也不删除已有值
        if (exists && keepExistingOnEmpty && it.value().trimmed().isEmpty())
            continue;
        ext.addBindValue(p.id);
        ext.addBindValue(it.key());
        ext.addBindValue(it.value());
        ext.exec();
        ext.finish();
    }

    // 更改说明
    if (!changeNote.trimmed().isEmpty()) {
        QSqlQuery log(m_db);
        log.prepare(QStringLiteral(
            "INSERT INTO change_log(product_id,content,oper,created_at) VALUES(?,?,?,?)"));
        log.addBindValue(p.id);
        log.addBindValue(changeNote.trimmed());
        log.addBindValue(p.creator);
        log.addBindValue(now);
        log.exec();
    }
    if (tx) m_db.commit();

    // 事务提交后再记日志，日志写失败不影响主流程
    addOpLog(exists ? QStringLiteral("修改产品") : QStringLiteral("新增产品"),
             p.model, p.name, makeDetail(old, p, !exists));
    return p.id;
}

// 去掉文件名中不允许的字符（型号里可能带 / 等符号）
static QString safeFileName(const QString &s)
{
    QString r;
    r.reserve(s.size());
    for (const QChar &c : s) {
        if (c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' ||
            c == '"' || c == '<' || c == '>' || c == '|')
            r += QLatin1Char('_');
        else
            r += c;
    }
    return r.trimmed();
}

// 图纸存储目录/文件名的叶子：优先“名称 型号”，缺哪边用另一边，都为空回退“未命名”
static QString drawingLeaf(const QString &name, const QString &model)
{
    const QString n = safeFileName(name).trimmed();
    const QString m = safeFileName(model).trimmed();
    if (n.isEmpty() && m.isEmpty()) return QStringLiteral("未命名");
    if (n.isEmpty()) return m;
    if (m.isEmpty()) return n;
    return n + QStringLiteral(" ") + m;
}

// 图纸文件名：一个产品常有多张子图纸，必须能按各自的名字区分。
// 1) 原始文件名有效（拖入 / 选择 / 批量导入的真实文件）→ 用它的主干，
//    如 "1.1 电源总开关及熔断器盒HB2204-48A-022"；重名追加 _2、_3…
// 2) 程序生成的临时文件（粘贴 nd_paste_、HEIC 转码 nd_heif_、客户端上传 nd_upload_）
//    或没传名字（存量迁移）→ 沿用旧规则 "<名称 型号>" + 图纸类型 / 时间戳，
//    保证历史迁移行为不变。
static QString buildDrawingPath(const QString &dirPath, const QString &name,
                                const QString &model, const QString &suffix,
                                int dtype, const QString &srcName)
{
    QString base = drawingLeaf(name, model);
    if (base.isEmpty()) base = QStringLiteral("未命名");

    QString origin = safeFileName(QFileInfo(srcName).completeBaseName()).trimmed();
    const bool generated = origin.isEmpty() || origin.startsWith(QStringLiteral("nd_"));
    if (!generated) {
        origin = origin.left(120).trimmed(); // 限制长度，避免 Windows 路径超长
        while (origin.endsWith(QLatin1Char('.')) || origin.endsWith(QLatin1Char(' ')))
            origin.chop(1);
        if (!origin.isEmpty()) base = origin;
    }

    if (generated) {
        const QString type = (dtype == TypeCountersign) ? QStringLiteral("会签")
                                                       : QStringLiteral("下发");
        const QString stamp = QDateTime::currentDateTime()
                                  .toString(QStringLiteral("yyyyMMddhhmmsszzz"));
        const QStringList cands = {
            QStringLiteral("%1/%2.%3").arg(dirPath, base, suffix),
            QStringLiteral("%1/%2_%3.%4").arg(dirPath, base, type, suffix),
            QStringLiteral("%1/%2_%3_%4.%5").arg(dirPath, base, type, stamp, suffix)};
        for (const QString &c : cands)
            if (!QFile::exists(c)) return c;
        return cands.last();
    }

    const QString first = QStringLiteral("%1/%2.%3").arg(dirPath, base, suffix);
    if (!QFile::exists(first)) return first;
    for (int i = 2; i <= 999; ++i) {
        const QString c = QStringLiteral("%1/%2_%3.%4")
                              .arg(dirPath, base, QString::number(i), suffix);
        if (!QFile::exists(c)) return c;
    }
    return QStringLiteral("%1/%2_%3.%4")
        .arg(dirPath, base,
             QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMddhhmmsszzz")),
             suffix);
}

// 文件内容 MD5（用于图纸去重比对）
static QByteArray fileMd5(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return QByteArray();
    QCryptographicHash h(QCryptographicHash::Md5);
    h.addData(&f);
    return h.result();
}

bool DatabaseManager::addDrawing(int productId, const QString &srcFile, int dtype,
                                 const QDate &drawingDate, QString *err,
                                 const QString &srcName)
{
    if (!isServer()) return RemoteApiClient::instance().addDrawing(productId, srcFile, dtype, drawingDate, err);
    Product p = productById(productId);
    if (p.id < 0) {
        if (err) *err = QStringLiteral("产品不存在");
        return false;
    }
    // 内容去重：同一类型下已有内容完全相同的图纸（重复粘贴/重选同一文件再保存），
    // 不新增记录，只刷新该记录的日期/类型
    const QByteArray srcHash = fileMd5(srcFile);
    if (!srcHash.isEmpty()) {
        const QList<Drawing> olds = drawingsOf(productId, dtype);
        for (const Drawing &d : olds) {
            if (!QFile::exists(d.filePath) || fileMd5(d.filePath) != srcHash)
                continue;
            QSqlQuery u(m_db);
            u.prepare(QStringLiteral("UPDATE drawing SET dtype=?, drawing_date=? WHERE id=?"));
            u.addBindValue(dtype);
            u.addBindValue(drawingDate.isValid() ? drawingDate.toString(Qt::ISODate) : QString());
            u.addBindValue(d.id);
            u.exec();
            return true;
        }
    }
    QFileInfo fi(srcFile);
    const QString suffix = fi.suffix().toLower();
    // 存储目录：<图纸根目录>/<分类>/<名称 型号>/；文件按各自原名归档（重名追加 _2、_3…）
    const QString dirPath = QStringLiteral("%1/%2/%3")
                                .arg(m_storageRoot, p.category, drawingLeaf(p.name, p.model));
    QDir().mkpath(dirPath);
    // 未显式传原名时取源文件自己的文件名：PC 本机拖入/选择/批量导入都是原始文件
    const QString origin = srcName.isEmpty() ? QFileInfo(srcFile).fileName() : srcName;
    const QString dstPath = buildDrawingPath(dirPath, p.name, p.model, suffix, dtype, origin);
    if (!QFile::copy(srcFile, dstPath)) {
        if (err) *err = QStringLiteral("文件拷贝失败");
        return false;
    }
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "INSERT INTO drawing(product_id,dtype,format,file_path,drawing_date,created_at)"
        " VALUES(?,?,?,?,?,?)"));
    q.addBindValue(productId);
    q.addBindValue(dtype);
    q.addBindValue(suffix);
    q.addBindValue(dstPath);
    q.addBindValue(drawingDate.isValid() ? drawingDate.toString(Qt::ISODate) : QString());
    q.addBindValue(QDateTime::currentDateTime().toString(Qt::ISODate));
    if (!q.exec()) {
        if (err) *err = q.lastError().text();
        return false;
    }
    QJsonArray rows;
    rows.append(opRow(QStringLiteral("图纸文件"), QStringLiteral("（无）"), QFileInfo(dstPath).fileName()));
    rows.append(opRow(QStringLiteral("图纸类型"), QStringLiteral("（无）"),
                      dtype == TypeCountersign ? QStringLiteral("会签图") : QStringLiteral("下发图")));
    rows.append(opRow(QStringLiteral("图纸时间"), QStringLiteral("（无）"),
                      drawingDate.isValid() ? drawingDate.toString(Qt::ISODate) : QStringLiteral("-")));
    addOpLog(QStringLiteral("新增图纸"), p.model, p.name, opRowsJson(rows));
    return true;
}

bool DatabaseManager::updateDrawingMeta(int drawingId, int dtype, const QDate &drawingDate,
                                        QString *err)
{
    if (!isServer()) return RemoteApiClient::instance().updateDrawingMeta(drawingId, dtype, drawingDate, err);
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("UPDATE drawing SET dtype=?, drawing_date=? WHERE id=?"));
    q.addBindValue(dtype);
    q.addBindValue(drawingDate.isValid() ? drawingDate.toString(Qt::ISODate) : QString());
    q.addBindValue(drawingId);
    if (!q.exec()) {
        if (err) *err = q.lastError().text();
        return false;
    }
    return true;
}

// 清理历史重复图纸：同一产品、同一类型、文件内容完全相同的记录只保留最早一条，
// 其余删除记录及多余文件（文件路径相同则只删记录）
void DatabaseManager::dedupeDrawings()
{
    QSqlQuery pq(m_db);
    pq.exec(QStringLiteral("SELECT DISTINCT product_id FROM drawing"));
    QList<int> pids;
    while (pq.next()) pids << pq.value(0).toInt();

    QSqlQuery del(m_db);
    del.prepare(QStringLiteral("DELETE FROM drawing WHERE id=?"));
    for (const int pid : pids) {
        QList<Drawing> ds = drawingsOf(pid);
        std::sort(ds.begin(), ds.end(),
                  [](const Drawing &a, const Drawing &b) { return a.id < b.id; });
        for (int i = 1; i < ds.size(); ++i) {
            if (!QFile::exists(ds[i].filePath)) continue;
            for (int j = 0; j < i; ++j) {
                if (ds[j].dtype != ds[i].dtype || !QFile::exists(ds[j].filePath)) continue;
                if (QFileInfo(ds[j].filePath).size() != QFileInfo(ds[i].filePath).size()) continue;
                if (fileMd5(ds[j].filePath) != fileMd5(ds[i].filePath)) continue;
                del.addBindValue(ds[i].id);
                if (!del.exec())
                    qWarning() << "删除重复图纸记录失败:" << del.lastError().text();
                del.finish();
                if (ds[i].filePath.compare(ds[j].filePath, Qt::CaseInsensitive) != 0)
                    QFile::remove(ds[i].filePath); // 指向同一文件时保留文件本体
                qInfo() << "已清理重复图纸记录 id=" << ds[i].id << ds[i].filePath;
                break;
            }
        }
    }
}

// 清理“文件已不存在”的孤儿图纸记录：例如在资源管理器里手动删掉了重复/多余的图片，
// 或重命名迁移中断导致路径失效。这些记录若不清掉，会出现在列表和手机同步清单里
// （手机端表现为“图片未下载或文件缺失”的幽灵条目）。
// 仅当存储根目录确实存在时才执行，避免磁盘未挂载/离线时误删全部记录。
void DatabaseManager::pruneMissingDrawings()
{
    if (!QFileInfo::exists(m_storageRoot)) {
        qWarning() << "存储根目录不可用，跳过孤儿图纸清理:" << m_storageRoot;
        return;
    }
    QSqlQuery q(m_db);
    if (!q.exec(QStringLiteral("SELECT id,file_path FROM drawing")))
        return;
    QList<int> stale;
    while (q.next()) {
        const QString path = q.value(1).toString();
        if (path.isEmpty() || !QFileInfo::exists(path))
            stale << q.value(0).toInt();
    }
    if (stale.isEmpty())
        return;
    QSqlQuery del(m_db);
    del.prepare(QStringLiteral("DELETE FROM drawing WHERE id=?"));
    for (const int id : stale) {
        del.addBindValue(id);
        if (del.exec()) qInfo() << "已清理孤儿图纸记录 id=" << id;
        else qWarning() << "清理孤儿图纸失败:" << del.lastError().text();
        del.finish();
    }
}

// 存量图纸归档目录修正：把历史文件（含旧「分类/型号」结构）移动到
// <根>/<分类>/<名称 型号>/ 下，文件名保持不变，并回写 file_path 与 created_at。
// 幂等：第一次移动后 cur == target，之后启动直接跳过，不会反复重命名。
void DatabaseManager::renameDrawingsByNameModel()
{
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "SELECT d.id,d.file_path,d.dtype,p.category,p.name,p.model FROM drawing d"
        " JOIN product p ON p.id=d.product_id ORDER BY d.id"));
    if (!q.exec()) return;

    struct Item { int id; QString path; int dtype; QString dir; QString name; QString model; };
    QList<Item> items;
    while (q.next()) {
        const QString category = q.value(3).toString();
        const QString name = q.value(4).toString();
        const QString model = q.value(5).toString();
        items.append({q.value(0).toInt(), q.value(1).toString(), q.value(2).toInt(),
                      QStringLiteral("%1/%2/%3").arg(m_storageRoot, category,
                                                    drawingLeaf(name, model)),
                      name, model});
    }

    QSqlQuery u(m_db);
    u.prepare(QStringLiteral(
        "UPDATE drawing SET file_path=?, created_at=? WHERE id=?"));
    const QString now = QDateTime::currentDateTime().toString(Qt::ISODate);
    for (const Item &it : items) {
        if (it.path.isEmpty() || !QFile::exists(it.path)) continue; // 文件已丢失，跳过
        QDir().mkpath(it.dir);
        // 只修正目录（分类/名称 型号），文件名原样保留：
        // 下发/会签图纸文件名里的时间戳是区分同产品多张图纸的依据，
        // 绝不能用「当前时间」重算目标名，否则每次启动目标都不同、会反复重命名。
        const QString target = QDir(it.dir).filePath(QFileInfo(it.path).fileName());
        const QString cur = QFileInfo(it.path).absoluteFilePath();
        if (cur.compare(target, Qt::CaseInsensitive) == 0) continue; // 已归档到规范目录
        if (QFile::exists(target)) continue;                         // 目标已有同名文件，保持原样
        QFile f(it.path);
        if (!f.rename(target)) {
            qWarning() << "图纸重命名失败:" << it.path << "->" << target
                       << "原因:" << f.errorString();
            continue;
        }
        u.addBindValue(target);
        u.addBindValue(now);          // 刷新 created_at，触发手机端按新路径重新同步
        u.addBindValue(it.id);
        if (!u.exec()) qWarning() << "图纸路径回写失败:" << u.lastError().text();
        u.finish();
    }
}

bool DatabaseManager::verifyUser(const QString &name, const QString &password) const
{
    if (!isServer()) return RemoteApiClient::instance().verifyUser(name, password);
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("SELECT COUNT(*) FROM user WHERE username=? AND password=?"));
    q.addBindValue(name);
    q.addBindValue(password);
    q.exec();
    return q.next() && q.value(0).toInt() > 0;
}

// 验证并返回角色（admin / editor），失败返回空串
QString DatabaseManager::userRole(const QString &name, const QString &password) const
{
    if (!isServer()) return RemoteApiClient::instance().login(name, password);
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("SELECT role FROM user WHERE username=? AND password=?"));
    q.addBindValue(name);
    q.addBindValue(password);
    if (q.exec() && q.next())
        return q.value(0).toString();
    return QString();
}

// ---------- 字段配置管理 ----------
bool DatabaseManager::addFieldConfig(const QString &category, const QString &key,
                                     const QString &label, QString *err)
{
    if (!isServer()) return RemoteApiClient::instance().addFieldConfig(category, key, label, err);
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "INSERT INTO field_config(category,field_key,field_label,sort) VALUES(?,?,?,"
        "(SELECT IFNULL(MAX(sort),0)+1 FROM field_config WHERE category=?))"));
    q.addBindValue(category);
    q.addBindValue(key);
    q.addBindValue(label);
    q.addBindValue(category);
    if (!q.exec()) {
        if (err) *err = q.lastError().text();
        return false;
    }
    return true;
}

bool DatabaseManager::deleteFieldConfig(int id)
{
    if (!isServer()) return RemoteApiClient::instance().deleteFieldConfig(id);
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("DELETE FROM field_config WHERE id=?"));
    q.addBindValue(id);
    return q.exec() && q.numRowsAffected() > 0;
}

// ---------- 账号管理 ----------
QList<QPair<int, QString>> DatabaseManager::listUsers() const
{
    if (!isServer()) return RemoteApiClient::instance().listUsers();
    QList<QPair<int, QString>> list;
    QSqlQuery q(m_db);
    q.exec(QStringLiteral("SELECT id,username FROM user ORDER BY id"));
    while (q.next())
        list.append({q.value(0).toInt(), q.value(1).toString()});
    return list;
}

bool DatabaseManager::addUser(const QString &name, const QString &password, QString *err)
{
    if (!isServer()) return RemoteApiClient::instance().addUser(name, password, err);
    if (name.trimmed().isEmpty() || password.isEmpty()) {
        if (err) *err = QStringLiteral("用户名和密码不能为空");
        return false;
    }
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("INSERT INTO user(username,password,role) VALUES(?,?,'editor')"));
    q.addBindValue(name.trimmed());
    q.addBindValue(password);
    if (!q.exec()) {
        if (err) *err = QStringLiteral("添加失败（用户名可能已存在）：%1").arg(q.lastError().text());
        return false;
    }
    return true;
}

bool DatabaseManager::deleteUser(int id, QString *err)
{
    if (!isServer()) return RemoteApiClient::instance().deleteUser(id, err);
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("SELECT username FROM user WHERE id=?"));
    q.addBindValue(id);
    if (q.exec() && q.next() && q.value(0).toString() == QStringLiteral("admin")) {
        if (err) *err = QStringLiteral("admin 账号不可删除");
        return false;
    }
    QSqlQuery d(m_db);
    d.prepare(QStringLiteral("DELETE FROM user WHERE id=?"));
    d.addBindValue(id);
    if (!d.exec() || d.numRowsAffected() == 0) {
        if (err) *err = d.lastError().text();
        return false;
    }
    return true;
}

bool DatabaseManager::changeUserPassword(int id, const QString &newPassword, QString *err)
{
    if (!isServer()) return RemoteApiClient::instance().changeUserPassword(id, newPassword, err);
    if (newPassword.isEmpty()) {
        if (err) *err = QStringLiteral("密码不能为空");
        return false;
    }
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("UPDATE user SET password=? WHERE id=?"));
    q.addBindValue(newPassword);
    q.addBindValue(id);
    if (!q.exec() || q.numRowsAffected() == 0) {
        if (err) *err = q.lastError().text();
        return false;
    }
    return true;
}

// ---------- 产品删除 / 更改记录管理 ----------
bool DatabaseManager::deleteProducts(const QList<int> &ids, QString *err)
{
    if (!isServer()) return RemoteApiClient::instance().deleteProducts(ids, err);
    if (ids.isEmpty()) return true;
    m_db.transaction();
    QList<Product> removed;
    for (int id : ids) {
        removed << productById(id); // 删除前留一份，供操作日志显示
        // 删除归档的图纸文件
        QSqlQuery dq(m_db);
        dq.prepare(QStringLiteral("SELECT file_path FROM drawing WHERE product_id=?"));
        dq.addBindValue(id);
        if (dq.exec())
            while (dq.next()) {
                const QString fp = dq.value(0).toString();
                if (!fp.isEmpty()) QFile::remove(fp);
            }
        const QStringList tables = {QStringLiteral("product_ext"), QStringLiteral("drawing"),
                                    QStringLiteral("change_log"), QStringLiteral("product")};
        for (const QString &t : tables) {
            QSqlQuery q(m_db);
            q.prepare(QStringLiteral("DELETE FROM %1 WHERE %2").arg(
                t, t == QStringLiteral("product") ? QStringLiteral("id=?")
                                                  : QStringLiteral("product_id=?")));
            q.addBindValue(id);
            if (!q.exec()) {
                m_db.rollback();
                if (err) *err = q.lastError().text();
                return false;
            }
        }
    }
    m_db.commit();
    for (const Product &old : removed) {
        if (old.id < 0) continue;
        QJsonArray rows;
        rows.append(opRow(QStringLiteral("产品记录"), QStringLiteral("存在"), QStringLiteral("（已删除）")));
        rows.append(opRow(QStringLiteral("分类"), old.category, QStringLiteral("（已删除）")));
        rows.append(opRow(QStringLiteral("物料编码"), old.materialCode, QStringLiteral("（已删除）")));
        rows.append(opRow(QStringLiteral("名称"), old.name, QStringLiteral("（已删除）")));
        addOpLog(QStringLiteral("删除产品"), old.model, old.name, opRowsJson(rows));
    }
    return true;
}

bool DatabaseManager::updateChangeLog(int id, const QString &content, QString *err)
{
    if (!isServer()) return RemoteApiClient::instance().updateChangeLog(id, content, err);
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("UPDATE change_log SET content=? WHERE id=?"));
    q.addBindValue(content);
    q.addBindValue(id);
    if (!q.exec() || q.numRowsAffected() == 0) {
        if (err) *err = q.lastError().text();
        return false;
    }
    return true;
}

bool DatabaseManager::deleteChangeLog(int id)
{
    if (!isServer()) return RemoteApiClient::instance().deleteChangeLog(id);
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("DELETE FROM change_log WHERE id=?"));
    q.addBindValue(id);
    return q.exec() && q.numRowsAffected() > 0;
}

