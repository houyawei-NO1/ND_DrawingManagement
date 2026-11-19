#include "databasemanager.h"
#include "server/remoteapiclient.h"
#include <QSqlQuery>
#include <QSqlError>
#include <QThread>
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
#include <QHash>
#include <QRegularExpression>
#include <QSet>
#include <QMap>
#include "util/xlsreader.h"
#include "util/moldtypes.h"
#include "util/cuttypes.h"

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
    // 客户端模式默认连接 192.168.1.16；用户仍可在「设置 → 本机角色 / 服务器连接」修改
    return QSettings().value(QStringLiteral("server/host"),
                             QStringLiteral("192.168.1.16")).toString().trimmed();
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
    seedChipData(); // 首次启动把 Excel 数据（已转成 JSON 资源）灌入 chip_item
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
    seedChipData(); // 旧备份里没有芯片台账时补入初始数据（有数据则不动）
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
    // ERP 库存档案：批次信息 + 明细，存在主库里，随备份一起走
    q.exec(QStringLiteral(
        "CREATE TABLE IF NOT EXISTS stock_batch("
        " key TEXT PRIMARY KEY, file_name TEXT, label TEXT,"
        " rows INTEGER, imported_at TEXT)"));
    q.exec(QStringLiteral(
        "CREATE TABLE IF NOT EXISTS stock_archive("
        " batch TEXT NOT NULL, ord INTEGER NOT NULL,"
        " row_no TEXT, code TEXT, name TEXT, model TEXT, spec TEXT,"
        " origin TEXT, supplier TEXT, safe_qty TEXT,"
        " creator TEXT, created_at TEXT, modifier TEXT, modified_at TEXT,"
        " package TEXT, search TEXT)"));
    q.exec(QStringLiteral(
        "CREATE INDEX IF NOT EXISTS idx_stock_batch ON stock_archive(batch)"));
    // 重要芯片台账：工具 → 芯片查询专用，与 product 表完全解耦（没有图纸、不参与产品搜索）
    q.exec(QStringLiteral(
        "CREATE TABLE IF NOT EXISTS chip_item("
        " id INTEGER PRIMARY KEY AUTOINCREMENT,"
        " hardware TEXT NOT NULL,"          // 硬件版本 ← 物资小类
        " category TEXT,"                   // 类型     ← 零部件中使用进口芯片名称
        " model TEXT NOT NULL,"             // 型号     ← 芯片型号
        " qty INTEGER NOT NULL DEFAULT 0,"  // 数量     ← 使用数量
        " function TEXT,"                   // 功能
        " country TEXT,"                    // 国家
        " brand TEXT,"                      // 品牌
        " remark TEXT,"                     // 备注（预留为空）
        " creator TEXT, created_at TEXT, updated_at TEXT)"));
    q.exec(QStringLiteral(
        "CREATE INDEX IF NOT EXISTS idx_chip_hardware ON chip_item(hardware)"));
    q.exec(QStringLiteral(
        "CREATE INDEX IF NOT EXISTS idx_chip_model ON chip_item(model)"));
    // 端子机模具台账：模具号可空（老模具/刀片无编号），status 标记待完善。
    // terminals 存适用端子（多个用「、」连接），便于老模具无编号时按端子反查。
    q.exec(QStringLiteral(
        "CREATE TABLE IF NOT EXISTS mold_item("
        " id INTEGER PRIMARY KEY AUTOINCREMENT,"
        " mold_code TEXT,"
        " machine_type TEXT,"
        " terminals TEXT,"
        " press_wire TEXT,"
        " terminal_info TEXT,"
        " seal_info TEXT,"
        " machine_no TEXT,"
        " seal_image TEXT,"
        " terminal_image TEXT,"
        " harness TEXT,"
        " orientation TEXT,"
        " department TEXT,"
        " area TEXT,"
        " keeper TEXT,"
        " vendor TEXT,"
        " remark TEXT,"
        " status INTEGER NOT NULL DEFAULT 0,"
        " creator TEXT, created_at TEXT, updated_at TEXT)"));
    q.exec(QStringLiteral(
        "CREATE INDEX IF NOT EXISTS idx_mold_code ON mold_item(mold_code)"));
    ensureMoldColumns();
    // 线束下线：线束 → 明细 → 任务 → 机器，派工规则可配置。
    // 物理表已从 cut_batch 改名为 cut_harness；name 列存放表格型号，batch_no 存放表格批次号。
    // HTTP/AutoIt 仍使用 batchName/batchNo 字段名，避免跨端协议变动。
    q.exec(QStringLiteral(
        "CREATE TABLE IF NOT EXISTS cut_harness("
        " id INTEGER PRIMARY KEY AUTOINCREMENT,"
        " name TEXT, batch_no TEXT, file_name TEXT, quantity TEXT, total_rows INTEGER DEFAULT 0,"
        " creator TEXT, created_at TEXT)"));
    {
        // 旧库把 cut_batch 改名为 cut_harness，只迁移一次。
        // 坑：查询结果集不关闭时（oldTable 仍 active）SQLite 会锁住 cut_batch，
        // 导致后面的 DROP TABLE 静默失败；旧表留在库里，每次启动又把已删除的线束
        // 回填进 cut_harness，表现为「删除后重开软件线束又回来了」。
        QSqlQuery probe(m_db);
        bool hasOldTable = probe.exec(QStringLiteral(
                "SELECT name FROM sqlite_master WHERE type='table' AND name='cut_batch'"))
            && probe.next();
        probe.finish();               // 必须先关游标释放读锁，否则 DROP 会被锁住
        if (hasOldTable) {
            QSqlQuery mark(m_db);
            const bool migrated = mark.exec(QStringLiteral(
                    "SELECT value FROM app_meta WHERE key='cut_harness_migrated'"))
                && mark.next() && mark.value(0).toString() == QLatin1String("1");
            mark.finish();
            if (!migrated) {
                // 旧库可能缺少 batch_no 列；列已存在时 ALTER 会失败，忽略即可
                QSqlQuery alter(m_db);
                alter.exec(QStringLiteral("ALTER TABLE cut_batch ADD COLUMN batch_no TEXT"));
                alter.finish();
                QSqlQuery copy(m_db);
                copy.exec(QStringLiteral(
                    "INSERT INTO cut_harness(id,name,batch_no,file_name,quantity,total_rows,creator,created_at) "
                    "SELECT id,name,batch_no,file_name,quantity,total_rows,creator,created_at FROM cut_batch "
                    "WHERE NOT EXISTS (SELECT 1 FROM cut_harness WHERE cut_harness.id=cut_batch.id)"));
                copy.finish();
                QSqlQuery flag(m_db);
                flag.exec(QStringLiteral(
                    "INSERT OR REPLACE INTO app_meta(key,value) VALUES('cut_harness_migrated','1')"));
                flag.finish();
            }
            // 旧表必须清掉；失败时记日志并重试，绝不能留着反复回填
            QSqlQuery dropQ(m_db);
            for (int attempt = 0; attempt < 3; ++attempt) {
                if (dropQ.exec(QStringLiteral("DROP TABLE IF EXISTS cut_batch")))
                    break;
                qWarning() << "DROP TABLE cut_batch failed:" << dropQ.lastError().text();
                dropQ.finish();
                QThread::msleep(50);
            }
            dropQ.finish();
            QSqlQuery seqOld(m_db);
            seqOld.exec(QStringLiteral("DELETE FROM sqlite_sequence WHERE name='cut_batch'"));
            seqOld.finish();
        }
    }
    // 空库时同步清掉自增计数，避免外部删行后新线束仍从旧 ID 继续
    {
        QSqlQuery seq(m_db);
        if (seq.exec(QStringLiteral("SELECT COUNT(*) FROM cut_harness")) && seq.next()
            && seq.value(0).toInt() == 0)
            seq.exec(QStringLiteral("DELETE FROM sqlite_sequence WHERE name='cut_harness'"));
    }
    q.exec(QStringLiteral(
        "CREATE TABLE IF NOT EXISTS cut_item("
        " id INTEGER PRIMARY KEY AUTOINCREMENT,"
        " batch_id INTEGER NOT NULL, seq INTEGER, circuit TEXT,"
        " material TEXT, section TEXT, color TEXT, wire_length TEXT,"
        " strip_left TEXT, strip_right TEXT, terminal_left TEXT, terminal_right TEXT,"
        " work_left TEXT, work_right TEXT,"
        " created_at TEXT)"));
    q.exec(QStringLiteral("CREATE INDEX IF NOT EXISTS idx_cut_item_batch ON cut_item(batch_id)"));
    // machine_no（下线表原表里的机台号）已彻底废弃：派工只看规则 + 线径能力 + 均衡。
    // 老库升级时把这列删掉，避免历史数据误导。
    {
        QSqlQuery ti(m_db);
        bool hasMachineNo = false;
        if (ti.exec(QStringLiteral("PRAGMA table_info(cut_item)")))
            while (ti.next())
                if (ti.value(1).toString() == QLatin1String("machine_no")) { hasMachineNo = true; break; }
        ti.finish();
        if (hasMachineNo) {
            QSqlQuery dropCol(m_db);
            if (!dropCol.exec(QStringLiteral("ALTER TABLE cut_item DROP COLUMN machine_no")))
                qWarning() << "drop cut_item.machine_no failed:" << dropCol.lastError().text();
        }
    }
    q.exec(QStringLiteral(
        "CREATE TABLE IF NOT EXISTS cut_task("
        " id INTEGER PRIMARY KEY AUTOINCREMENT,"
        " batch_id INTEGER NOT NULL, item_id INTEGER NOT NULL, machine_id INTEGER NOT NULL,"
        " quantity INTEGER DEFAULT 1, done_qty INTEGER DEFAULT 0, status INTEGER DEFAULT 0,"
        " operator TEXT, created_at TEXT, finished_at TEXT)"));
    q.exec(QStringLiteral("CREATE INDEX IF NOT EXISTS idx_cut_task_machine ON cut_task(machine_id,status)"));
    q.exec(QStringLiteral(
        "CREATE TABLE IF NOT EXISTS cut_machine("
        " id INTEGER PRIMARY KEY AUTOINCREMENT,"
        " code TEXT UNIQUE, name TEXT, capabilities TEXT, active INTEGER DEFAULT 1)"));
    // 首次启动播种 5 台机器（名称：一号机~五号机）
    if (!q.exec(QStringLiteral("SELECT COUNT(*) FROM cut_machine)"))
        || !q.next() || q.value(0).toInt() == 0) {
        const QStringList names = { QStringLiteral("一号机"), QStringLiteral("二号机"),
            QStringLiteral("三号机"), QStringLiteral("四号机"), QStringLiteral("五号机") };
        for (int i = 0; i < names.size(); ++i) {
            QSqlQuery ins(m_db);
            ins.prepare(QStringLiteral("INSERT INTO cut_machine(code,name,capabilities,active) VALUES(?,?,?,1)"));
            ins.addBindValue(QStringLiteral("M%1").arg(i + 1));
            ins.addBindValue(names.at(i));
            ins.addBindValue(QStringLiteral("{}"));
            ins.exec();
        }
    }
    // 「其他」= 手动/人工下线，不参与智能分配，只供人工派工时选择（老库也能补上）
    {
        QSqlQuery chk(m_db);
        if (chk.exec(QStringLiteral("SELECT id FROM cut_machine WHERE code='OTHER'")) && !chk.next()) {
            QSqlQuery ins(m_db);
            ins.exec(QStringLiteral(
                "INSERT INTO cut_machine(code,name,capabilities,active) VALUES('OTHER','其他','{}',1)"));
        }
    }
    q.exec(QStringLiteral(
        "CREATE TABLE IF NOT EXISTS cut_rule("
        " id INTEGER PRIMARY KEY AUTOINCREMENT,"
        " field TEXT, match_type TEXT, match_value TEXT,"
        " target_machine TEXT, enabled INTEGER DEFAULT 1)"));
    // 下线机设备端：设备授权、当前任务、打印日志
    q.exec(QStringLiteral(
        "CREATE TABLE IF NOT EXISTS cut_device("
        " machine_id INTEGER PRIMARY KEY,"
        " machine_name TEXT, device_key TEXT, active INTEGER DEFAULT 1)"));
    q.exec(QStringLiteral(
        "CREATE TABLE IF NOT EXISTS cut_device_state("
        " machine_id INTEGER PRIMARY KEY,"
        " current_task_id INTEGER DEFAULT 0,"
        " last_start_at TEXT, last_confirm_at TEXT, updated_at TEXT)"));
    q.exec(QStringLiteral(
        "CREATE TABLE IF NOT EXISTS cut_print_log("
        " id INTEGER PRIMARY KEY AUTOINCREMENT,"
        " task_id INTEGER, machine_id INTEGER, operator TEXT,"
        " label_no TEXT, copies INTEGER, result TEXT, created_at TEXT)"));
    {
        const QStringList names = { QStringLiteral("一号机"), QStringLiteral("二号机"),
            QStringLiteral("三号机"), QStringLiteral("四号机"), QStringLiteral("五号机") };
        for (int i = 0; i < names.size(); ++i) {
            QSqlQuery ins(m_db);
            ins.prepare(QStringLiteral(
                "INSERT OR IGNORE INTO cut_device(machine_id,machine_name,device_key,active)"
                " VALUES(?,?,?,1)"));
            ins.addBindValue(i + 1);
            ins.addBindValue(names.at(i));
            ins.addBindValue(QStringLiteral("ND-M%1").arg(i + 1));
            ins.exec();
        }
    }
    ensureProductColumns();
}

