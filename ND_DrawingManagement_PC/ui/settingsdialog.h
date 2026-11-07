#ifndef SETTINGSDIALOG_H
#define SETTINGSDIALOG_H

#include <QWidget>
#include <QList>
#include <QStringList>
#include <QTimer>

class QTabWidget;
class QComboBox;
class QTableWidget;
class QTimeEdit;
class QLineEdit;
class QLabel;
class QPlainTextEdit;
class QCheckBox;
class QButtonGroup;
class QPushButton;
class LanHttpServer;

// 设置页：服务器主机显示全部管理 tab；客户端只显示「服务器连接」
class SettingsDialog : public QWidget
{
    Q_OBJECT
public:
    explicit SettingsDialog(QWidget *parent = nullptr);
    ~SettingsDialog() override;

    // 注入正在运行的同步服务（用于「手机同步」tab 展示实时连接信息）
    void setServer(LanHttpServer *server, quint16 port) { m_server = server; m_serverPort = port; }

signals:
    void databaseRestored();     // 还原数据库后通知主窗口刷新列表
    void serverAddressChanged(); // 客户端修改服务器地址后刷新标题栏
    void roleChanged();          // 切换了本机角色，通知主窗口重启程序使配置生效

private slots:
    // 字段管理
    void onFieldCatChanged(int index);
    void onAddField();
    void onDeleteField();
    // 账号管理
    void onAddUser();
    void onDeleteUser();
    void onResetPwd();
    // 导入
    void onPickCsv();
    void onImport();
    // 手机同步实时刷新
    void refreshOffline();
    // 用户日志
    void onViewOpLog();
    void onDeleteOpLog();
    void onClearOpLogs();
    // 工作路径
    void onPickWorkRoot();
    void onConfirmWorkRoot();
    void onCancelWorkRoot();
    // 备份还原
    void onBackupNow();
    void onRestore();             // 从外部文件选择备份还原
    void onRestoreBackup();       // 表格里单条「恢复」
    void onDeleteBackup();        // 表格里单条「删除」
    void onAutoBackupToggled(bool on);
    void onAutoBackupTimeChanged();
    void checkAutoBackup();

private:
    void buildFieldTab();
    void buildUserTab();
    void buildImportTab();
    void buildOfflineTab();
    void buildWorkPathTab();
    void buildBackupTab();
    void buildConnectionTab(); // 客户端：服务器地址与角色说明
    void buildRoleTab();       // 所有角色：切换本机角色（服务器主机 / 客户端）
    void buildOpLogTab();      // 服务器主机：近一周用户操作日志
    bool saveRoleChoice(bool restart); // 写入 QSettings，restart=true 时发出 roleChanged()
    void refreshFieldTable();
    void refreshUserTable();
    void refreshOpLogTable();
    void refreshWorkDbLabel();
    void refreshBackupList();
    void refreshAutoBackupStatus();          // 展示自动备份开关/时间/上次执行日期
    bool restoreFrom(const QString &src);    // 还原公共逻辑（单条恢复与外部文件共用）
    void pruneOldBackups();
    QString newBackupPath(const QString &tag) const;
    QList<QStringList> parseCsv(const QString &path) const; // 解析 CSV（自动识别 UTF-8/GBK）

    QTabWidget *m_tabs = nullptr;

    // 切换本机角色
    QButtonGroup *m_roleGroup = nullptr;
    QPushButton *m_btnRoleServer = nullptr;
    QPushButton *m_btnRoleClient = nullptr;
    QLineEdit *m_edtRoleHost = nullptr;
    QLineEdit *m_edtRolePort = nullptr;
    QLabel *m_lblRoleStatus = nullptr;
    QLabel *m_lblRoleCurrent = nullptr;

    // 手机同步
    LanHttpServer *m_server = nullptr;
    quint16 m_serverPort = 0;
    QTimer *m_offlineTimer = nullptr;
    QLabel *m_lblHostName = nullptr;
    QLabel *m_lblServer = nullptr;
    QLabel *m_lblInterfaces = nullptr;
    QLabel *m_lblPhoneIp = nullptr;
    QLabel *m_lblDeviceName = nullptr;
    QLabel *m_lblConnStatus = nullptr;
    QLabel *m_lblLastSeen = nullptr;
    QPlainTextEdit *m_logPhone = nullptr;   // 手机端实时传输日志

    // 字段管理
    QComboBox *m_cmbFieldCat = nullptr;
    QLineEdit *m_edtFieldLabel = nullptr;
    QLineEdit *m_edtFieldKey = nullptr;
    QTableWidget *m_fieldTable = nullptr;

    // 账号管理
    QLineEdit *m_edtUserName = nullptr;
    QLineEdit *m_edtUserPwd = nullptr;
    QTableWidget *m_userTable = nullptr;

    // 用户日志
    QTableWidget *m_opLogTable = nullptr;

    // 导入
    QComboBox *m_cmbImportCat = nullptr;
    QLabel *m_importFileLabel = nullptr;
    QTableWidget *m_importPreview = nullptr;
    QLabel *m_importResult = nullptr;
    QString m_csvPath;
    QList<QStringList> m_csvRows; // 含表头

    // 工作路径
    QLineEdit *m_edtWorkRoot = nullptr;
    QLabel *m_lblWorkDb = nullptr;
    QLabel *m_lblWorkStatus = nullptr;

    // 备份还原
    QCheckBox *m_chkAutoBackup = nullptr;
    QTimeEdit *m_edtAutoBackupTime = nullptr;
    QTableWidget *m_backupTable = nullptr;   // 与用户日志一致的表格：单条可恢复/删除
    QLabel *m_lblAutoBackupInfo = nullptr;
    QLabel *m_lblBackupStatus = nullptr;
    QTimer *m_backupTimer = nullptr;
};

#endif // SETTINGSDIALOG_H