// ==================== ERP 库存档案 ====================
namespace {
// 存货档案数据-2026_11_07-16_40_38.xls → 管家婆20261107 16:40数据
QString stockBatchLabel(const QString &fileName)
{
    static const QRegularExpression re(QStringLiteral(
        "(\\d{4})[_-](\\d{2})[_-](\\d{2})[-_ T]*(\\d{2})[_-](\\d{2})[_-](\\d{2})"));
    const QRegularExpressionMatch m = re.match(fileName);
    if (m.hasMatch())
        return QStringLiteral("管家婆%1%2%3 %4:%5数据")
            .arg(m.captured(1), m.captured(2), m.captured(3),
                 m.captured(4), m.captured(5));
    return QFileInfo(fileName).completeBaseName();
}

// 从存货全名/型号里提取常见贴片封装，方便一眼看到 0603、0805 这类信息
QString stockPackageOf(const QString &name, const QString &model)
{
    static const QRegularExpression re(QStringLiteral(
        "C?(0201|0402|0603|0805|1206|1210|1812|2010|2512)"),
        QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch m = re.match(name + QChar(' ') + model);
    return m.hasMatch() ? m.captured(1) : QString();
}

QString stockRowText(const QStringList &c)
{
    return c.join(QChar(' ')).toLower();
}
} // namespace

QList<StockBatch> DatabaseManager::stockBatches(QString *err) const
{
    if (!isServer())
        return RemoteApiClient::instance().stockBatches(err);
    QList<StockBatch> out;
    QSqlQuery q(m_db);
    if (!q.exec(QStringLiteral(
            "SELECT key,file_name,label,rows,imported_at FROM stock_batch "
            "ORDER BY imported_at DESC"))) {
        if (err) *err = q.lastError().text();
        return out;
    }
    while (q.next()) {
        StockBatch b;
        b.key = q.value(0).toString();
        b.fileName = q.value(1).toString();
        b.label = q.value(2).toString();
        b.rows = q.value(3).toInt();
        b.importedAt = q.value(4).toString();
        out.append(b);
    }
    return out;
}

QList<QStringList> DatabaseManager::stockSearch(const QString &batchKey,
                                                const QString &keyword, int limit,
                                                int *total, QString *err) const
{
    if (total) *total = 0;
    if (!isServer())
        return RemoteApiClient::instance().stockSearch(batchKey, keyword, limit, total, err);
    QList<QStringList> out;
    if (batchKey.isEmpty() || keyword.trimmed().isEmpty())
        return out;
    // instr 是纯子串匹配，用户输入的 %、_ 不会被当成 LIKE 通配符；两边统一小写
    const QString kw = keyword.trimmed().toLower();
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "SELECT COUNT(*) FROM stock_archive WHERE batch=? AND instr(search, ?) > 0"));
    q.addBindValue(batchKey);
    q.addBindValue(kw);
    if (q.exec() && q.next() && total)
        *total = q.value(0).toInt();
    q.prepare(QStringLiteral(
        "SELECT row_no,code,name,model,spec,origin,supplier,safe_qty,"
        "creator,created_at,modifier,modified_at,package "
        "FROM stock_archive WHERE batch=? AND instr(search, ?) > 0 "
        "ORDER BY ord LIMIT ?"));
    q.addBindValue(batchKey);
    q.addBindValue(kw);
    q.addBindValue(qMax(1, limit));
    if (!q.exec()) {
        if (err) *err = q.lastError().text();
        return out;
    }
    while (q.next()) {
        QStringList row;
        row.reserve(13);
        for (int i = 0; i < 13; ++i)
            row.append(q.value(i).toString());
        out.append(row);
    }
    return out;
}

bool DatabaseManager::importStockFile(const QString &srcPath,
                                      const QString &originalName, QString *err)
{
    if (!isServer())
        return RemoteApiClient::instance().importStockFile(srcPath, originalName, err);

    // 归档到服务器工作目录的「数据库」文件夹，保留 ERP 原始文件名
    const QString fileName = QFileInfo(originalName.isEmpty() ? srcPath : originalName).fileName();
    if (fileName.isEmpty()) {
        if (err) *err = QStringLiteral("文件名无效");
        return false;
    }
    const QString dir = dbDirectory();
    if (dir.isEmpty()) {
        if (err) *err = QStringLiteral("服务器工作目录未配置");
        return false;
    }
    if (!QDir().mkpath(dir)) {
        if (err) *err = QStringLiteral("无法创建数据库文件夹：%1").arg(dir);
        return false;
    }
    const QString target = QDir(dir).filePath(fileName);
    const QFileInfo srcInfo(srcPath);
    if (srcInfo.absoluteFilePath().compare(QFileInfo(target).absoluteFilePath(),
                                           Qt::CaseInsensitive) != 0) {
        if (QFile::exists(target) && !QFile::remove(target)) {
            if (err) *err = QStringLiteral("无法覆盖同名表格文件");
            return false;
        }
        if (!QFile::copy(srcPath, target)) {
            if (err) *err = QStringLiteral("复制表格到数据库文件夹失败：%1").arg(target);
            return false;
        }
    }

    QList<QStringList> rows;
    QString perr;
    if (!XlsReader::read(target, rows, &perr)) {
        if (err) *err = perr;
        return false;
    }

    // 表头按列名定位，兼容 ERP 导出的列顺序变化；前 20 行内找
    int header = -1;
    for (int i = 0; i < qMin(rows.size(), 20); ++i) {
        if (rows.at(i).contains(QStringLiteral("存货编号"))
            && (rows.at(i).contains(QStringLiteral("存货全名"))
                || rows.at(i).contains(QStringLiteral("存货型号")))) {
            header = i;
            break;
        }
    }
    if (header < 0) {
        if (err) *err = QStringLiteral("未识别到表头（需包含「存货编号」「存货全名」）");
        return false;
    }
    QHash<QString, int> idx;
    const QStringList &head = rows.at(header);
    for (int c = 0; c < head.size(); ++c)
        idx.insert(head.at(c).trimmed(), c);
    const auto col = [&idx](const QStringList &line, const QString &key) {
        const int c = idx.value(key, -1);
        return (c >= 0 && c < line.size()) ? line.at(c).trimmed() : QString();
    };

    const QString key = QFileInfo(fileName).completeBaseName();
    const QString label = stockBatchLabel(fileName);
    const QString now = QDateTime::currentDateTime().toString(
        QStringLiteral("yyyy-MM-dd HH:mm:ss"));

    if (!m_db.transaction()) {
        if (err) *err = m_db.lastError().text();
        return false;
    }
    QSqlQuery q(m_db);
    const auto fail = [&](const QString &why) {
        m_db.rollback();
        if (err) *err = why.isEmpty() ? q.lastError().text() : why;
        return false;
    };
    q.prepare(QStringLiteral("DELETE FROM stock_archive WHERE batch=?"));
    q.addBindValue(key);
    if (!q.exec()) return fail(QString());
    q.prepare(QStringLiteral(
        "INSERT INTO stock_archive(batch,ord,row_no,code,name,model,spec,origin,supplier,"
        "safe_qty,creator,created_at,modifier,modified_at,package,search) "
        "VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)"));
    int count = 0;
    for (int i = header + 1; i < rows.size(); ++i) {
        const QStringList &line = rows.at(i);
        QStringList c;
        c.reserve(13);
        c << col(line, QStringLiteral("行号"))
          << col(line, QStringLiteral("存货编号"))
          << col(line, QStringLiteral("存货全名"))
          << col(line, QStringLiteral("存货型号"))
          << col(line, QStringLiteral("存货规格"))
          << col(line, QStringLiteral("存货产地"))
          << col(line, QStringLiteral("缺省供应商"))
          << col(line, QStringLiteral("安全库存数量"))
          << col(line, QStringLiteral("建档人"))
          << col(line, QStringLiteral("建档时间"))
          << col(line, QStringLiteral("修改人"))
          << col(line, QStringLiteral("修改时间"));
        c << stockPackageOf(c.at(2), c.at(3));
        bool empty = true;
        for (const QString &v : c) {
            if (!v.isEmpty()) { empty = false; break; }
        }
        if (empty) continue;
        q.addBindValue(key);
        q.addBindValue(count);
        for (int k = 0; k < 12; ++k) q.addBindValue(c.at(k));
        q.addBindValue(c.at(12));
        q.addBindValue(stockRowText(c));
        if (!q.exec()) return fail(QString());
        ++count;
    }
    q.prepare(QStringLiteral(
        "INSERT OR REPLACE INTO stock_batch(key,file_name,label,rows,imported_at) "
        "VALUES(?,?,?,?,?)"));
    q.addBindValue(key);
    q.addBindValue(fileName);
    q.addBindValue(label);
    q.addBindValue(count);
    q.addBindValue(now);
    if (!q.exec()) return fail(QString());
    if (!m_db.commit()) return fail(QString());
    return true;
}

bool DatabaseManager::deleteStockBatch(const QString &key, QString *err)
{
    if (!isServer())
        return RemoteApiClient::instance().deleteStockBatch(key, err);
    if (key.isEmpty()) {
        if (err) *err = QStringLiteral("批次为空");
        return false;
    }
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("SELECT file_name FROM stock_batch WHERE key=?"));
    q.addBindValue(key);
    const QString fileName = (q.exec() && q.next()) ? q.value(0).toString() : QString();
    if (!m_db.transaction()) {
        if (err) *err = m_db.lastError().text();
        return false;
    }
    q.prepare(QStringLiteral("DELETE FROM stock_archive WHERE batch=?"));
    q.addBindValue(key);
    if (!q.exec()) { m_db.rollback(); if (err) *err = q.lastError().text(); return false; }
    q.prepare(QStringLiteral("DELETE FROM stock_batch WHERE key=?"));
    q.addBindValue(key);
    if (!q.exec()) { m_db.rollback(); if (err) *err = q.lastError().text(); return false; }
    if (!m_db.commit()) { if (err) *err = m_db.lastError().text(); return false; }
    if (!fileName.isEmpty()) {
        const QString path = QDir(dbDirectory()).filePath(fileName);
        if (QFile::exists(path)) QFile::remove(path);
    }
    return true;
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

QString DatabaseManager::currentOperatorRaw() const
{
    return m_currentOperator;
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
    // 客户端本机没有 op_log 表，所有账号的操作都由服务器统一记录，这里转发只读查询
    if (!isServer()) return RemoteApiClient::instance().opLogs(since);
    QList<OpLog> list;
    QSqlQuery q(m_db);
    // 只取最近 500 条：设置页逐行建表格项/按钮，全量日志会让切页明显卡顿
    q.prepare(QStringLiteral(
        "SELECT id,oper,action,model,name,detail,created_at FROM op_log"
        " WHERE created_at>=? ORDER BY id DESC LIMIT 500"));
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

// 旧版 mold_item 表字段较少，CREATE TABLE IF NOT EXISTS 不会改动已存在的表；
// 这里按 PRAGMA 补齐新增列，保证升级后不丢数据。
void DatabaseManager::ensureMoldColumns()
{
    QSqlQuery q(m_db);
    q.exec(QStringLiteral("PRAGMA table_info(mold_item)"));
    QStringList have;
    while (q.next())
        have << q.value(1).toString();
    const QList<QPair<QString, QString>> cols = {
        {QStringLiteral("machine_type"), QStringLiteral("TEXT")},
        {QStringLiteral("terminal_info"), QStringLiteral("TEXT")},
        {QStringLiteral("machine_no"), QStringLiteral("TEXT")},
        {QStringLiteral("seal_image"), QStringLiteral("TEXT")},
        {QStringLiteral("terminal_image"), QStringLiteral("TEXT")},
        {QStringLiteral("orientation"), QStringLiteral("TEXT")},
        {QStringLiteral("department"), QStringLiteral("TEXT")},
        {QStringLiteral("area"), QStringLiteral("TEXT")},
        {QStringLiteral("keeper"), QStringLiteral("TEXT")},
        {QStringLiteral("vendor"), QStringLiteral("TEXT")}};
    for (const auto &c : cols) {
        if (have.contains(c.first))
            continue;
        QSqlQuery a(m_db);
        a.exec(QStringLiteral("ALTER TABLE mold_item ADD COLUMN %1 %2").arg(c.first, c.second));
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

// ==================== 芯片查询（重要芯片台账） ====================
namespace {
ChipItem chipFromQuery(const QSqlQuery &q)
{
    ChipItem c;
    c.id       = q.value(0).toInt();
    c.hardware = q.value(1).toString();
    c.category = q.value(2).toString();
    c.model    = q.value(3).toString();
    c.qty      = q.value(4).toInt();
    c.function = q.value(5).toString();
    c.country  = q.value(6).toString();
    c.brand    = q.value(7).toString();
    c.remark   = q.value(8).toString();
    return c;
}
} // namespace

// 初始 Excel 把恩智浦（NXP）的产地误填成「德国」，实际它是荷兰公司。
// 已经灌进库的旧数据不会被 seedChipData() 重灌，所以这里单独做一次性订正；
// 用 app_meta 打标记，只跑一次，避免日后反复覆盖管理员手工修改的国家。
void DatabaseManager::fixChipBrandCountry()
{
    if (!m_db.isOpen())
        return;
    QSqlQuery q(m_db);
    if (q.exec(QStringLiteral("SELECT value FROM app_meta WHERE key='chip_nxp_country_fixed'"))
        && q.next() && q.value(0).toString() == QLatin1String("1"))
        return;
    const QString brand = QStringLiteral("恩智浦（NXP）");
    const QString country = QStringLiteral("荷兰");
    QSqlQuery up(m_db);
    up.prepare(QStringLiteral(
        "UPDATE chip_item SET country=?, updated_at=? "
        "WHERE brand=? AND (country IS NULL OR country<>?)"));
    up.addBindValue(country);
    up.addBindValue(QDateTime::currentDateTime().toString(Qt::ISODate));
    up.addBindValue(brand);
    up.addBindValue(country);
    if (!up.exec()) {
        qWarning() << "fix chip brand country failed:" << up.lastError().text();
        return;
    }
    q.exec(QStringLiteral(
        "INSERT OR REPLACE INTO app_meta(key,value) VALUES('chip_nxp_country_fixed','1')"));
}

// 表格只作初始数据来源：首次启动把 :/data/chip_seed.json 灌进来，
// 之后系统内的增删改就是唯一数据源，任何时候都不覆盖用户数据。
void DatabaseManager::seedChipData()
{
    if (!m_db.isOpen())
        return;
    fixChipBrandCountry(); // 旧库产地填错的先订正，再判断是否需要灌初始数据
    QSqlQuery q(m_db);
    if (q.exec(QStringLiteral("SELECT value FROM app_meta WHERE key='chip_seeded'"))
        && q.next() && q.value(0).toString() == QLatin1String("1"))
        return;
    if (q.exec(QStringLiteral("SELECT COUNT(*) FROM chip_item"))
        && q.next() && q.value(0).toInt() > 0) {
        q.exec(QStringLiteral(
            "INSERT OR REPLACE INTO app_meta(key,value) VALUES('chip_seeded','1')"));
        return;
    }
    QFile f(QStringLiteral(":/data/chip_seed.json"));
    if (!f.open(QIODevice::ReadOnly)) {
        qWarning() << "chip seed resource missing";
        return;
    }
    QJsonParseError pe;
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &pe);
    if (pe.error != QJsonParseError::NoError || !doc.isArray()) {
        qWarning() << "chip seed json invalid:" << pe.errorString();
        return;
    }
    const QString now = QDateTime::currentDateTime().toString(Qt::ISODate);
    m_db.transaction();
    QSqlQuery ins(m_db);
    ins.prepare(QStringLiteral(
        "INSERT INTO chip_item(hardware,category,model,qty,function,country,brand,remark,"
        "creator,created_at,updated_at) VALUES(?,?,?,?,?,?,?,?,?,?,?)"));
    for (const QJsonValue &v : doc.array()) {
        const QJsonObject o = v.toObject();
        ins.addBindValue(o.value(QStringLiteral("hardware")).toString());
        ins.addBindValue(o.value(QStringLiteral("type")).toString());
        ins.addBindValue(o.value(QStringLiteral("model")).toString());
        ins.addBindValue(o.value(QStringLiteral("qty")).toInt());
        ins.addBindValue(o.value(QStringLiteral("function")).toString());
        ins.addBindValue(o.value(QStringLiteral("country")).toString());
        ins.addBindValue(o.value(QStringLiteral("brand")).toString());
        ins.addBindValue(QString());                 // 备注预留为空
        ins.addBindValue(QStringLiteral("system"));  // 初始导入
        ins.addBindValue(now);
        ins.addBindValue(now);
        ins.exec();
    }
    m_db.commit();
    q.exec(QStringLiteral(
        "INSERT OR REPLACE INTO app_meta(key,value) VALUES('chip_seeded','1')"));
}

QStringList DatabaseManager::chipHardwares(QString *err) const
{
    if (err) err->clear();
    if (!isServer())
        return RemoteApiClient::instance().chipHardwares(err);
    QStringList out;
    QSqlQuery q(m_db);
    if (!q.exec(QStringLiteral(
            "SELECT DISTINCT hardware FROM chip_item WHERE hardware<>'' ORDER BY hardware"))) {
        if (err) *err = q.lastError().text();
        return out;
    }
    while (q.next())
        out << q.value(0).toString();
    return out;
}

QList<ChipItem> DatabaseManager::chipModelDict(QString *err) const
{
    if (err) err->clear();
    if (!isServer())
        return RemoteApiClient::instance().chipModelDict(err);
    QList<ChipItem> out;
    QStringList seen;
    QSqlQuery q(m_db);
    if (!q.exec(QStringLiteral(
            "SELECT id,hardware,category,model,qty,function,country,brand,remark "
            "FROM chip_item ORDER BY model, id"))) {
        if (err) *err = q.lastError().text();
        return out;
    }
    while (q.next()) {
        const QString key = q.value(3).toString().trimmed().toUpper();
        if (seen.contains(key))
            continue;
        seen.append(key);
        out.append(chipFromQuery(q));
    }
    return out;
}

QList<ChipItem> DatabaseManager::chipSearch(const QString &mode, const QString &keyword,
                                            int limit, int *total, QString *err) const
{
    if (total) *total = 0;
    if (err) err->clear();
    if (!isServer())
        return RemoteApiClient::instance().chipSearch(mode, keyword, limit, total, err);

    QList<ChipItem> out;
    const QString kw = keyword.trimmed();
    const bool byHardware = (mode != QLatin1String("model"));
    const QString col = byHardware ? QStringLiteral("hardware") : QStringLiteral("model");
    const QString where = kw.isEmpty()
        ? QString()
        : QStringLiteral(" WHERE instr(upper(%1), upper(?)) > 0").arg(col);

    QSqlQuery cnt(m_db);
    cnt.prepare(QStringLiteral("SELECT COUNT(*) FROM chip_item") + where);
    if (!kw.isEmpty())
        cnt.addBindValue(kw);
    if (cnt.exec() && cnt.next() && total)
        *total = cnt.value(0).toInt();

    QString sql = QStringLiteral(
        "SELECT id,hardware,category,model,qty,function,country,brand,remark "
        "FROM chip_item") + where
        + QStringLiteral(" ORDER BY %1, category, model, id").arg(col);
    if (limit > 0)
        sql += QStringLiteral(" LIMIT %1").arg(limit);
    QSqlQuery q(m_db);
    q.prepare(sql);
    if (!kw.isEmpty())
        q.addBindValue(kw);
    if (!q.exec()) {
        if (err) *err = q.lastError().text();
        return out;
    }
    while (q.next())
        out.append(chipFromQuery(q));
    return out;
}

bool DatabaseManager::chipSave(ChipItem *item, QString *err)
{
    if (err) err->clear();
    if (!item)
        return false;
    if (!isServer())
        return RemoteApiClient::instance().chipSave(item, err);
    if (item->hardware.trimmed().isEmpty() || item->model.trimmed().isEmpty()) {
        if (err) *err = QStringLiteral("硬件版本和型号不能为空");
        return false;
    }
    const bool isNew = (item->id <= 0);
    const QString now = QDateTime::currentDateTime().toString(Qt::ISODate);
    QSqlQuery q(m_db);
    bool ok = false;
    if (!isNew) {
        q.prepare(QStringLiteral(
            "UPDATE chip_item SET hardware=?,category=?,model=?,qty=?,function=?,"
            "country=?,brand=?,remark=?,updated_at=? WHERE id=?"));
        q.addBindValue(item->hardware);
        q.addBindValue(item->category);
        q.addBindValue(item->model);
        q.addBindValue(item->qty);
        q.addBindValue(item->function);
        q.addBindValue(item->country);
        q.addBindValue(item->brand);
        q.addBindValue(item->remark);
        q.addBindValue(now);
        q.addBindValue(item->id);
        ok = q.exec();
    } else {
        q.prepare(QStringLiteral(
            "INSERT INTO chip_item(hardware,category,model,qty,function,country,brand,remark,"
            "creator,created_at,updated_at) VALUES(?,?,?,?,?,?,?,?,?,?,?)"));
        q.addBindValue(item->hardware);
        q.addBindValue(item->category);
        q.addBindValue(item->model);
        q.addBindValue(item->qty);
        q.addBindValue(item->function);
        q.addBindValue(item->country);
        q.addBindValue(item->brand);
        q.addBindValue(item->remark);
        q.addBindValue(currentOperator());
        q.addBindValue(now);
        q.addBindValue(now);
        ok = q.exec();
        if (ok)
            item->id = q.lastInsertId().toInt();
    }
    if (!ok) {
        if (err) *err = q.lastError().text();
        return false;
    }
    addOpLog(isNew ? QStringLiteral("新增芯片") : QStringLiteral("修改芯片"),
             item->model, item->hardware,
             QStringLiteral("%1 · %2 颗").arg(item->model).arg(item->qty));
    return true;
}

bool DatabaseManager::chipDelete(int id, QString *err)
{
    if (err) err->clear();
    if (id <= 0)
        return false;
    if (!isServer())
        return RemoteApiClient::instance().chipDelete(id, err);
    QString oldModel, oldHardware;
    QSqlQuery g(m_db);
    g.prepare(QStringLiteral("SELECT model,hardware FROM chip_item WHERE id=?"));
    g.addBindValue(id);
    if (g.exec() && g.next()) {
        oldModel = g.value(0).toString();
        oldHardware = g.value(1).toString();
    }
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("DELETE FROM chip_item WHERE id=?"));
    q.addBindValue(id);
    if (!q.exec()) {
        if (err) *err = q.lastError().text();
        return false;
    }
    addOpLog(QStringLiteral("删除芯片"), oldModel, oldHardware,
             QStringLiteral("id=%1").arg(id));
    return true;
}

// ==================== 端子机模具台账 ====================
namespace {
MoldItem moldFromQuery(const QSqlQuery &q)
{
    // 列顺序：id,mold_code,type,machine_type,terminals,press_wire,terminal_info,
    // seal_info,seal_image,terminal_image,harness,orientation,department,area,
    // keeper,vendor,remark,status,creator,created_at,updated_at
    MoldItem m;
    m.id = q.value(0).toInt();
    m.moldCode = q.value(1).toString();
    m.type = q.value(2).toString();
    m.machineType = q.value(3).toString();
    m.terminals = q.value(4).toString();
    m.pressWire = q.value(5).toString();
    m.terminalInfo = q.value(6).toString();
    m.sealInfo = q.value(7).toString();
    m.machineNo = q.value(8).toString();
    m.sealImage = q.value(9).toString();
    m.terminalImage = q.value(10).toString();
    m.harness = q.value(11).toString();
    m.orientation = q.value(12).toString();
    m.department = q.value(13).toString();
    m.area = q.value(14).toString();
    m.keeper = q.value(15).toString();
    m.vendor = q.value(16).toString();
    m.remark = q.value(17).toString();
    m.status = q.value(18).toInt();
    m.creator = q.value(19).toString();
    m.createdAt = q.value(20).toString();
    m.updatedAt = q.value(21).toString();
    return m;
}
const QString kMoldColumns = QStringLiteral(
    "id,mold_code,type,machine_type,terminals,press_wire,terminal_info,seal_info,"
    "machine_no,seal_image,terminal_image,harness,orientation,department,area,keeper,"
    "vendor,remark,status,creator,created_at,updated_at");
} // namespace

QList<MoldItem> DatabaseManager::moldSearch(const QString &keyword, const QString &status,
                                            int limit, int *total, QString *err) const
{
    if (total) *total = 0;
    if (err) err->clear();
    if (!isServer())
        return RemoteApiClient::instance().moldSearch(keyword, status, limit, total, err);

    QList<MoldItem> out;
    const QString kw = keyword.trimmed();
    QStringList cond;
    QVariantList binds;
    if (!kw.isEmpty()) {
        cond << QStringLiteral("(instr(upper(coalesce(mold_code,'')),upper(?))>0"
                               " OR instr(upper(coalesce(terminals,'')),upper(?))>0"
                               " OR instr(upper(coalesce(harness,'')),upper(?))>0"
                               " OR instr(upper(coalesce(press_wire,'')),upper(?))>0"
                               " OR instr(upper(coalesce(terminal_info,'')),upper(?))>0"
                               " OR instr(upper(coalesce(keeper,'')),upper(?))>0)");
        binds << kw << kw << kw << kw << kw << kw;
    }
    if (status == QLatin1String("incomplete"))
        cond << QStringLiteral("status=0");
    else if (status == QLatin1String("complete"))
        cond << QStringLiteral("status=1");
    else if (status == QLatin1String("checked"))
        cond << QStringLiteral("status=2");
    const QString where = cond.isEmpty() ? QString() : QStringLiteral(" WHERE ") + cond.join(QStringLiteral(" AND "));

    QSqlQuery cnt(m_db);
    cnt.prepare(QStringLiteral("SELECT COUNT(*) FROM mold_item") + where);
    for (const QVariant &v : binds) cnt.addBindValue(v);
    if (cnt.exec() && cnt.next() && total) *total = cnt.value(0).toInt();

    QString sql = QStringLiteral("SELECT ") + kMoldColumns
        + QStringLiteral(" FROM mold_item") + where
        + QStringLiteral(" ORDER BY status, mold_code, id");
    if (limit > 0) sql += QStringLiteral(" LIMIT %1").arg(limit);
    QSqlQuery q(m_db);
    q.prepare(sql);
    for (const QVariant &v : binds) q.addBindValue(v);
    if (!q.exec()) {
        if (err) *err = q.lastError().text();
        return out;
    }
    while (q.next()) out.append(moldFromQuery(q));
    return out;
}

bool DatabaseManager::moldSave(MoldItem *item, QString *err)
{
    if (err) err->clear();
    if (!item) return false;
    if (!isServer()) return RemoteApiClient::instance().moldSave(item, err);
    const bool isNew = (item->id <= 0);
    const QString now = QDateTime::currentDateTime().toString(Qt::ISODate);
    QSqlQuery q(m_db);
    bool ok = false;
    if (!isNew) {
        q.prepare(QStringLiteral(
            "UPDATE mold_item SET mold_code=?,type=?,machine_type=?,terminals=?,press_wire=?,"
            "terminal_info=?,seal_info=?,machine_no=?,seal_image=?,terminal_image=?,harness=?,"
            "orientation=?,department=?,area=?,keeper=?,vendor=?,remark=?,status=?,updated_at=? "
            "WHERE id=?"));
    } else {
        q.prepare(QStringLiteral(
            "INSERT INTO mold_item(mold_code,type,machine_type,terminals,press_wire,terminal_info,"
            "seal_info,machine_no,seal_image,terminal_image,harness,orientation,department,area,"
            "keeper,vendor,remark,status,creator,created_at,updated_at) "
            "VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)"));
    }
    q.addBindValue(item->moldCode);
    q.addBindValue(item->type);
    q.addBindValue(item->machineType);
    q.addBindValue(item->terminals);
    q.addBindValue(item->pressWire);
    q.addBindValue(item->terminalInfo);
    q.addBindValue(item->sealInfo);
    q.addBindValue(item->machineNo);
    q.addBindValue(item->sealImage);
    q.addBindValue(item->terminalImage);
    q.addBindValue(item->harness);
    q.addBindValue(item->orientation);
    q.addBindValue(item->department);
    q.addBindValue(item->area);
    q.addBindValue(item->keeper);
    q.addBindValue(item->vendor);
    q.addBindValue(item->remark);
    q.addBindValue(item->status);
    if (isNew) {
        q.addBindValue(currentOperator());
        q.addBindValue(now);
        q.addBindValue(now);
    } else {
        q.addBindValue(now);
        q.addBindValue(item->id);
    }
    ok = q.exec();
    if (ok && isNew) item->id = q.lastInsertId().toInt();
    if (!ok) {
        if (err) *err = q.lastError().text();
        return false;
    }
    addOpLog(isNew ? QStringLiteral("新增模具") : QStringLiteral("修改模具"),
             item->moldCode.isEmpty() ? item->terminals : item->moldCode,
             item->machineType, item->terminals);
    return true;
}

bool DatabaseManager::moldDelete(int id, QString *err)
{
    if (err) err->clear();
    if (id <= 0) return false;
    if (!isServer()) return RemoteApiClient::instance().moldDelete(id, err);
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("DELETE FROM mold_item WHERE id=?"));
    q.addBindValue(id);
    if (!q.exec()) {
        if (err) *err = q.lastError().text();
        return false;
    }
    addOpLog(QStringLiteral("删除模具"), QString(), QString(), QStringLiteral("id=%1").arg(id));
    return true;
}

// ==================== 线束下线 ====================
namespace {
CutItem cutItemFromQuery(const QSqlQuery &q)
{
    CutItem it;
    it.id = q.value(0).toInt();
    it.batchId = q.value(1).toInt();
    it.seq = q.value(2).toInt();
    it.circuit = q.value(3).toString();
    it.material = q.value(4).toString();
    it.section = q.value(5).toString();
    it.color = q.value(6).toString();
    it.wireLength = q.value(7).toString();
    it.stripLeft = q.value(8).toString();
    it.stripRight = q.value(9).toString();
    it.terminalLeft = q.value(10).toString();
    it.terminalRight = q.value(11).toString();
    it.workLeft = q.value(12).toString();
    it.workRight = q.value(13).toString();
    return it;
}
CutTask cutTaskFromQuery(const QSqlQuery &q)
{
    CutTask t;
    t.id = q.value(0).toInt();
    t.batchId = q.value(1).toInt();
    t.itemId = q.value(2).toInt();
    t.machineId = q.value(3).toInt();
    t.quantity = q.value(4).toInt();
    t.doneQty = q.value(5).toInt();
    t.status = q.value(6).toInt();
    t.operName = q.value(7).toString();
    t.createdAt = q.value(8).toString();
    t.finishedAt = q.value(9).toString();
    t.seq = q.value(10).toInt();
    t.circuit = q.value(11).toString();
    t.color = q.value(12).toString();
    t.wireLength = q.value(13).toString();
    t.section = q.value(14).toString();
    t.workLeft = q.value(15).toString();
    t.workRight = q.value(16).toString();
    t.batchCreatedAt = q.value(17).toString();
    t.material = q.value(18).toString();
    t.stripLeft = q.value(19).toString();
    t.stripRight = q.value(20).toString();
    t.terminalLeft = q.value(21).toString();
    t.terminalRight = q.value(22).toString();
    t.batchName = q.value(23).toString();
    t.batchNo = q.value(24).toString();
    return t;
}
} // namespace

QList<CutBatch> DatabaseManager::cutBatches(QString *err) const
{
    if (err) err->clear();
    if (!isServer()) return RemoteApiClient::instance().cutBatches(err);
    QList<CutBatch> out;
    QSqlQuery q(m_db);
    if (!q.exec(QStringLiteral("SELECT id,name,file_name,quantity,total_rows,creator,created_at,batch_no "
                               "FROM cut_harness ORDER BY id DESC"))) {
        if (err) *err = q.lastError().text();
        return out;
    }
    while (q.next()) {
        CutBatch b;
        b.id = q.value(0).toInt();
        b.name = q.value(1).toString();
        b.fileName = q.value(2).toString();
        b.quantity = q.value(3).toString();
        b.totalRows = q.value(4).toInt();
        b.creator = q.value(5).toString();
        b.createdAt = q.value(6).toString();
        b.batchNo = q.value(7).toString();
        out.append(b);
    }
    return out;
}

QList<CutItem> DatabaseManager::cutItems(int batchId, QString *err) const
{
    if (err) err->clear();
    if (!isServer()) return RemoteApiClient::instance().cutItems(batchId, err);
    QList<CutItem> out;
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("SELECT id,batch_id,seq,circuit,material,section,color,wire_length,"
                             "strip_left,strip_right,terminal_left,terminal_right,work_left,work_right "
                             "FROM cut_item WHERE batch_id=? ORDER BY seq,id"));
    q.addBindValue(batchId);
    if (!q.exec()) {
        if (err) *err = q.lastError().text();
        return out;
    }
    while (q.next()) out.append(cutItemFromQuery(q));
    return out;
}

int DatabaseManager::cutImportBatch(const CutBatch &batch, const QList<CutItem> &items, QString *err)
{
    if (err) err->clear();
    if (!isServer()) {
        int id = RemoteApiClient::instance().cutImportBatch(batch, items, err);
        return id;
    }
    const QString now = QDateTime::currentDateTime().toString(Qt::ISODate);
    m_db.transaction();
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("INSERT INTO cut_harness(name,batch_no,file_name,quantity,total_rows,creator,created_at) "
                             "VALUES(?,?,?,?,?,?,?)"));
    q.addBindValue(batch.name);
    q.addBindValue(batch.batchNo.isEmpty() ? batch.name : batch.batchNo);
    q.addBindValue(batch.fileName);
    q.addBindValue(batch.quantity);
    q.addBindValue(items.size());
    q.addBindValue(batch.creator.isEmpty() ? currentOperator() : batch.creator);
    q.addBindValue(now);
    if (!q.exec()) {
        if (err) *err = q.lastError().text();
        m_db.rollback();
        return -1;
    }
    const int batchId = q.lastInsertId().toInt();
    QSqlQuery ins(m_db);
    ins.prepare(QStringLiteral("INSERT INTO cut_item(batch_id,seq,circuit,material,section,color,wire_length,"
                               "strip_left,strip_right,terminal_left,terminal_right,work_left,work_right,"
                               "created_at) VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?)"));
    for (const CutItem &it : items) {
        ins.addBindValue(batchId);
        ins.addBindValue(it.seq);
        ins.addBindValue(it.circuit);
        ins.addBindValue(it.material);
        ins.addBindValue(it.section);
        ins.addBindValue(it.color);
        ins.addBindValue(it.wireLength);
        ins.addBindValue(it.stripLeft);
        ins.addBindValue(it.stripRight);
        ins.addBindValue(it.terminalLeft);
        ins.addBindValue(it.terminalRight);
        ins.addBindValue(it.workLeft);
        ins.addBindValue(it.workRight);
        ins.addBindValue(now);
        if (!ins.exec()) {
            if (err) *err = ins.lastError().text();
            m_db.rollback();
            return -1;
        }
    }
    m_db.commit();
    addOpLog(QStringLiteral("导入下线表"), batch.name, batch.fileName,
             QStringLiteral("%1 条明细").arg(items.size()));
    return batchId;
}

bool DatabaseManager::cutDeleteBatch(int batchId, QString *err)
{
    if (err) err->clear();
    // ID 非法时直接失败：否则三条 DELETE 都是 0 行，却会被当成「删除成功」
    if (batchId <= 0) {
        if (err) *err = QStringLiteral("线束 ID 无效，请刷新列表后重新选择要删除的线束。");
        return false;
    }
    if (!isServer()) return RemoteApiClient::instance().cutDeleteBatch(batchId, err);
    m_db.transaction();
    QSqlQuery q(m_db);
    bool ok = true;
    q.prepare(QStringLiteral("DELETE FROM cut_task WHERE batch_id=?")); q.addBindValue(batchId);
    ok &= q.exec();
    q.prepare(QStringLiteral("DELETE FROM cut_item WHERE batch_id=?")); q.addBindValue(batchId);
    ok &= q.exec();
    q.prepare(QStringLiteral("DELETE FROM cut_harness WHERE id=?")); q.addBindValue(batchId);
    ok &= q.exec();
    const int harnessDeleted = ok ? q.numRowsAffected() : 0;
    if (!ok) {
        if (err) *err = q.lastError().text();
        m_db.rollback();
        return false;
    }
    // 线束主记录必须真的被删掉，否则不能报成功（防止「删除完成」但记录还在）
    if (harnessDeleted <= 0) {
        m_db.rollback();
        if (err) *err = QStringLiteral("线束不存在或已被删除，请刷新列表后重试。");
        return false;
    }
    m_db.commit();
    // 最后一条线束被删除后重置自增序列，下次导入从 ID=1 重新开始
    {
        QSqlQuery seq(m_db);
        if (seq.exec(QStringLiteral("SELECT COUNT(*) FROM cut_harness")) && seq.next()
            && seq.value(0).toInt() == 0) {
            const QStringList tables = { QStringLiteral("cut_harness"), QStringLiteral("cut_item"),
                                         QStringLiteral("cut_task"), QStringLiteral("cut_print_log") };
            for (const QString &table : tables)
                seq.exec(QStringLiteral("DELETE FROM sqlite_sequence WHERE name='%1'").arg(table));
        }
    }
    return true;
}

QList<CutMachine> DatabaseManager::cutMachines(QString *err) const
{
    if (err) err->clear();
    if (!isServer()) return RemoteApiClient::instance().cutMachines(err);
    QList<CutMachine> out;
    QSqlQuery q(m_db);
    if (!q.exec(QStringLiteral("SELECT id,code,name,capabilities,active FROM cut_machine ORDER BY id"))) {
        if (err) *err = q.lastError().text();
        return out;
    }
    while (q.next()) {
        CutMachine m;
        m.id = q.value(0).toInt();
        m.code = q.value(1).toString();
        m.name = q.value(2).toString();
        m.capabilities = q.value(3).toString();
        m.active = q.value(4).toInt();
        out.append(m);
    }
    return out;
}

bool DatabaseManager::cutSaveMachine(CutMachine *m, QString *err)
{
    if (err) err->clear();
    if (!m) return false;
    if (!isServer()) return RemoteApiClient::instance().cutSaveMachine(m, err);
    QSqlQuery q(m_db);
    if (m->id > 0) {
        q.prepare(QStringLiteral("UPDATE cut_machine SET code=?,name=?,capabilities=?,active=? WHERE id=?"));
        q.addBindValue(m->code); q.addBindValue(m->name); q.addBindValue(m->capabilities);
        q.addBindValue(m->active); q.addBindValue(m->id);
    } else {
        q.prepare(QStringLiteral("INSERT INTO cut_machine(code,name,capabilities,active) VALUES(?,?,?,?)"));
        q.addBindValue(m->code); q.addBindValue(m->name); q.addBindValue(m->capabilities);
        q.addBindValue(m->active);
    }
    if (!q.exec()) {
        if (err) *err = q.lastError().text();
        return false;
    }
    if (m->id <= 0) m->id = q.lastInsertId().toInt();
    return true;
}

QList<CutRule> DatabaseManager::cutRules(QString *err) const
{
    if (err) err->clear();
    if (!isServer()) return RemoteApiClient::instance().cutRules(err);
    QList<CutRule> out;
    QSqlQuery q(m_db);
    if (!q.exec(QStringLiteral("SELECT id,field,match_type,match_value,target_machine,enabled "
                               "FROM cut_rule ORDER BY id"))) {
        if (err) *err = q.lastError().text();
        return out;
    }
    while (q.next()) {
        CutRule r;
        r.id = q.value(0).toInt();
        r.field = q.value(1).toString();
        r.matchType = q.value(2).toString();
        r.matchValue = q.value(3).toString();
        r.machineId = q.value(4).toString().toInt();
        r.enabled = q.value(5).toInt();
        out.append(r);
    }
    // 命中多条时：专用件（端子/防水栓）优先于线径范围
    std::stable_sort(out.begin(), out.end(), [](const CutRule &a, const CutRule &b) {
        return cutRuleSpecificity(a) < cutRuleSpecificity(b);
    });
    return out;
}

bool DatabaseManager::cutSaveRule(CutRule *r, QString *err)
{
    if (err) err->clear();
    if (!r) return false;
    if (!isServer()) return RemoteApiClient::instance().cutSaveRule(r, err);
    if (r->machineId <= 0) {
        if (err) *err = QStringLiteral("请选择目标机台");
        return false;
    }
    if (r->field.isEmpty()) r->field = QStringLiteral("section");
    if (r->isRange()) {
        // 线径规则：匹配值统一存成 "最小-最大"
        double lo = 0.0, hi = 0.0;
        if (!cutRuleParseRange(r->matchValue, &lo, &hi)) {
            if (err) *err = QStringLiteral("线径范围格式不对，应形如 0.5-1.5");
            return false;
        }
        r->matchType = QStringLiteral("range");
        r->matchValue = QStringLiteral("%1-%2")
            .arg(QString::number(lo, 'g', 10), QString::number(hi, 'g', 10));
    } else {
        if (r->matchValue.trimmed().isEmpty()) {
            if (err) *err = QStringLiteral("请填写匹配值（如 PP2011101）");
            return false;
        }
        r->matchValue = r->matchValue.trimmed();
        if (r->matchType.isEmpty()) r->matchType = QStringLiteral("contains");
    }
    // 去重：同字段 + 同匹配方式 + 同匹配值（忽略大小写/首尾空格）+ 同目标机台视为重复
    {
        QSqlQuery dup(m_db);
        dup.prepare(QStringLiteral(
            "SELECT id FROM cut_rule WHERE field=? AND match_type=? "
            "AND LOWER(TRIM(match_value))=LOWER(TRIM(?)) "
            "AND target_machine=? AND id<>? LIMIT 1"));
        dup.addBindValue(r->field);
        dup.addBindValue(r->matchType);
        dup.addBindValue(r->matchValue);
        dup.addBindValue(QString::number(r->machineId));
        dup.addBindValue(r->id);
        if (dup.exec() && dup.next()) {
            if (err) {
                *err = QStringLiteral("已存在相同规则：%1「%2」→ 同一台机台（规则 #%3）。\n"
                                      "请直接修改原规则，或换一个匹配值。")
                           .arg(CutRule::fieldText(r->field), r->matchValue)
                           .arg(dup.value(0).toInt());
            }
            return false;
        }
    }
    QSqlQuery q(m_db);
    if (r->id > 0) {
        q.prepare(QStringLiteral("UPDATE cut_rule SET field=?,match_type=?,match_value=?,"
                                 "target_machine=?,enabled=? WHERE id=?"));
    } else {
        q.prepare(QStringLiteral("INSERT INTO cut_rule(field,match_type,match_value,"
                                 "target_machine,enabled) VALUES(?,?,?,?,?)"));
    }
    q.addBindValue(r->field);
    q.addBindValue(r->matchType);
    q.addBindValue(r->matchValue);
    q.addBindValue(QString::number(r->machineId));
    q.addBindValue(r->enabled);
    if (r->id > 0) q.addBindValue(r->id);
    if (!q.exec()) {
        if (err) *err = q.lastError().text();
        return false;
    }
    if (r->id <= 0) r->id = q.lastInsertId().toInt();
    addOpLog(QStringLiteral("保存派单规则"), CutRule::fieldText(r->field), r->valueText(),
             QStringLiteral("机台%1").arg(r->machineId));
    return true;
}

bool DatabaseManager::cutDeleteRule(int id, QString *err)
{
    if (err) err->clear();
    if (!isServer()) return RemoteApiClient::instance().cutDeleteRule(id, err);
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("DELETE FROM cut_rule WHERE id=?"));
    q.addBindValue(id);
    if (!q.exec()) {
        if (err) *err = q.lastError().text();
        return false;
    }
    return true;
}

bool DatabaseManager::cutDispatch(int batchId, int machineId, const QList<int> &itemIds,
                                  int quantity, QString *err)
{
    if (err) err->clear();
    if (!isServer())
        return RemoteApiClient::instance().cutDispatch(batchId, machineId, itemIds, quantity, err);
    if (machineId <= 0) {
        if (err) *err = QStringLiteral("请选择目标机台");
        return false;
    }
    const QString now = QDateTime::currentDateTime().toString(Qt::ISODate);
    m_db.transaction();
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("INSERT INTO cut_task(batch_id,item_id,machine_id,quantity,done_qty,status,created_at) "
                             "VALUES(?,?,?,?,0,1,?)"));
    for (int itemId : itemIds) {
        q.addBindValue(batchId);
        q.addBindValue(itemId);
        q.addBindValue(machineId);
        q.addBindValue(quantity > 0 ? quantity : 1);
        q.addBindValue(now);
        if (!q.exec()) {
            if (err) *err = q.lastError().text();
            m_db.rollback();
            return false;
        }
    }
    m_db.commit();
    addOpLog(QStringLiteral("派工"), QString(), QString(),
             QStringLiteral("批次%1 → 机台%2，%3 条").arg(batchId).arg(machineId).arg(itemIds.size()));
    return true;
}

// 智能分配的公共计算部分：规则/Excel 指定机台 + 线径线色归组。
// 只算出「明细 → 机台」建议，不写库；预览与正式派工共用同一套逻辑。
bool DatabaseManager::cutComputeAssignments(int batchId, QList<CutAssign> *out,
                                            int *fixedCount, int *balancedCount, QString *err) const
{
    if (out) out->clear();
    if (fixedCount) *fixedCount = 0;
    if (balancedCount) *balancedCount = 0;
    const QList<CutMachine> machines = cutMachines(err);
    QList<CutMachine> active;
    int otherMachineId = 0;   // 「其他」= 人工/手动下线，未匹配线径规则的任务直接归它
    for (const CutMachine &m : machines) {
        if (m.code == QLatin1String("OTHER")) { otherMachineId = m.id; continue; }
        if (m.active) active.append(m);
    }
    if (active.isEmpty()) {
        if (err) *err = QStringLiteral("没有可用的下线机");
        return false;
    }
    // 已派工数量（用于均衡）：统计本批次各机器现有任务数
    QMap<int, int> load;
    for (const CutMachine &m : active) load[m.id] = 0;
    {
        QSqlQuery q(m_db);
        q.prepare(QStringLiteral("SELECT machine_id,COUNT(*) FROM cut_task WHERE batch_id=? GROUP BY machine_id"));
        q.addBindValue(batchId);
        if (q.exec())
            while (q.next()) load[q.value(0).toInt()] += q.value(1).toInt();
    }

    const QList<CutItem> allItems = cutItems(batchId, err);
    // 只处理尚未派工的明细
    QSet<int> dispatched;
    {
        QSqlQuery q(m_db);
        q.prepare(QStringLiteral("SELECT DISTINCT item_id FROM cut_task WHERE batch_id=?"));
        q.addBindValue(batchId);
        if (q.exec())
            while (q.next()) dispatched.insert(q.value(0).toInt());
    }

    struct Assign { int itemId; int machineId; };
    QList<Assign> assigns;
    QSet<int> assignedItems;
    QHash<int, int> itemMachine;   // itemId → 已确定机台，供材料归组参照
    int nFixed = 0;

    // 派单规则分两类：
    //  ① 专用件（防水栓/端子/颜色/材质）——硬约束，命中即锁定机台；
    //  ② 线径范围——只表示「该机台适合这个线径」，是候选机台而非锁定。
    // 这样避免大量明细被线径规则一次性锁到同一台机，剩余任务再按候选机台均衡。
    QList<CutRule> rules = cutRules(err);
    QList<CutRule> hardRules;
    QList<CutRule> sectionRules;
    auto machineActive = [&active](int mid) {
        for (const CutMachine &m : active) if (m.id == mid) return true;
        return false;
    };
    // 「其他」是人工/手动机台，不参与均衡，但允许作为硬规则目标（如屏蔽件）
    auto ruleTargetValid = [&active, otherMachineId](int mid) {
        if (mid == otherMachineId) return true;
        for (const CutMachine &m : active) if (m.id == mid) return true;
        return false;
    };
    for (const CutRule &r : rules) {
        if (!r.enabled || r.machineId <= 0 || !ruleTargetValid(r.machineId)) continue;
        if (r.isRange()) sectionRules.append(r);
        else hardRules.append(r);
    }
    QList<int> activeIds;
    for (const CutMachine &m : active) activeIds.append(m.id);
    QHash<int, CutItem> itemById;
    for (const CutItem &it : allItems) itemById.insert(it.id, it);

    auto ruleMatch = [&hardRules](const CutItem &it) -> int {
        for (const CutRule &r : hardRules)
            if (cutRuleMatches(r, it)) return r.machineId;
        return 0;
    };
    // 某条明细适合的机台：命中所有线径规则的并集。
    // 没有任何线径规则覆盖时返回空列表——该材料保持未分配，等人工指定机台。
    auto sectionCandidates = [&sectionRules](const CutItem &it) -> QList<int> {
        QList<int> opts;
        for (const CutRule &r : sectionRules)
            if (cutRuleMatches(r, it) && !opts.contains(r.machineId)) opts.append(r.machineId);
        return opts;
    };

    // 第一遍：屏蔽件（端子/防水栓/材质含「屏蔽」）→「其他」人工下线，
    // 五台自动机都不接这类工艺，优先于任何用户规则。
    if (otherMachineId > 0) {
        for (const CutItem &it : allItems) {
            if (dispatched.contains(it.id) || !cutItemHasShield(it)) continue;
            assigns.append({it.id, otherMachineId});
            assignedItems.insert(it.id);
            itemMachine.insert(it.id, otherMachineId);
            ++nFixed;
        }
    }
    // 第二遍：防水栓/端子等硬约束规则
    for (const CutItem &it : allItems) {
        if (dispatched.contains(it.id) || assignedItems.contains(it.id)) continue;
        const int mid = ruleMatch(it);
        if (mid > 0) {
            assigns.append({it.id, mid});
            load[mid] += 1;
            assignedItems.insert(it.id);
            itemMachine.insert(it.id, mid);
            ++nFixed;
        }
    }
    // 第三遍：不再使用下线表 Excel 的「机台」列。
    // 该列只是随表带过来的历史填写，实测与派单规则并不一致
    // （如 828904-1 在 Excel 里写 1，但没有任何规则要求它上一号机），
    // 当作硬约束会让某一台机台任务量暴涨。机台一律由规则 + 线径能力 + 均衡决定。
    // 第四遍：剩余任务按「线径 + 线色」归组（同组＝同一种材料），
    // 同组明细整体派给同一台机台，避免同材料在多台机器间来回换线；
    // 每组先由线径规则筛出适合机台，再挑当前负载最小的，尽量让各机台任务量接近。
    int nBalanced = 0;
    // 已被规则分配到机台的材料组，其剩余明细优先跟随同一机台。
    // 「其他」（人工/手动下线）不参与跟随，否则同材料里有一条屏蔽件/人工件
    // 就会把整组正常线径的明细一起拽到人工线。
    QHash<QString, int> groupPreferred;
    for (const CutItem &it : allItems) {
        const QHash<int, int>::const_iterator mit = itemMachine.constFind(it.id);
        if (mit == itemMachine.constEnd()) continue;
        if (otherMachineId > 0 && mit.value() == otherMachineId) continue;
        const QString key = cutMaterialKey(it);
        QHash<QString, int>::iterator pit = groupPreferred.find(key);
        if (pit == groupPreferred.end())
            groupPreferred.insert(key, mit.value());
        else if (load.value(mit.value(), 0) < load.value(pit.value(), 0))
            pit.value() = mit.value();
    }

    QStringList groupOrder;
    QHash<QString, QList<int>> groups;
    for (const CutItem &it : allItems) {
        if (dispatched.contains(it.id) || assignedItems.contains(it.id)) continue;
        const QString key = cutMaterialKey(it);
        if (!groups.contains(key)) {
            groups.insert(key, QList<int>());
            groupOrder << key;
        }
        groups[key].append(it.id);
    }
    // 条数多的材料组先占机台，尽量少换线
    std::stable_sort(groupOrder.begin(), groupOrder.end(),
                     [&groups](const QString &a, const QString &b) {
                         return groups.value(a).size() > groups.value(b).size();
                     });
    for (const QString &key : groupOrder) {
        const QList<int> ids = groups.value(key);
        int target = groupPreferred.value(key, 0);   // ① 同材料已被硬约束到某机台 → 跟随
        // 0 与「其他」都视为「还没定」：必须按线径规则重新找自动机台
        if (!machineActive(target)) {
            QList<int> opts = ids.isEmpty() ? activeIds
                                            : sectionCandidates(itemById.value(ids.first()));
            if (opts.isEmpty()) {   // 无线径规则覆盖：归「其他」机台（人工/手动下线）
                for (int id : ids) assigns.append({id, otherMachineId});
                continue;
            }
            target = opts.first();
            int minLoad = load.value(target, 0);
            for (int mid : opts) {                   // ②③ 适合的机台里挑当前最闲的
                const int l = load.value(mid, 0);
                if (l < minLoad) { minLoad = l; target = mid; }
            }
        }
        for (int id : ids) assigns.append({id, target});
        load[target] += ids.size();
        nBalanced += ids.size();
    }

    if (assigns.isEmpty()) {
        if (err) *err = QStringLiteral("没有需要分配的任务（可能已全部派工）");
        return false;
    }
    // 输出前按「截面数值 + 线色」排序：同一种材料连续显示，且 2.5 < 3 < 3.5 < 4 < 35
    // （不能按字符串排，否则 35 会落到 3 之前）
    std::stable_sort(assigns.begin(), assigns.end(), [&itemById](const Assign &a, const Assign &b) {
        const CutItem ia = itemById.value(a.itemId);
        const CutItem ib = itemById.value(b.itemId);
        const double sa = cutSectionNumber(ia.section);
        const double sb = cutSectionNumber(ib.section);
        if (sa >= 0.0 && sb >= 0.0 && qAbs(sa - sb) > 1e-9) return sa < sb;
        const int colorCmp = ia.color.trimmed().compare(ib.color.trimmed(), Qt::CaseInsensitive);
        if (colorCmp != 0) return colorCmp < 0;
        return cutMaterialKey(ia) < cutMaterialKey(ib);
    });
    for (const Assign &a : assigns) out->append({a.itemId, a.machineId});
    if (fixedCount) *fixedCount = nFixed;
    if (balancedCount) *balancedCount = nBalanced;
    return true;
}

// 保留原有「智能分配并直接派工」接口（供其它调用方兼容）
bool DatabaseManager::cutAutoDispatch(int batchId, int quantity, int *fixedCount,
                                      int *balancedCount, QString *err)
{
    if (err) err->clear();
    if (!isServer())
        return RemoteApiClient::instance().cutAutoDispatch(batchId, quantity, fixedCount,
                                                           balancedCount, err);
    QList<CutAssign> assigns;
    if (!cutComputeAssignments(batchId, &assigns, fixedCount, balancedCount, err))
        return false;
    const int nF = fixedCount ? *fixedCount : 0;
    const int nB = balancedCount ? *balancedCount : 0;
    const QString now = QDateTime::currentDateTime().toString(Qt::ISODate);
    m_db.transaction();
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("INSERT INTO cut_task(batch_id,item_id,machine_id,quantity,done_qty,status,created_at) "
                             "VALUES(?,?,?,?,0,1,?)"));
    for (const CutAssign &a : assigns) {
        if (a.machineId <= 0) continue;   // 未分配机台的行不写任务，留给人工指定
        q.addBindValue(batchId);
        q.addBindValue(a.itemId);
        q.addBindValue(a.machineId);
        q.addBindValue(quantity > 0 ? quantity : 1);
        q.addBindValue(now);
        if (!q.exec()) {
            if (err) *err = q.lastError().text();
            m_db.rollback();
            return false;
        }
    }
    m_db.commit();
    addOpLog(QStringLiteral("智能派工"), QString(), QString(),
             QStringLiteral("批次%1：固定%2 条 + 均衡%3 条").arg(batchId).arg(nF).arg(nB));
    return true;
}

// 智能分配预览：只返回建议机台（以虚拟任务行形式），不写数据库
bool DatabaseManager::cutAutoDispatchPreview(int batchId, QList<CutTask> *tasks,
                                             int *fixedCount, int *balancedCount, QString *err) const
{
    if (err) err->clear();
    if (tasks) tasks->clear();
    if (!isServer())
        return RemoteApiClient::instance().cutAutoDispatchPreview(batchId, tasks, fixedCount,
                                                                  balancedCount, err);
    QList<CutAssign> assigns;
    if (!cutComputeAssignments(batchId, &assigns, fixedCount, balancedCount, err))
        return false;
    QHash<int, CutItem> itemsById;
    for (const CutItem &it : cutItems(batchId, err)) itemsById.insert(it.id, it);
    QString batchCreatedAt;
    for (const CutBatch &b : cutBatches(err))
        if (b.id == batchId) { batchCreatedAt = b.createdAt; break; }
    for (const CutAssign &a : assigns) {
        const CutItem it = itemsById.value(a.itemId);
        if (it.id <= 0) continue;
        CutTask t;
        t.id = 0;                 // 0 = 尚未派工（预览行）
        t.batchId = batchId;
        t.itemId = it.id;
        t.machineId = a.machineId;
        t.quantity = 1;
        t.status = 0;
        t.seq = it.seq;
        t.circuit = it.circuit;
        t.color = it.color;
        t.wireLength = it.wireLength;
        t.section = it.section;
        t.workLeft = it.workLeft;
        t.workRight = it.workRight;
        t.batchCreatedAt = batchCreatedAt;
        if (tasks) tasks->append(t);
    }
    return true;
}

// 确认派工：把预览中（可能已被用户调整机台的）建议正式写成任务
bool DatabaseManager::cutCommitDispatch(int batchId, const QList<CutAssign> &assigns,
                                        int quantity, QString *err)
{
    if (err) err->clear();
    if (!isServer())
        return RemoteApiClient::instance().cutCommitDispatch(batchId, assigns, quantity, err);
    if (assigns.isEmpty()) {
        if (err) *err = QStringLiteral("没有需要派工的任务");
        return false;
    }
    const QString now = QDateTime::currentDateTime().toString(Qt::ISODate);
    m_db.transaction();
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("INSERT INTO cut_task(batch_id,item_id,machine_id,quantity,done_qty,status,created_at) "
                             "VALUES(?,?,?,?,0,1,?)"));
    for (const CutAssign &a : assigns) {
        if (a.machineId <= 0) continue;   // 未分配机台的行不写任务，留给人工指定
        q.addBindValue(batchId);
        q.addBindValue(a.itemId);
        q.addBindValue(a.machineId);
        q.addBindValue(quantity > 0 ? quantity : 1);
        q.addBindValue(now);
        if (!q.exec()) {
            if (err) *err = q.lastError().text();
            m_db.rollback();
            return false;
        }
    }
    m_db.commit();
    int committed = 0;
    for (const CutAssign &a : assigns) if (a.machineId > 0) ++committed;
    addOpLog(QStringLiteral("确认派工"), QString(), QString(),
             QStringLiteral("批次%1：正式派工 %2 条").arg(batchId).arg(committed));
    return true;
}

// 待生产任务改派机台：只改 machine_id，不动批次/明细/数量/状态
bool DatabaseManager::cutRetask(int taskId, int machineId, QString *err)
{
    if (err) err->clear();
    if (!isServer()) return RemoteApiClient::instance().cutRetask(taskId, machineId, err);
    if (taskId <= 0 || machineId <= 0) {
        if (err) *err = QStringLiteral("任务或目标机台无效");
        return false;
    }
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("UPDATE cut_task SET machine_id=? WHERE id=? AND status=1"));
    q.addBindValue(machineId);
    q.addBindValue(taskId);
    if (!q.exec()) {
        if (err) *err = q.lastError().text();
        return false;
    }
    if (q.numRowsAffected() <= 0) {
        if (err) *err = QStringLiteral("任务不存在，或已不在「待生产」状态（只有待生产任务可改派）");
        return false;
    }
    addOpLog(QStringLiteral("任务改派"), QString::number(taskId), QString(),
             QStringLiteral("改派到机台%1").arg(machineId));
    return true;
}

QList<CutTask> DatabaseManager::cutTasks(int batchId, int machineId, const QString &status,
                                         QString *err) const
{
    if (err) err->clear();
    if (!isServer()) return RemoteApiClient::instance().cutTasks(batchId, machineId, status, err);
    QList<CutTask> out;
    QStringList cond;
    QVariantList binds;
    if (batchId > 0) { cond << QStringLiteral("t.batch_id=?"); binds << batchId; }
    if (machineId > 0) { cond << QStringLiteral("t.machine_id=?"); binds << machineId; }
    if (status == QLatin1String("pending")) cond << QStringLiteral("t.status=0");
    else if (status == QLatin1String("assigned")) cond << QStringLiteral("t.status=1");
    else if (status == QLatin1String("doing")) cond << QStringLiteral("t.status=2");
    else if (status == QLatin1String("done")) cond << QStringLiteral("t.status=3");
    const QString where = cond.isEmpty() ? QString() : QStringLiteral(" WHERE ") + cond.join(QStringLiteral(" AND "));
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("SELECT t.id,t.batch_id,t.item_id,t.machine_id,t.quantity,t.done_qty,"
                             "t.status,t.operator,t.created_at,t.finished_at,"
                             "i.seq,i.circuit,i.color,i.wire_length,i.section,"
                             "i.work_left,i.work_right,b.created_at,"
                             "i.material,i.strip_left,i.strip_right,i.terminal_left,i.terminal_right,"
                             "b.name,b.batch_no "
                             "FROM cut_task t LEFT JOIN cut_item i ON i.id=t.item_id "
                             "LEFT JOIN cut_harness b ON b.id=t.batch_id") + where
              + QStringLiteral(" ORDER BY t.id"));
    for (const QVariant &v : binds) q.addBindValue(v);
    if (!q.exec()) {
        if (err) *err = q.lastError().text();
        return out;
    }
    while (q.next()) out.append(cutTaskFromQuery(q));
    if (!out.isEmpty()) {
        // 任务号统一为「批次导入时间 + 批次内序号」，按全量任务编号，筛选后不变化
        QHash<int, int> batchSeq;
        QHash<int, QString> taskNoById;
        QSqlQuery nq(m_db);
        nq.exec(QStringLiteral(
            "SELECT t.id,t.batch_id,b.created_at FROM cut_task t"
            " LEFT JOIN cut_harness b ON b.id=t.batch_id ORDER BY t.id"));
        while (nq.next()) {
            const int id = nq.value(0).toInt();
            const int bid = nq.value(1).toInt();
            const int no = ++batchSeq[bid];
            const QDateTime dt = QDateTime::fromString(nq.value(2).toString(), Qt::ISODate);
            const QString stamp = dt.isValid() ? dt.toString(QStringLiteral("yyyyMMddHHmm"))
                                               : QStringLiteral("000000000000");
            taskNoById.insert(id, QStringLiteral("%1-%2").arg(stamp).arg(no));
        }
        for (CutTask &t : out) t.taskNo = taskNoById.value(t.id);
    }
    return out;
}

bool DatabaseManager::cutFinishTask(int taskId, int doneQty, const QString &operatorName, QString *err)
{
    if (err) err->clear();
    if (!isServer()) return RemoteApiClient::instance().cutFinishTask(taskId, doneQty, operatorName, err);
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("UPDATE cut_task SET done_qty=?,status=3,operator=?,finished_at=? WHERE id=?"));
    q.addBindValue(doneQty);
    q.addBindValue(operatorName);
    q.addBindValue(QDateTime::currentDateTime().toString(Qt::ISODate));
    q.addBindValue(taskId);
    if (!q.exec()) {
        if (err) *err = q.lastError().text();
        return false;
    }
    addOpLog(QStringLiteral("完成下线任务"), QString(), operatorName,
             QStringLiteral("任务%1，完成 %2 条").arg(taskId).arg(doneQty));
    return true;
}

bool DatabaseManager::cutDeviceAuth(int machineNo, const QString &deviceKey,
                                    int *machineId, QString *machineName, QString *err) const
{
    if (err) err->clear();
    if (!isServer()) {
        if (err) *err = QStringLiteral("设备认证只能在服务器主机执行");
        return false;
    }
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "SELECT machine_id,machine_name FROM cut_device"
        " WHERE machine_id=? AND device_key=? AND active=1"));
    q.addBindValue(machineNo);
    q.addBindValue(deviceKey);
    if (!q.exec() || !q.next()) {
        if (err) *err = q.lastError().type() == QSqlError::NoError
                ? QStringLiteral("机台号或设备密钥错误") : q.lastError().text();
        return false;
    }
    if (machineId) *machineId = q.value(0).toInt();
    if (machineName) *machineName = q.value(1).toString();
    return true;
}

bool DatabaseManager::cutStartTask(int taskId, int machineId, QString *err)
{
    if (err) err->clear();
    if (!isServer()) {
        if (err) *err = QStringLiteral("设备任务只能在服务器主机更新");
        return false;
    }
    QSqlDatabase db = QSqlDatabase::database();
    if (!db.transaction()) {
        if (err) *err = db.lastError().text();
        return false;
    }
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("SELECT status FROM cut_task WHERE id=? AND machine_id=?"));
    q.addBindValue(taskId);
    q.addBindValue(machineId);
    if (!q.exec() || !q.next()) {
        db.rollback();
        if (err) *err = QStringLiteral("任务不存在或不属于当前机台");
        return false;
    }
    const int status = q.value(0).toInt();
    if (status == 3 || status == 4) {
        db.rollback();
        if (err) *err = QStringLiteral("任务已结束，不能开始");
        return false;
    }
    q.prepare(QStringLiteral("UPDATE cut_task SET status=2 WHERE id=?"));
    q.addBindValue(taskId);
    if (!q.exec()) {
        db.rollback();
        if (err) *err = q.lastError().text();
        return false;
    }
    const QString now = QDateTime::currentDateTime().toString(Qt::ISODate);
    q.prepare(QStringLiteral(
        "INSERT INTO cut_device_state(machine_id,current_task_id,last_start_at,updated_at)"
        " VALUES(?,?,?,?) ON CONFLICT(machine_id) DO UPDATE SET"
        " current_task_id=excluded.current_task_id,"
        " last_start_at=excluded.last_start_at, updated_at=excluded.updated_at"));
    q.addBindValue(machineId);
    q.addBindValue(taskId);
    q.addBindValue(now);
    q.addBindValue(now);
    if (!q.exec()) {
        db.rollback();
        if (err) *err = q.lastError().text();
        return false;
    }
    db.commit();
    addOpLog(QStringLiteral("下线任务开始"), QString(), QStringLiteral("设备%1").arg(machineId),
             QStringLiteral("任务%1").arg(taskId));
    return true;
}

bool DatabaseManager::cutConfirmTask(int taskId, int machineId, const QString &operatorName,
                                     int *doneQty, int *quantity, int *status, bool *finished,
                                     QString *err, int step)
{
    if (err) err->clear();
    if (!isServer()) {
        if (err) *err = QStringLiteral("设备任务只能在服务器主机更新");
        return false;
    }
    QSqlDatabase db = QSqlDatabase::database();
    if (!db.transaction()) {
        if (err) *err = db.lastError().text();
        return false;
    }
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "SELECT quantity,done_qty,status FROM cut_task WHERE id=? AND machine_id=?"));
    q.addBindValue(taskId);
    q.addBindValue(machineId);
    if (!q.exec() || !q.next()) {
        db.rollback();
        if (err) *err = QStringLiteral("任务不存在或不属于当前机台");
        return false;
    }
    const int total = q.value(0).toInt();
    const int done = q.value(1).toInt();
    const int oldStatus = q.value(2).toInt();
    if (oldStatus == 3 || oldStatus == 4) {
        db.rollback();
        if (err) *err = QStringLiteral("任务已结束，不能确认");
        return false;
    }
    const int nextDone = qMin(total, done + qMax(1, step));
    const bool isFinished = (nextDone >= total);
    const int nextStatus = isFinished ? 3 : 2;
    const QString now = QDateTime::currentDateTime().toString(Qt::ISODate);

    q.prepare(QStringLiteral(
        "UPDATE cut_task SET done_qty=?,status=?,operator=?,"
        "finished_at=CASE WHEN ? THEN ? ELSE finished_at END WHERE id=?"));
    q.addBindValue(nextDone);
    q.addBindValue(nextStatus);
    q.addBindValue(operatorName);
    q.addBindValue(isFinished);
    q.addBindValue(isFinished ? now : QString());
    q.addBindValue(taskId);
    if (!q.exec()) {
        db.rollback();
        if (err) *err = q.lastError().text();
        return false;
    }
    q.prepare(QStringLiteral(
        "INSERT INTO cut_device_state(machine_id,current_task_id,last_confirm_at,updated_at)"
        " VALUES(?,?,?,?) ON CONFLICT(machine_id) DO UPDATE SET"
        " current_task_id=excluded.current_task_id,"
        " last_confirm_at=excluded.last_confirm_at, updated_at=excluded.updated_at"));
    q.addBindValue(machineId);
    q.addBindValue(taskId);
    q.addBindValue(now);
    q.addBindValue(now);
    if (!q.exec()) {
        db.rollback();
        if (err) *err = q.lastError().text();
        return false;
    }
    db.commit();
    if (doneQty) *doneQty = nextDone;
    if (quantity) *quantity = total;
    if (status) *status = nextStatus;
    if (finished) *finished = isFinished;
    addOpLog(isFinished ? QStringLiteral("下线任务完成") : QStringLiteral("下线任务报产"),
             QString(), operatorName,
             QStringLiteral("任务%1，完成 %2/%3").arg(taskId).arg(nextDone).arg(total));
    return true;
}

bool DatabaseManager::cutLogPrint(int taskId, int machineId, const QString &operatorName,
                                  const QString &labelNo, int copies, const QString &result,
                                  QString *err)
{
    if (err) err->clear();
    if (!isServer()) {
        if (err) *err = QStringLiteral("打印日志只能在服务器主机写入");
        return false;
    }
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "INSERT INTO cut_print_log(task_id,machine_id,operator,label_no,copies,result,created_at)"
        " VALUES(?,?,?,?,?,?,?)"));
    q.addBindValue(taskId);
    q.addBindValue(machineId);
    q.addBindValue(operatorName);
    q.addBindValue(labelNo);
    q.addBindValue(copies);
    q.addBindValue(result);
    q.addBindValue(QDateTime::currentDateTime().toString(Qt::ISODate));
    if (!q.exec()) {
        if (err) *err = q.lastError().text();
        return false;
    }
    return true;
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

bool DatabaseManager::updateQuantities(int productId, int qty, int minQty, int maxQty,
                                       QString *err, bool logOp)
{
    if (!isServer())
        return RemoteApiClient::instance().updateQuantities(
            productId, qty, minQty, maxQty, err, logOp);
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
    if (logOp) {
        QJsonArray rows;
        rows.append(opRow(QStringLiteral("数量"), QString::number(old.qty), QString::number(qty)));
        rows.append(opRow(QStringLiteral("最小数量"), QString::number(old.minQty), QString::number(minQty)));
        rows.append(opRow(QStringLiteral("最大数量"), QString::number(old.maxQty), QString::number(maxQty)));
        addOpLog(QStringLiteral("修改库存数量"), old.model, old.name, opRowsJson(rows));
    }
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
        return RemoteApiClient::instance().qtyLogsOf(productId, err);
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

// 手机端离线同步：每日结存数量日志。按记录日期升序，手机端可直接按相邻日期算用量。
QList<QtyLog> DatabaseManager::qtyLogsChangedSince(const QDateTime &since) const
{
    QList<QtyLog> list;
    QSqlQuery q(m_db);
    QString sql = QStringLiteral(
        "SELECT id,product_id,qty,record_date,created_at FROM product_qty_log");
    if (since.isValid())
        sql += QStringLiteral(" WHERE created_at > ?");
    sql += QStringLiteral(" ORDER BY record_date ASC, id ASC");
    q.prepare(sql);
    if (since.isValid())
        q.addBindValue(since.toString(Qt::ISODate));
    if (!q.exec()) {
        qWarning() << "qtyLogsChangedSince failed:" << q.lastError().text();
        return list;
    }
    while (q.next()) {
        QtyLog l;
        l.id = q.value(0).toInt();
        l.productId = q.value(1).toInt();
        l.qty = q.value(2).toInt();
        l.recordDate = QDate::fromString(q.value(3).toString(), Qt::ISODate);
        l.createdAt = QDateTime::fromString(q.value(4).toString(), Qt::ISODate);
        list << l;
    }
    return list;
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
        // 录入页「补充信息 / 修改」会带上本次操作时间，据此刷新 created_at；
        // Excel 批量导入等未指定时间的入口仍沿用首次录入时间
        if (!p.createdAt.isValid()) p.createdAt = old.createdAt;
        const QString createdAt = p.createdAt.isValid()
            ? p.createdAt.toString(Qt::ISODate) : now;
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

// 删除单张误录图纸：先删记录，再删归档文件；客户端由服务器代删，本机只清预览缓存。
// 与 deleteProducts 的区别是只作用于 drawing 表的一行，产品及其它图纸保持不变。
bool DatabaseManager::deleteDrawing(int drawingId, QString *err)
{
    if (!isServer()) return RemoteApiClient::instance().deleteDrawing(drawingId, err);
    const Drawing d = drawingById(drawingId);
    if (d.id < 0) {
        if (err) *err = QStringLiteral("图纸记录不存在或已被删除");
        return false;
    }
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("DELETE FROM drawing WHERE id=?"));
    q.addBindValue(drawingId);
    if (!q.exec()) {
        if (err) *err = q.lastError().text();
        return false;
    }
    if (!d.filePath.isEmpty())
        QFile::remove(d.filePath);
    const Product p = productById(d.productId);
    QJsonArray rows;
    rows.append(opRow(QStringLiteral("图纸文件"), QFileInfo(d.filePath).fileName(),
                      QStringLiteral("（已删除）")));
    addOpLog(QStringLiteral("删除图纸"), p.model, p.name, opRowsJson(rows));
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

