#include "settingsdialog.h"
#include "db/databasemanager.h"
#include "server/lanhttpserver.h"
#include "model/product.h"

#include <QTimer>

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QTabWidget>
#include <QComboBox>
#include <QTableWidget>
#include <QPlainTextEdit>
#include <QFont>
#include <QHeaderView>
#include <QLineEdit>
#include <QLabel>
#include <QPushButton>
#include <QFileDialog>
#include "ndmessagebox.h"
#include <QInputDialog>
#include <QFile>
#include <QFileInfo>
#include <QTextStream>
#include <QStringDecoder>
#include <QDate>
#include <QSettings>
#include <QDateTime>
#include <QTime>
#include <QTimeEdit>
#include <QTableWidgetItem>
#include <QCheckBox>
#include <QListWidget>
#include <QButtonGroup>

// 自动备份时间统一从 QSettings 读取；旧版本没有该键时沿用 17:00
static QTime configuredAutoTime()
{
    const QTime t = QTime::fromString(
        QSettings().value(QStringLiteral("backup/autoTime"), QStringLiteral("17:00")).toString(),
        QStringLiteral("HH:mm"));
    return t.isValid() ? t : QTime(17, 0);
}
#include <QAbstractButton>
#include <QTextCursor>
#include <QDir>
#include <QDesktopServices>
#include <QUrl>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>

SettingsDialog::SettingsDialog(QWidget *parent) : QWidget(parent)
{
    setWindowTitle(QStringLiteral("设置"));

    QVBoxLayout *root = new QVBoxLayout(this);
    m_tabs = new QTabWidget(this);

    // 「本机角色」两种角色都显示：角色选错时不必再去注册表里删配置
    buildRoleTab();

    // 服务器主机：完整管理功能；客户端：只显示连接信息，不提供账号/备份/手机同步等系统设置
    if (DatabaseManager::isServer()) {
        buildFieldTab();
        buildUserTab();
        buildImportTab();
        buildOfflineTab();   // 手机 App 同步（仅服务器主机需要）
        buildWorkPathTab();
        buildBackupTab();
        buildOpLogTab();     // 近一周用户操作日志
    } else {
        buildConnectionTab();
    }

    root->addWidget(m_tabs);
}

// ==================== 字段管理 ====================
void SettingsDialog::buildFieldTab()
{
    QWidget *page = new QWidget(this);
    QVBoxLayout *lay = new QVBoxLayout(page);
    lay->setContentsMargins(16, 14, 16, 14);
    lay->setSpacing(10);

    QLabel *tip = new QLabel(QStringLiteral(
        "为各产品分类配置特有字段（如控制器的“护套针数”）。录入页的表单和主界面列表列会自动跟随。"), page);
    tip->setObjectName(QStringLiteral("previewInfo"));
    lay->addWidget(tip);

    QHBoxLayout *top = new QHBoxLayout();
    top->addWidget(new QLabel(QStringLiteral("分类："), page));
    m_cmbFieldCat = new QComboBox(page);
    m_cmbFieldCat->addItems(DatabaseManager::categories());
    top->addWidget(m_cmbFieldCat);
    top->addSpacing(16);
    top->addWidget(new QLabel(QStringLiteral("字段名称："), page));
    m_edtFieldLabel = new QLineEdit(page);
    m_edtFieldLabel->setPlaceholderText(QStringLiteral("如：护套针数"));
    m_edtFieldLabel->setFixedWidth(160);
    top->addWidget(m_edtFieldLabel);
    top->addWidget(new QLabel(QStringLiteral("字段标识："), page));
    m_edtFieldKey = new QLineEdit(page);
    m_edtFieldKey->setPlaceholderText(QStringLiteral("可空，默认同字段名"));
    m_edtFieldKey->setFixedWidth(160);
    top->addWidget(m_edtFieldKey);
    QPushButton *btnAdd = new QPushButton(QStringLiteral("添 加"), page);
    btnAdd->setObjectName(QStringLiteral("btnEntry"));
    btnAdd->setCursor(Qt::PointingHandCursor);
    top->addWidget(btnAdd);
    top->addStretch();
    lay->addLayout(top);

    m_fieldTable = new QTableWidget(0, 3, page);
    m_fieldTable->setObjectName(QStringLiteral("productTable"));
    m_fieldTable->setHorizontalHeaderLabels({QStringLiteral("字段名称"), QStringLiteral("字段标识"), QStringLiteral("操作")});
    m_fieldTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_fieldTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_fieldTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Fixed);
    m_fieldTable->setColumnWidth(2, 90);
    m_fieldTable->verticalHeader()->setVisible(false);
    m_fieldTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    lay->addWidget(m_fieldTable, 1);

    connect(m_cmbFieldCat, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &SettingsDialog::onFieldCatChanged);
    connect(btnAdd, &QPushButton::clicked, this, &SettingsDialog::onAddField);
    m_tabs->addTab(page, QStringLiteral("字段管理"));

    refreshFieldTable();
}

void SettingsDialog::onFieldCatChanged(int) { refreshFieldTable(); }

void SettingsDialog::refreshFieldTable()
{
    const QString cat = m_cmbFieldCat->currentText();
    const QList<FieldConfig> fields = DatabaseManager::instance().fieldConfigs(cat);
    m_fieldTable->setRowCount(fields.size());
    for (int i = 0; i < fields.size(); ++i) {
        m_fieldTable->setItem(i, 0, new QTableWidgetItem(fields[i].fieldLabel));
        m_fieldTable->setItem(i, 1, new QTableWidgetItem(fields[i].fieldKey));
        QPushButton *btnDel = new QPushButton(QStringLiteral("删除"), m_fieldTable);
        btnDel->setObjectName(QStringLiteral("btnDanger"));
        btnDel->setCursor(Qt::PointingHandCursor);
        btnDel->setProperty("fieldId", fields[i].id);
        connect(btnDel, &QPushButton::clicked, this, &SettingsDialog::onDeleteField);
        m_fieldTable->setCellWidget(i, 2, btnDel);
        m_fieldTable->setRowHeight(i, 34);
    }
}

void SettingsDialog::onAddField()
{
    const QString label = m_edtFieldLabel->text().trimmed();
    if (label.isEmpty()) {
        NdMessageBox::warning(this, QStringLiteral("提示"), QStringLiteral("字段名称不能为空"));
        return;
    }
    QString key = m_edtFieldKey->text().trimmed();
    if (key.isEmpty()) key = label; // 标识默认同名称
    QString err;
    if (!DatabaseManager::instance().addFieldConfig(m_cmbFieldCat->currentText(), key, label, &err)) {
        NdMessageBox::warning(this, QStringLiteral("添加失败"), err);
        return;
    }
    m_edtFieldLabel->clear();
    m_edtFieldKey->clear();
    refreshFieldTable();
}

void SettingsDialog::onDeleteField()
{
    QPushButton *btn = qobject_cast<QPushButton *>(sender());
    if (!btn) return;
    if (!NdMessageBox::question(this, QStringLiteral("确认"),
                                QStringLiteral("删除该字段？（已录入的历史数据保留，但不再显示）")))
        return;
    DatabaseManager::instance().deleteFieldConfig(btn->property("fieldId").toInt());
    refreshFieldTable();
}

// ==================== 用户日志 ====================
void SettingsDialog::buildOpLogTab()
{
    QWidget *page = new QWidget(this);
    QVBoxLayout *lay = new QVBoxLayout(page);
    lay->setContentsMargins(16, 14, 16, 14);
    lay->setSpacing(10);

    QHBoxLayout *top = new QHBoxLayout();
    QLabel *tip = new QLabel(QStringLiteral(
        "近一周内所有账号（含客户端）对产品、图纸、库存数量的操作；「查看」可看修改前后明细。"), page);
    tip->setObjectName(QStringLiteral("previewInfo"));
    top->addWidget(tip, 1);
    QPushButton *btnClear = new QPushButton(QStringLiteral("清空所有操作日志"), page);
    btnClear->setObjectName(QStringLiteral("btnDanger"));
    btnClear->setCursor(Qt::PointingHandCursor);
    connect(btnClear, &QPushButton::clicked, this, &SettingsDialog::onClearOpLogs);
    top->addWidget(btnClear);
    lay->addLayout(top);

    m_opLogTable = new QTableWidget(0, 7, page);
    m_opLogTable->setObjectName(QStringLiteral("productTable"));
    m_opLogTable->setHorizontalHeaderLabels({QStringLiteral("时间"), QStringLiteral("型号"),
                                              QStringLiteral("名称"), QStringLiteral("用户名"),
                                              QStringLiteral("操作"), QStringLiteral("查看"),
                                              QStringLiteral("删除")});
    QHeaderView *hh = m_opLogTable->horizontalHeader();
    hh->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    hh->setSectionResizeMode(1, QHeaderView::Stretch);
    hh->setSectionResizeMode(2, QHeaderView::Stretch);
    hh->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    hh->setSectionResizeMode(4, QHeaderView::ResizeToContents);
    hh->setSectionResizeMode(5, QHeaderView::Fixed);
    hh->setSectionResizeMode(6, QHeaderView::Fixed);
    m_opLogTable->setColumnWidth(5, 76);
    m_opLogTable->setColumnWidth(6, 76);
    m_opLogTable->verticalHeader()->setVisible(false);
    m_opLogTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    lay->addWidget(m_opLogTable, 1);

    m_tabs->addTab(page, QStringLiteral("用户日志"));
    refreshOpLogTable();
}

void SettingsDialog::refreshOpLogTable()
{
    const QList<OpLog> logs = DatabaseManager::instance().opLogs(
        QDateTime::currentDateTime().addDays(-7));
    m_opLogTable->setRowCount(logs.size());
    for (int i = 0; i < logs.size(); ++i) {
        const OpLog &l = logs.at(i);
        auto put = [&](int c, const QString &s) {
            auto *it = new QTableWidgetItem(s);
            it->setTextAlignment(Qt::AlignCenter);
            m_opLogTable->setItem(i, c, it);
        };
        put(0, l.createdAt.toString(QStringLiteral("yyyy-MM-dd hh:mm:ss")));
        put(1, l.model);
        put(2, l.name);
        put(3, l.oper);
        put(4, l.action);

        QPushButton *btnView = new QPushButton(QStringLiteral("查看"), m_opLogTable);
        btnView->setObjectName(QStringLiteral("btnEntry")); // 绿色
        btnView->setCursor(Qt::PointingHandCursor);
        btnView->setProperty("logId", l.id);
        btnView->setProperty("logInfo", QStringLiteral("%1 于 %2 对型号「%3」（%4）执行：%5")
            .arg(l.oper, l.createdAt.toString(QStringLiteral("yyyy-MM-dd hh:mm:ss")),
                 l.model, l.name, l.action));
        btnView->setProperty("logDetail", l.detail);
        connect(btnView, &QPushButton::clicked, this, &SettingsDialog::onViewOpLog);
        m_opLogTable->setCellWidget(i, 5, btnView);

        QPushButton *btnDel = new QPushButton(QStringLiteral("删除"), m_opLogTable);
        btnDel->setObjectName(QStringLiteral("btnDanger")); // 红色
        btnDel->setCursor(Qt::PointingHandCursor);
        btnDel->setProperty("logId", l.id);
        connect(btnDel, &QPushButton::clicked, this, &SettingsDialog::onDeleteOpLog);
        m_opLogTable->setCellWidget(i, 6, btnDel);
        m_opLogTable->setRowHeight(i, 34);
    }
}

void SettingsDialog::onViewOpLog()
{
    QPushButton *btn = qobject_cast<QPushButton *>(sender());
    if (!btn) return;
    const QString info = btn->property("logInfo").toString();
    const QJsonDocument doc = QJsonDocument::fromJson(btn->property("logDetail").toString().toUtf8());

    QString html = QStringLiteral(
        "<div style='color:rgba(255,255,255,.88);font-size:13px;line-height:150%'>%1</div><br>"
        "<table width='100%' cellpadding='6' cellspacing='0' "
        "style='border-collapse:collapse;color:#fff;font-size:13px'>"
        "<tr style='color:#8a90a6'><th align='left'>修改项</th>"
        "<th align='left'>修改前</th><th align='left'>修改后</th></tr>")
        .arg(info.toHtmlEscaped());
    int n = 0;
    if (doc.isArray()) {
        const QJsonArray arr = doc.array();
        for (const QJsonValue &v : arr) {
            const QJsonObject o = v.toObject();
            ++n;
            html += QStringLiteral(
                "<tr style='background:%1'><td>%2</td>"
                "<td style='color:#E5C07B'>%3</td><td style='color:#43CF7C'>%4</td></tr>")
                .arg(n % 2 ? QStringLiteral("rgba(255,255,255,.04)") : QStringLiteral("transparent"),
                     o.value(QStringLiteral("field")).toString().toHtmlEscaped(),
                     o.value(QStringLiteral("before")).toString().toHtmlEscaped(),
                     o.value(QStringLiteral("after")).toString().toHtmlEscaped());
        }
    }
    if (!n)
        html += QStringLiteral("<tr><td colspan='3' style='color:#8a90a6'>无字段变化</td></tr>");
    html += QStringLiteral("</table>");
    NdMessageBox::details(this, QStringLiteral("操作明细"), html);
}

void SettingsDialog::onDeleteOpLog()
{
    QPushButton *btn = qobject_cast<QPushButton *>(sender());
    if (!btn) return;
    if (!NdMessageBox::question(this, QStringLiteral("确认"),
                                QStringLiteral("删除这条操作日志？（不影响产品数据）")))
        return;
    DatabaseManager::instance().deleteOpLog(btn->property("logId").toInt());
    refreshOpLogTable();
}

void SettingsDialog::onClearOpLogs()
{
    if (!NdMessageBox::question(this, QStringLiteral("确认"),
                                QStringLiteral("清空全部操作日志？此操作不可恢复，产品数据不受影响。")))
        return;
    DatabaseManager::instance().clearOpLogs();
    refreshOpLogTable();
}

// ==================== 账号管理 ====================
void SettingsDialog::buildUserTab()
{
    QWidget *page = new QWidget(this);
    QVBoxLayout *lay = new QVBoxLayout(page);
    lay->setContentsMargins(16, 14, 16, 14);
    lay->setSpacing(10);

    QLabel *tip = new QLabel(QStringLiteral(
        "录入页和录入操作需要账号验证。admin 为内置管理员，不可删除。"), page);
    tip->setObjectName(QStringLiteral("previewInfo"));
    lay->addWidget(tip);

    QHBoxLayout *top = new QHBoxLayout();
    top->addWidget(new QLabel(QStringLiteral("用户名："), page));
    m_edtUserName = new QLineEdit(page);
    m_edtUserName->setFixedWidth(160);
    top->addWidget(m_edtUserName);
    top->addWidget(new QLabel(QStringLiteral("密码："), page));
    m_edtUserPwd = new QLineEdit(page);
    m_edtUserPwd->setFixedWidth(160);
    top->addWidget(m_edtUserPwd);
    QPushButton *btnAdd = new QPushButton(QStringLiteral("新增账号"), page);
    btnAdd->setObjectName(QStringLiteral("btnEntry"));
    btnAdd->setCursor(Qt::PointingHandCursor);
    top->addWidget(btnAdd);
    top->addStretch();
    lay->addLayout(top);

    m_userTable = new QTableWidget(0, 3, page);
    m_userTable->setObjectName(QStringLiteral("productTable"));
    m_userTable->setHorizontalHeaderLabels({QStringLiteral("用户名"), QStringLiteral("修改密码"), QStringLiteral("操作")});
    m_userTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_userTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Fixed);
    m_userTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Fixed);
    m_userTable->setColumnWidth(1, 100);
    m_userTable->setColumnWidth(2, 90);
    m_userTable->verticalHeader()->setVisible(false);
    m_userTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    lay->addWidget(m_userTable, 1);

    connect(btnAdd, &QPushButton::clicked, this, &SettingsDialog::onAddUser);
    m_tabs->addTab(page, QStringLiteral("账号管理"));

    refreshUserTable();
}

void SettingsDialog::refreshUserTable()
{
    const auto users = DatabaseManager::instance().listUsers();
    m_userTable->setRowCount(users.size());
    for (int i = 0; i < users.size(); ++i) {
        QTableWidgetItem *nameItem = new QTableWidgetItem(users[i].second);
        nameItem->setData(Qt::UserRole, users[i].first);
        m_userTable->setItem(i, 0, nameItem);

        QPushButton *btnPwd = new QPushButton(QStringLiteral("重置密码"), m_userTable);
        btnPwd->setObjectName(QStringLiteral("btnGhost"));
        btnPwd->setCursor(Qt::PointingHandCursor);
        connect(btnPwd, &QPushButton::clicked, this, &SettingsDialog::onResetPwd);
        m_userTable->setCellWidget(i, 1, btnPwd);

        QPushButton *btnDel = new QPushButton(QStringLiteral("删除"), m_userTable);
        btnDel->setObjectName(QStringLiteral("btnDanger"));
        btnDel->setCursor(Qt::PointingHandCursor);
        connect(btnDel, &QPushButton::clicked, this, &SettingsDialog::onDeleteUser);
        m_userTable->setCellWidget(i, 2, btnDel);
        m_userTable->setRowHeight(i, 34);
    }
}

int userIdOfRow(QObject *sender, QTableWidget *table) // 由按钮定位所在行的用户 id
{
    QPushButton *btn = qobject_cast<QPushButton *>(sender);
    if (!btn) return -1;
    for (int r = 0; r < table->rowCount(); ++r)
        if (table->cellWidget(r, 1) == btn || table->cellWidget(r, 2) == btn) {
            QTableWidgetItem *it = table->item(r, 0);
            return it ? it->data(Qt::UserRole).toInt() : -1;
        }
    return -1;
}

void SettingsDialog::onAddUser()
{
    QString err;
    if (!DatabaseManager::instance().addUser(m_edtUserName->text(), m_edtUserPwd->text(), &err)) {
        NdMessageBox::warning(this, QStringLiteral("提示"), err);
        return;
    }
    m_edtUserName->clear();
    m_edtUserPwd->clear();
    refreshUserTable();
}

void SettingsDialog::onDeleteUser()
{
    const int id = userIdOfRow(sender(), m_userTable);
    if (id < 0) return;
    if (!NdMessageBox::question(this, QStringLiteral("确认"), QStringLiteral("删除该账号？")))
        return;
    QString err;
    if (!DatabaseManager::instance().deleteUser(id, &err))
        NdMessageBox::warning(this, QStringLiteral("提示"), err);
    refreshUserTable();
}

void SettingsDialog::onResetPwd()
{
    const int id = userIdOfRow(sender(), m_userTable);
    if (id < 0) return;
    bool ok = false;
    const QString pwd = QInputDialog::getText(this, QStringLiteral("重置密码"),
                                              QStringLiteral("请输入新密码："), QLineEdit::Password,
                                              QString(), &ok);
    if (!ok) return;
    QString err;
    if (!DatabaseManager::instance().changeUserPassword(id, pwd, &err))
        NdMessageBox::warning(this, QStringLiteral("提示"), err);
    else
        NdMessageBox::information(this, QStringLiteral("提示"), QStringLiteral("密码已重置"));
}

// ==================== Excel(CSV) 导入 ====================
void SettingsDialog::buildImportTab()
{
    QWidget *page = new QWidget(this);
    QVBoxLayout *lay = new QVBoxLayout(page);
    lay->setContentsMargins(16, 14, 16, 14);
    lay->setSpacing(10);

    QLabel *tip = new QLabel(QStringLiteral(
        "把现有 Excel 表格「另存为 CSV UTF-8」后在此导入。\n"
        "通用格式识别列：型号(必填)、名称、物料编码、硬件版本号、软件版本号、备注、录入人；其余列自动作为特有字段导入。\n"
        "「南都海韵…线束型号对照表」自动对应：物料代码/品名→物料编码、线束名称→名称、海韵型号→型号、"
        "南都名称/南都型号/版本号→同名特有字段（缺失时自动登记），其余无关列忽略。同分类+型号已存在时更新。"), page);
    tip->setObjectName(QStringLiteral("previewInfo"));
    lay->addWidget(tip);

    QHBoxLayout *top = new QHBoxLayout();
    top->addWidget(new QLabel(QStringLiteral("导入到分类："), page));
    m_cmbImportCat = new QComboBox(page);
    m_cmbImportCat->addItems(DatabaseManager::categories());
    top->addWidget(m_cmbImportCat);
    QPushButton *btnPick = new QPushButton(QStringLiteral("选择 CSV 文件…"), page);
    btnPick->setObjectName(QStringLiteral("btnEntry"));
    btnPick->setCursor(Qt::PointingHandCursor);
    top->addWidget(btnPick);
    m_importFileLabel = new QLabel(QStringLiteral("未选择文件"), page);
    m_importFileLabel->setObjectName(QStringLiteral("previewInfo"));
    top->addWidget(m_importFileLabel, 1);
    QPushButton *btnImport = new QPushButton(QStringLiteral("开始导入"), page);
    btnImport->setObjectName(QStringLiteral("btnEntry"));
    btnImport->setCursor(Qt::PointingHandCursor);
    top->addWidget(btnImport);
    lay->addLayout(top);

    m_importPreview = new QTableWidget(page);
    m_importPreview->setObjectName(QStringLiteral("productTable"));
    m_importPreview->verticalHeader()->setVisible(false);
    m_importPreview->setEditTriggers(QAbstractItemView::NoEditTriggers);
    lay->addWidget(m_importPreview, 1);

    m_importResult = new QLabel(page);
    m_importResult->setObjectName(QStringLiteral("previewInfo"));
    lay->addWidget(m_importResult);

    connect(btnPick, &QPushButton::clicked, this, &SettingsDialog::onPickCsv);
    connect(btnImport, &QPushButton::clicked, this, &SettingsDialog::onImport);
    m_tabs->addTab(page, QStringLiteral("Excel 导入"));
}

// ==================== 手机同步（仅服务器主机） ====================
void SettingsDialog::buildOfflineTab()
{
    QWidget *page = new QWidget(this);
    QVBoxLayout *lay = new QVBoxLayout(page);
    lay->setContentsMargins(16, 14, 16, 14);
    lay->setSpacing(10);

    auto mkInfo = [&](const QString &txt) {
        QLabel *l = new QLabel(txt, page);
        l->setObjectName(QStringLiteral("previewInfo"));
        l->setWordWrap(true);
        return l;
    };
    lay->addWidget(mkInfo(QStringLiteral(
        "本服务器负责向手机 App 提供离线同步数据。用 USB 数据线连接本服务器电脑后，在手机开启「USB 网络共享」（手机当热点），") +
        QStringLiteral("App 会在 USB 子网中找到本服务器（运行 8080 服务）并下载。")));

    // 与其它设置页一致的绿色边框分组卡片
    auto mkBlock = [&](const QString &title, const QList<QLabel *> &labels) {
        QWidget *box = new QWidget(page);
        box->setObjectName(QStringLiteral("infoBlock"));
        box->setAttribute(Qt::WA_StyledBackground, true); // 确保 QSS 背景/边框绘制
        QVBoxLayout *b = new QVBoxLayout(box);
        b->setContentsMargins(10, 8, 10, 8);
        b->setSpacing(4);
        QLabel *h = new QLabel(title, box);
        h->setStyleSheet(QStringLiteral("font-size:14px; font-weight:bold; color:#43CF7C;"));
        b->addWidget(h);
        for (QLabel *l : labels) {
            l->setParent(box);
            b->addWidget(l);
        }
        lay->addWidget(box);
    };

    m_lblHostName = mkInfo(QStringLiteral("服务器名称：—"));
    m_lblServer = mkInfo(QStringLiteral("同步服务：—"));
    m_lblInterfaces = mkInfo(QStringLiteral("服务器网络接口：—"));
    m_lblPhoneIp = mkInfo(QStringLiteral("手机 IP：—"));
    mkBlock(QStringLiteral("服务器网络"),
            {m_lblHostName, m_lblServer, m_lblInterfaces, m_lblPhoneIp});

    m_lblDeviceName = mkInfo(QStringLiteral("设备名称：—"));
    m_lblConnStatus = mkInfo(QStringLiteral("连接状态：未连接"));
    m_lblLastSeen = mkInfo(QStringLiteral("最近活动：—"));
    mkBlock(QStringLiteral("USB 设备"),
            {m_lblDeviceName, m_lblConnStatus, m_lblLastSeen});

    QLabel *logTitle = new QLabel(QStringLiteral("服务器同步日志"), page);
    logTitle->setStyleSheet(QStringLiteral("font-size:14px; font-weight:bold; color:#43CF7C;"));
    lay->addWidget(logTitle);
    m_logPhone = new QPlainTextEdit(page);
    m_logPhone->setObjectName(QStringLiteral("logPhone"));
    m_logPhone->setReadOnly(true);
    m_logPhone->setMaximumHeight(180);
    m_logPhone->setMaximumBlockCount(500); // 超过 500 行自动清掉最旧记录，只保留最新
    QFont logFont(QStringLiteral("Consolas"), 10);
    if (!logFont.exactMatch())
        logFont = QFont(QStringLiteral("Courier New"), 10);
    m_logPhone->setFont(logFont);
    m_logPhone->setPlainText(QStringLiteral("（手机 App 同步时，这里显示服务器侧的传输记录：连接、IP、正在下发的文件等）"));
    lay->addWidget(m_logPhone);

    lay->addSpacing(8);
    QPushButton *btnRefresh = new QPushButton(QStringLiteral("立即刷新"), page);
    btnRefresh->setObjectName(QStringLiteral("btnEntry")); // 绿色主按钮
    btnRefresh->setCursor(Qt::PointingHandCursor);
    connect(btnRefresh, &QPushButton::clicked, this, &SettingsDialog::refreshOffline);
    lay->addWidget(btnRefresh, 0, Qt::AlignLeft);

    lay->addStretch();
    m_tabs->addTab(page, QStringLiteral("手机同步"));

    m_offlineTimer = new QTimer(this);
    connect(m_offlineTimer, &QTimer::timeout, this, &SettingsDialog::refreshOffline);
    m_offlineTimer->start(2000);
    refreshOffline(); // 首次立即刷新一次
}

SettingsDialog::~SettingsDialog()
{
    // 析构时先停掉手机同步刷新定时器，并断开子对象发往本页的信号，
    // 避免子控件析构期发出的信号投递到已析构对象触发 Qt 断言
    if (m_offlineTimer)
        m_offlineTimer->stop();
    if (m_backupTimer)
        m_backupTimer->stop();
    const QList<QObject *> kids = findChildren<QObject *>();
    for (QObject *o : kids)
        disconnect(o, nullptr, this, nullptr);
}

// ==================== 本机角色（服务器主机 / 客户端） ====================
void SettingsDialog::buildRoleTab()
{
    QWidget *page = new QWidget(this);
    QVBoxLayout *lay = new QVBoxLayout(page);
    lay->setContentsMargins(16, 14, 16, 14);
    lay->setSpacing(12);
    page->setStyleSheet(QStringLiteral(
        "QLabel { font-size:15px; color:rgba(255,255,255,0.88); }"
        "QLabel#previewInfo { font-size:15px; color:rgba(255,255,255,0.88); }"
        "QLabel#roleStatus { color:#E5C07B; font-size:14px; }"
        "QPushButton#roleChoice { background:#2C344A; color:rgba(255,255,255,0.88);"
        " border:1px solid #4A5468; border-radius:8px; padding:12px 14px;"
        " text-align:left; font-size:14px; }"
        "QPushButton#roleChoice:checked { background:#1E3B2C; border:1px solid #43CF7C;"
        " color:#43CF7C; }"));

    const bool curClient = !DatabaseManager::isServer();
    m_lblRoleCurrent = new QLabel(page);
    m_lblRoleCurrent->setTextFormat(Qt::RichText);
    m_lblRoleCurrent->setWordWrap(true);
    m_lblRoleCurrent->setText(QStringLiteral("<b>本机当前角色：%1</b>")
        .arg(curClient ? QStringLiteral("客户端") : QStringLiteral("服务器主机")));
    lay->addWidget(m_lblRoleCurrent);

    QLabel *tip = new QLabel(QStringLiteral(
        "整个系统只需一台服务器主机：图纸与数据库都保存在服务器主机上，网页查询与手机 App 同步也由它提供。"
        "其余电脑设为客户端，只填写服务器地址即可，本机不保存图纸和数据库。"
        "切换角色后需要重启程序才能生效。"), page);
    tip->setObjectName(QStringLiteral("previewInfo"));
    tip->setWordWrap(true);
    lay->addWidget(tip);

    m_roleGroup = new QButtonGroup(page);
    m_roleGroup->setExclusive(true);
    m_btnRoleServer = new QPushButton(
        QStringLiteral("服务器主机\n保存全部图纸与数据库，负责录入、管理、账号、备份和手机 App 同步"), page);
    m_btnRoleClient = new QPushButton(
        QStringLiteral("客户端\n本机不保存图纸和数据库，通过服务器地址访问；未登录只能查看，登录后可以录入和修改"), page);
    for (QPushButton *b : {m_btnRoleServer, m_btnRoleClient}) {
        b->setObjectName(QStringLiteral("roleChoice"));
        b->setCheckable(true);
        b->setCursor(Qt::PointingHandCursor);
        // 点击后不获取键盘焦点，避免选中卡片上出现一圈虚线焦点框（与分类标签一致）
        b->setFocusPolicy(Qt::NoFocus);
        m_roleGroup->addButton(b);
    }
    (curClient ? m_btnRoleClient : m_btnRoleServer)->setChecked(true);
    lay->addWidget(m_btnRoleServer);
    lay->addWidget(m_btnRoleClient);

    QHBoxLayout *addr = new QHBoxLayout();
    addr->addWidget(new QLabel(QStringLiteral("服务器地址："), page));
    m_edtRoleHost = new QLineEdit(page);
    m_edtRoleHost->setPlaceholderText(QStringLiteral("客户端填写服务器主机 IP 或计算机名"));
    m_edtRoleHost->setText(DatabaseManager::serverHost());
    addr->addWidget(m_edtRoleHost, 1);
    addr->addWidget(new QLabel(QStringLiteral("端口："), page));
    m_edtRolePort = new QLineEdit(page);
    m_edtRolePort->setFixedWidth(90);
    m_edtRolePort->setText(QString::number(DatabaseManager::serverPort()));
    addr->addWidget(m_edtRolePort);
    lay->addLayout(addr);

    m_lblRoleStatus = new QLabel(page);
    m_lblRoleStatus->setObjectName(QStringLiteral("roleStatus"));
    m_lblRoleStatus->setWordWrap(true);
    lay->addWidget(m_lblRoleStatus);

    QHBoxLayout *btns = new QHBoxLayout();
    btns->addStretch();
    QPushButton *btnRestart = new QPushButton(QStringLiteral("切换角色并重启"), page);
    btnRestart->setObjectName(QStringLiteral("btnEntry"));
    btnRestart->setCursor(Qt::PointingHandCursor);
    btns->addWidget(btnRestart);
    lay->addLayout(btns);
    lay->addStretch();

    connect(m_roleGroup, &QButtonGroup::buttonClicked, this, [this](QAbstractButton *) {
        const bool client = m_btnRoleClient->isChecked();
        m_edtRoleHost->setEnabled(client);
        m_lblRoleStatus->setText(client
            ? QStringLiteral("客户端需正确填写服务器地址，账号密码由服务器主机验证。")
            : QStringLiteral("服务器主机在本机启动网页/手机同步服务，端口即服务端口。"));
    });
    m_edtRoleHost->setEnabled(curClient);

    connect(btnRestart, &QPushButton::clicked, this, [this, btnRestart] {
        if (!saveRoleChoice(true))
            return;
        btnRestart->setEnabled(false); // 立即禁用，避免连点拉起多个新进程
    });

    m_tabs->addTab(page, QStringLiteral("本机角色"));
}

bool SettingsDialog::saveRoleChoice(bool restart)
{
    const bool client = m_btnRoleClient->isChecked();
    const QString host = m_edtRoleHost->text().trimmed();
    int port = m_edtRolePort->text().trimmed().toInt();
    if (port <= 0 || port > 65535) port = 8080;
    m_edtRolePort->setText(QString::number(port));

    if (client && host.isEmpty()) {
        m_lblRoleStatus->setText(QStringLiteral("请先填写服务器主机的 IP 地址或计算机名。"));
        m_edtRoleHost->setFocus();
        return false;
    }
    DatabaseManager::setServerHost(host);
    DatabaseManager::setServerPort(port);
    DatabaseManager::setSystemRole(client ? QStringLiteral("client") : QStringLiteral("server"));

    m_lblRoleCurrent->setText(QStringLiteral("<b>本机当前角色：%1</b>")
        .arg(client ? QStringLiteral("客户端") : QStringLiteral("服务器主机")));
    m_lblRoleStatus->setText(restart
        ? QStringLiteral("已保存，程序即将重启生效。")
        : QStringLiteral("已保存，重启程序后生效（角色、服务端口、设置页功能会随之切换）。"));
    emit serverAddressChanged();
    if (restart)
        emit roleChanged();
    return true;
}

// ==================== 客户端：服务器连接 ====================
void SettingsDialog::buildConnectionTab()
{
    QWidget *page = new QWidget(this);
    QVBoxLayout *lay = new QVBoxLayout(page);
    lay->setContentsMargins(16, 14, 16, 14);
    lay->setSpacing(12);
    page->setStyleSheet(QStringLiteral(
        "QLabel { font-size:15px; color:rgba(255,255,255,0.88); }"));

    auto info = [&](const QString &txt) {
        QLabel *l = new QLabel(txt, page);
        l->setWordWrap(true);
        l->setTextFormat(Qt::RichText);
        return l;
    };
    lay->addWidget(info(QStringLiteral("<b>本机角色：客户端</b>")));
    lay->addWidget(info(QStringLiteral(
        "本机不保存图纸和数据库，所有数据都在「服务器主机」上，本机通过服务器地址访问。<br>"
        "未登录时只能查看图纸；使用服务器主机上建立的账号登录后，即可录入和修改。<br>"
        "账号管理、备份还原、手机 App 离线同步都请到服务器主机上操作，客户端不提供这些功能。")));

    QHBoxLayout *row = new QHBoxLayout();
    row->addWidget(new QLabel(QStringLiteral("服务器地址："), page));
    QLineEdit *edtHost = new QLineEdit(page);
    edtHost->setText(DatabaseManager::serverHost());
    row->addWidget(edtHost, 1);
    row->addWidget(new QLabel(QStringLiteral("端口："), page));
    QLineEdit *edtPort = new QLineEdit(page);
    edtPort->setFixedWidth(90);
    edtPort->setText(QString::number(DatabaseManager::serverPort()));
    row->addWidget(edtPort);
    lay->addLayout(row);

    QLabel *status = new QLabel(page);
    status->setObjectName(QStringLiteral("previewInfo"));
    status->setWordWrap(true);
    lay->addWidget(status);

    QHBoxLayout *btns = new QHBoxLayout();
    btns->addStretch();
    QPushButton *btnOpen = new QPushButton(QStringLiteral("打开网页查询"), page);
    btnOpen->setObjectName(QStringLiteral("btnGhost"));
    btnOpen->setCursor(Qt::PointingHandCursor);
    QPushButton *btnSave = new QPushButton(QStringLiteral("确 认"), page);
    btnSave->setObjectName(QStringLiteral("btnEntry"));
    btnSave->setCursor(Qt::PointingHandCursor);
    btns->addWidget(btnOpen);
    btns->addWidget(btnSave);
    lay->addLayout(btns);
    lay->addStretch();

    connect(btnOpen, &QPushButton::clicked, this, [] {
        QDesktopServices::openUrl(QUrl(QStringLiteral("http://%1:%2")
            .arg(DatabaseManager::serverHost()).arg(DatabaseManager::serverPort())));
    });
    connect(btnSave, &QPushButton::clicked, this,
            [this, edtHost, edtPort, status] {
        const QString host = edtHost->text().trimmed();
        int port = edtPort->text().trimmed().toInt();
        if (host.isEmpty()) {
            status->setText(QStringLiteral("请填写服务器地址。"));
            return;
        }
        if (port <= 0 || port > 65535) port = 8080;
        DatabaseManager::setServerHost(host);
        DatabaseManager::setServerPort(port);
        edtPort->setText(QString::number(port));
        status->setText(QStringLiteral("已保存：%1:%2\n标题栏会显示新地址，点一下即可打开网页查询。")
                            .arg(host).arg(port));
        emit serverAddressChanged();
    });

    m_tabs->addTab(page, QStringLiteral("服务器连接"));
}

// ==================== 工作路径 ====================
void SettingsDialog::buildWorkPathTab()
{
    QWidget *page = new QWidget(this);
    QVBoxLayout *lay = new QVBoxLayout(page);
    lay->setContentsMargins(16, 14, 16, 14);
    lay->setSpacing(12);

    // 该页的目录说明文字统一放大，便于阅读（覆盖全局 #previewInfo 的 12px）
    page->setStyleSheet(QStringLiteral(
        "QLabel { font-size:15px; color:rgba(255,255,255,0.88); }"
        "QLabel#previewInfo { font-size:15px; color:rgba(255,255,255,0.88); }"));

    QLabel *title = new QLabel(QStringLiteral("工作路径（仅服务器主机：图纸与数据库根目录）"), page);
    title->setObjectName(QStringLiteral("previewInfo"));
    lay->addWidget(title);

    QHBoxLayout *row = new QHBoxLayout();
    row->addWidget(new QLabel(QStringLiteral("根目录："), page));
    m_edtWorkRoot = new QLineEdit(page);
    m_edtWorkRoot->setReadOnly(true);
    m_edtWorkRoot->setText(DatabaseManager::instance().storageRoot());
    row->addWidget(m_edtWorkRoot, 1);
    QPushButton *btnPick = new QPushButton(QStringLiteral("选择目录…"), page);
    btnPick->setObjectName(QStringLiteral("btnGhost"));
    btnPick->setCursor(Qt::PointingHandCursor);
    row->addWidget(btnPick);
    lay->addLayout(row);

    m_lblWorkDb = new QLabel(page);
    m_lblWorkDb->setObjectName(QStringLiteral("previewInfo"));
    m_lblWorkDb->setWordWrap(true);
    lay->addWidget(m_lblWorkDb);

    // 确认（绿）/ 取消（红）
    QHBoxLayout *btns = new QHBoxLayout();
    btns->addStretch();
    QPushButton *btnOk = new QPushButton(QStringLiteral("确 认"), page);
    btnOk->setObjectName(QStringLiteral("btnEntry"));
    btnOk->setCursor(Qt::PointingHandCursor);
    QPushButton *btnCancel = new QPushButton(QStringLiteral("取 消"), page);
    btnCancel->setObjectName(QStringLiteral("btnDanger"));
    btnCancel->setCursor(Qt::PointingHandCursor);
    btns->addWidget(btnOk);
    btns->addWidget(btnCancel);
    lay->addLayout(btns);

    m_lblWorkStatus = new QLabel(page);
    m_lblWorkStatus->setObjectName(QStringLiteral("previewInfo"));
    m_lblWorkStatus->setWordWrap(true);
    lay->addWidget(m_lblWorkStatus);

    // 按键下面写工作路径根目录说明
    QLabel *intro = new QLabel(QStringLiteral(
        "<b>工作路径根目录说明</b><br>"
        "1. 根目录下按产品类型分子文件夹（控制器、电器盒、开关、传感器、仪表、线束、方向盘），"
        "每个产品再按「名称 型号」建一层文件夹存放图纸。<br>"
        "2. 根目录下还有一个「数据库」文件夹，保存 nd_drawings.db 及自动备份文件；客户端不需要设置这里。<br>"
        "3. <b>移植服务器数据时，请把整个根目录一起打包复制</b>到新电脑/新位置，"
        "再在这里选择该目录；只复制数据库文件会丢失全部图纸。<br>"
        "4. 修改后需<b>重启软件</b>生效。若新目录中没有数据库，重启后会在其中新建空库。"), page);
    intro->setObjectName(QStringLiteral("previewInfo"));
    intro->setWordWrap(true);
    intro->setTextFormat(Qt::RichText);
    lay->addWidget(intro);
    lay->addStretch();

    connect(btnPick, &QPushButton::clicked, this, &SettingsDialog::onPickWorkRoot);
    connect(btnOk, &QPushButton::clicked, this, &SettingsDialog::onConfirmWorkRoot);
    connect(btnCancel, &QPushButton::clicked, this, &SettingsDialog::onCancelWorkRoot);
    m_tabs->addTab(page, QStringLiteral("工作路径"));
    refreshWorkDbLabel();
}

void SettingsDialog::refreshWorkDbLabel()
{
    if (!m_lblWorkDb || !m_edtWorkRoot) return;
    const QString root = QDir::cleanPath(m_edtWorkRoot->text());
    m_lblWorkDb->setText(QStringLiteral("数据库目录：%1/数据库　（nd_drawings.db）").arg(root));
}

void SettingsDialog::onPickWorkRoot()
{
    const QString dir = QFileDialog::getExistingDirectory(
        this, QStringLiteral("选择工作路径根目录"), m_edtWorkRoot->text(),
        QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks);
    if (dir.isEmpty()) return;
    m_edtWorkRoot->setText(QDir::cleanPath(dir));
    refreshWorkDbLabel();
    m_lblWorkStatus->setText(QStringLiteral("已选择新目录，点「确 认」保存；重启软件后生效。"));
}

void SettingsDialog::onConfirmWorkRoot()
{
    const QString root = QDir::cleanPath(m_edtWorkRoot->text().trimmed());
    if (root.isEmpty()) return;
    if (root.compare(DatabaseManager::instance().storageRoot(), Qt::CaseInsensitive) == 0) {
        m_lblWorkStatus->setText(QStringLiteral("工作路径未变更。"));
        return;
    }
    if (!QDir().mkpath(root) || !QFileInfo(root).isWritable()) {
        NdMessageBox::warning(this, QStringLiteral("工作路径"),
                              QStringLiteral("该目录不可写或无法创建，请重新选择。"));
        return;
    }
    const QString newDb = QDir::cleanPath(root + QStringLiteral("/数据库/nd_drawings.db"));
    const QString tip = QStringLiteral(
        "确认把工作路径改为：\n%1\n\n%2\n\n修改保存后需要重启软件才能生效。")
        .arg(root)
        .arg(QFileInfo::exists(newDb)
             ? QStringLiteral("已检测到该目录下的数据库，重启后将直接使用。")
             : QStringLiteral("该目录下还没有数据库，重启后会新建空库；如需保留现有数据，"
                              "请先取消，并把原根目录整体复制到这里。"));
    if (!NdMessageBox::question(this, QStringLiteral("确认工作路径"), tip)) return;

    QDir().mkpath(QDir::cleanPath(root + QStringLiteral("/数据库"))); // 根目录内含数据库子文件夹
    DatabaseManager::writeStorageRoot(root);
    m_lblWorkStatus->setText(QStringLiteral("已保存：%1\n请重启软件后生效。").arg(root));
    NdMessageBox::information(this, QStringLiteral("工作路径"),
                              QStringLiteral("已保存，重启软件后生效。"));
}

void SettingsDialog::onCancelWorkRoot()
{
    m_edtWorkRoot->setText(DatabaseManager::instance().storageRoot());
    refreshWorkDbLabel();
    m_lblWorkStatus->setText(QStringLiteral("已取消修改，恢复为当前工作路径。"));
}

// ==================== 备份还原 ====================
void SettingsDialog::buildBackupTab()
{
    QWidget *page = new QWidget(this);
    QVBoxLayout *lay = new QVBoxLayout(page);
    lay->setContentsMargins(16, 14, 16, 14);
    lay->setSpacing(10);

    QLabel *tip = new QLabel(QStringLiteral(
        "（仅服务器主机执行）备份文件保存在工作路径的「数据库/备份」文件夹中。"
        "「恢复」会用所选备份覆盖当前数据库，覆盖前会自动再备份一次当前库；"
        "超过 30 天的备份文件会被自动清理。"), page);
    tip->setObjectName(QStringLiteral("previewInfo"));
    tip->setWordWrap(true);
    lay->addWidget(tip);

    // 第一行：操作按钮 + 自动备份运行状态（让用户能确认自动备份确实跑过）
    QHBoxLayout *row1 = new QHBoxLayout();
    QPushButton *btnBackup = new QPushButton(QStringLiteral("立 即 备 份"), page);
    btnBackup->setObjectName(QStringLiteral("btnEntry"));
    btnBackup->setCursor(Qt::PointingHandCursor);
    QPushButton *btnRestore = new QPushButton(QStringLiteral("从文件还原…"), page);
    btnRestore->setObjectName(QStringLiteral("btnDanger"));
    btnRestore->setCursor(Qt::PointingHandCursor);
    row1->addWidget(btnBackup);
    row1->addWidget(btnRestore);
    row1->addStretch();
    m_lblAutoBackupInfo = new QLabel(page);
    m_lblAutoBackupInfo->setObjectName(QStringLiteral("previewInfo"));
    row1->addWidget(m_lblAutoBackupInfo);
    lay->addLayout(row1);

    // 第二行：自动备份开关 + 可调时间
    QHBoxLayout *row2 = new QHBoxLayout();
    m_chkAutoBackup = new QCheckBox(QStringLiteral("自动备份（每天）"), page);
    m_chkAutoBackup->setChecked(
        QSettings().value(QStringLiteral("backup/autoEnabled"), true).toBool());
    m_edtAutoBackupTime = new QTimeEdit(page);
    m_edtAutoBackupTime->setDisplayFormat(QStringLiteral("HH:mm"));
    m_edtAutoBackupTime->setTime(configuredAutoTime());
    m_edtAutoBackupTime->setFixedWidth(86);
    m_edtAutoBackupTime->setEnabled(m_chkAutoBackup->isChecked());
    m_edtAutoBackupTime->setToolTip(QStringLiteral(
        "每天到该时刻自动备份一次；若到点后程序才启动，会自动补做当天备份"));
    row2->addWidget(m_chkAutoBackup);
    row2->addWidget(m_edtAutoBackupTime);
    row2->addWidget(new QLabel(QStringLiteral("执行；备份保留 30 天"), page));
    row2->addStretch();
    lay->addLayout(row2);

    // 备份列表：与「用户日志」一致的表格，每条可单独恢复 / 删除
    m_backupTable = new QTableWidget(0, 6, page);
    m_backupTable->setObjectName(QStringLiteral("productTable"));
    m_backupTable->setHorizontalHeaderLabels({QStringLiteral("备份文件"), QStringLiteral("类型"),
                                               QStringLiteral("大小"), QStringLiteral("备份时间"),
                                               QStringLiteral("恢复"), QStringLiteral("删除")});
    QHeaderView *hh = m_backupTable->horizontalHeader();
    hh->setSectionResizeMode(0, QHeaderView::Stretch);
    hh->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    hh->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    hh->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    hh->setSectionResizeMode(4, QHeaderView::Fixed);
    hh->setSectionResizeMode(5, QHeaderView::Fixed);
    m_backupTable->setColumnWidth(4, 76);
    m_backupTable->setColumnWidth(5, 76);
    m_backupTable->verticalHeader()->setVisible(false);
    m_backupTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_backupTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    lay->addWidget(m_backupTable, 1);

    m_lblBackupStatus = new QLabel(page);
    m_lblBackupStatus->setObjectName(QStringLiteral("previewInfo"));
    m_lblBackupStatus->setWordWrap(true);
    lay->addWidget(m_lblBackupStatus);

    connect(btnBackup, &QPushButton::clicked, this, &SettingsDialog::onBackupNow);
    connect(btnRestore, &QPushButton::clicked, this, &SettingsDialog::onRestore);
    connect(m_chkAutoBackup, &QCheckBox::toggled, this, &SettingsDialog::onAutoBackupToggled);
    connect(m_edtAutoBackupTime, &QTimeEdit::timeChanged,
            this, &SettingsDialog::onAutoBackupTimeChanged);
    m_tabs->addTab(page, QStringLiteral("备份还原"));

    refreshBackupList();
    pruneOldBackups();
    // 每分钟检查一次：到点且当天还没备份时执行；到点之后启动会补做当天备份
    m_backupTimer = new QTimer(this);
    connect(m_backupTimer, &QTimer::timeout, this, &SettingsDialog::checkAutoBackup);
    m_backupTimer->start(60 * 1000);
    checkAutoBackup();
}

QString SettingsDialog::newBackupPath(const QString &tag) const
{
    return DatabaseManager::instance().backupDirectory()
        + QStringLiteral("/nd_drawings_") + tag
        + QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd_HHmmss"))
        + QStringLiteral(".db");
}

void SettingsDialog::refreshBackupList()
{
    if (!m_backupTable) return;
    m_backupTable->setRowCount(0);
    QDir d(DatabaseManager::instance().backupDirectory());
    const QFileInfoList files = d.entryInfoList(
        {QStringLiteral("nd_drawings_*.db")}, QDir::Files, QDir::Name | QDir::Reversed);
    m_backupTable->setRowCount(files.size());
    for (int i = 0; i < files.size(); ++i) {
        const QFileInfo &fi = files.at(i);
        QString type = QStringLiteral("手动");
        if (fi.fileName().contains(QStringLiteral("_自动_"))) type = QStringLiteral("自动");
        else if (fi.fileName().contains(QStringLiteral("_还原前_"))) type = QStringLiteral("还原前");

        auto put = [&](int c, const QString &s) {
            auto *it = new QTableWidgetItem(s);
            it->setTextAlignment(Qt::AlignCenter);
            m_backupTable->setItem(i, c, it);
        };
        put(0, fi.fileName());
        m_backupTable->item(i, 0)->setTextAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        put(1, type);
        put(2, QStringLiteral("%1 KB").arg((fi.size() + 1023) / 1024));
        put(3, fi.lastModified().toString(QStringLiteral("yyyy-MM-dd HH:mm")));

        QPushButton *btnRestore = new QPushButton(QStringLiteral("恢复"), m_backupTable);
        btnRestore->setObjectName(QStringLiteral("btnEntry")); // 绿色
        btnRestore->setCursor(Qt::PointingHandCursor);
        btnRestore->setProperty("backupPath", fi.absoluteFilePath());
        connect(btnRestore, &QPushButton::clicked, this, &SettingsDialog::onRestoreBackup);
        m_backupTable->setCellWidget(i, 4, btnRestore);

        QPushButton *btnDel = new QPushButton(QStringLiteral("删除"), m_backupTable);
        btnDel->setObjectName(QStringLiteral("btnDanger")); // 红色
        btnDel->setCursor(Qt::PointingHandCursor);
        btnDel->setProperty("backupPath", fi.absoluteFilePath());
        connect(btnDel, &QPushButton::clicked, this, &SettingsDialog::onDeleteBackup);
        m_backupTable->setCellWidget(i, 5, btnDel);
        m_backupTable->setRowHeight(i, 34);
    }
    refreshAutoBackupStatus();
}

void SettingsDialog::refreshAutoBackupStatus()
{
    if (!m_lblAutoBackupInfo) return;
    QSettings s;
    const bool on = s.value(QStringLiteral("backup/autoEnabled"), true).toBool();
    const QString last = s.value(QStringLiteral("backup/lastAutoDate")).toString();
    m_lblAutoBackupInfo->setText(QStringLiteral("自动备份：%1　｜　上次自动备份：%2")
        .arg(on ? QStringLiteral("每天 %1").arg(configuredAutoTime().toString(QStringLiteral("HH:mm")))
                : QStringLiteral("已关闭"),
             last.isEmpty() ? QStringLiteral("暂无记录") : last));
}

void SettingsDialog::pruneOldBackups()
{
    const QDateTime cutoff = QDateTime::currentDateTime().addDays(-30);
    QDir d(DatabaseManager::instance().backupDirectory());
    int removed = 0;
    for (const QFileInfo &fi : d.entryInfoList({QStringLiteral("nd_drawings_*.db")}, QDir::Files)) {
        if (fi.lastModified() < cutoff && QFile::remove(fi.absoluteFilePath()))
            ++removed;
    }
    if (removed > 0 && m_lblBackupStatus)
        m_lblBackupStatus->setText(QStringLiteral("已清理 %1 个超过 30 天的备份文件。").arg(removed));
}

void SettingsDialog::onBackupNow()
{
    DatabaseManager &db = DatabaseManager::instance();
    QDir().mkpath(db.backupDirectory());
    const QString dst = newBackupPath(QStringLiteral("手动_"));
    QString err;
    if (!db.backupDatabase(dst, &err)) {
        m_lblBackupStatus->setText(QStringLiteral("备份失败：%1").arg(err));
        NdMessageBox::warning(this, QStringLiteral("备份失败"), err);
        return;
    }
    refreshBackupList();
    pruneOldBackups();
    m_lblBackupStatus->setText(QStringLiteral("备份成功：%1").arg(QFileInfo(dst).fileName()));
    NdMessageBox::information(this, QStringLiteral("备份完成"),
                              QStringLiteral("已备份到：\n%1").arg(dst));
}

void SettingsDialog::onRestore()
{
    DatabaseManager &db = DatabaseManager::instance();
    const QString src = QFileDialog::getOpenFileName(
        this, QStringLiteral("选择用于还原的数据库（备份位于「数据库/备份」）"),
        db.backupDirectory(), QStringLiteral("数据库文件 (*.db);;所有文件 (*)"));
    if (!src.isEmpty())
        restoreFrom(src);
}

bool SettingsDialog::restoreFrom(const QString &src)
{
    if (src.isEmpty() || !QFileInfo::exists(src)) return false;
    if (!NdMessageBox::question(this, QStringLiteral("确认还原"),
            QStringLiteral("将用以下备份覆盖当前数据库：\n%1\n\n"
                           "覆盖前会自动备份一份当前库，还原后界面立即刷新。是否继续？").arg(src)))
        return false;
    QString err;
    if (!DatabaseManager::instance().restoreDatabase(src, &err)) {
        m_lblBackupStatus->setText(QStringLiteral("还原失败：%1").arg(err));
        NdMessageBox::critical(this, QStringLiteral("还原失败"), err);
        return false;
    }
    refreshBackupList();
    m_lblBackupStatus->setText(QStringLiteral("还原成功：%1").arg(QFileInfo(src).fileName()));
    emit databaseRestored();
    NdMessageBox::information(this, QStringLiteral("还原完成"),
                              QStringLiteral("数据库已还原，界面数据已刷新。"));
    return true;
}

void SettingsDialog::onRestoreBackup()
{
    QPushButton *btn = qobject_cast<QPushButton *>(sender());
    if (!btn) return;
    restoreFrom(btn->property("backupPath").toString());
}

void SettingsDialog::onDeleteBackup()
{
    QPushButton *btn = qobject_cast<QPushButton *>(sender());
    if (!btn) return;
    const QString path = btn->property("backupPath").toString();
    if (path.isEmpty()) return;
    if (!NdMessageBox::question(this, QStringLiteral("确认删除"),
            QStringLiteral("删除这个备份文件？此操作不可恢复：\n%1")
                .arg(QFileInfo(path).fileName())))
        return;
    if (!QFile::remove(path)) {
        m_lblBackupStatus->setText(QStringLiteral("删除失败：文件可能被占用。"));
        NdMessageBox::warning(this, QStringLiteral("删除失败"),
                              QStringLiteral("无法删除该备份，请确认文件未被其它程序占用。"));
        return;
    }
    refreshBackupList();
    m_lblBackupStatus->setText(QStringLiteral("已删除备份：%1").arg(QFileInfo(path).fileName()));
}

void SettingsDialog::onAutoBackupToggled(bool on)
{
    QSettings s;
    s.setValue(QStringLiteral("backup/autoEnabled"), on);
    s.sync();
    if (m_edtAutoBackupTime)
        m_edtAutoBackupTime->setEnabled(on);
    refreshAutoBackupStatus();
    m_lblBackupStatus->setText(on
        ? QStringLiteral("已开启自动备份：每天 %1，备份保留 30 天。")
              .arg(configuredAutoTime().toString(QStringLiteral("HH:mm")))
        : QStringLiteral("已关闭自动备份。"));
}

void SettingsDialog::onAutoBackupTimeChanged()
{
    if (!m_edtAutoBackupTime) return;
    const QString t = m_edtAutoBackupTime->time().toString(QStringLiteral("HH:mm"));
    QSettings s;
    s.setValue(QStringLiteral("backup/autoTime"), t);
    s.sync();
    refreshAutoBackupStatus();
    m_lblBackupStatus->setText(QStringLiteral(
        "自动备份时间已设为每天 %1；若今天已过该时间且尚未备份，一分钟内会自动补做。").arg(t));
}

void SettingsDialog::checkAutoBackup()
{
    QSettings s;
    if (!s.value(QStringLiteral("backup/autoEnabled"), true).toBool()) return;
    const QDateTime now = QDateTime::currentDateTime();
    // 时间可调：到设定时刻（含之后启动补做）才执行，每天只做一次
    if (now.time() < configuredAutoTime()) return;
    const QString key = QStringLiteral("backup/lastAutoDate");
    const QString today = now.date().toString(Qt::ISODate);
    if (s.value(key).toString() == today) return;

    DatabaseManager &db = DatabaseManager::instance();
    QDir().mkpath(db.backupDirectory());
    const QString dst = newBackupPath(QStringLiteral("自动_"));
    QString err;
    if (!db.backupDatabase(dst, &err)) {
        if (m_lblBackupStatus)
            m_lblBackupStatus->setText(QStringLiteral("自动备份失败：%1").arg(err));
        return;
    }
    s.setValue(key, today);
    s.sync();
    refreshBackupList();
    pruneOldBackups();
    if (m_lblBackupStatus)
        m_lblBackupStatus->setText(QStringLiteral("自动备份完成：%1").arg(QFileInfo(dst).fileName()));
}

void SettingsDialog::refreshOffline()
{
    if (!m_server) {
        m_lblServer->setText(QStringLiteral("同步服务：未运行"));
        return;
    }
    const LanHttpServer::SyncActivity a = m_server->activity();
    m_lblHostName->setText(QStringLiteral("服务器名称：%1").arg(m_server->localHostName()));
    m_lblServer->setText(QStringLiteral("同步服务：运行中（端口 %1）").arg(m_serverPort));

    QString iface;
    for (const auto &p : m_server->localInterfaces())
        iface += QStringLiteral("　%1　%2\n").arg(p.first, p.second);
    m_lblInterfaces->setText(QStringLiteral("服务器网络接口：\n") +
                             (iface.isEmpty() ? QStringLiteral("（无）") : iface.trimmed()));

    m_lblPhoneIp->setText(QStringLiteral("手机 IP：%1")
                          .arg(a.clientIp.isEmpty() ? QStringLiteral("无") : a.clientIp));
    m_lblDeviceName->setText(QStringLiteral("设备名称：%1")
                             .arg(a.deviceName.isEmpty() ? QStringLiteral("—") : a.deviceName));

    const qint64 secs = a.lastSeen.isValid()
                            ? a.lastSeen.secsTo(QDateTime::currentDateTime()) : -1;
    const bool connected = a.active && secs >= 0 && secs <= 120;
    m_lblConnStatus->setText(QStringLiteral("连接状态：%1")
                             .arg(connected ? QStringLiteral("已连接") : QStringLiteral("未连接")));
    m_lblLastSeen->setText(QStringLiteral("最近活动：%1（%2）")
                           .arg(a.action.isEmpty() ? QStringLiteral("—") : a.action,
                                a.lastSeen.isValid() ? a.lastSeen.toString(QStringLiteral("hh:mm:ss"))
                                                    : QStringLiteral("—")));

    if (m_logPhone) {
        const QString log = m_server->transferLog();
        const QString text = log.isEmpty()
            ? QStringLiteral("（手机 App 同步时，这里显示服务器侧的传输记录：连接、IP、正在下发的文件等）")
            : log;
        // 内容没变就不重置文本，避免每 2 秒打断查看；有新增日志时自动显示最新一行
        if (m_logPhone->toPlainText() != text) {
            m_logPhone->setPlainText(text);
            m_logPhone->moveCursor(QTextCursor::End);
            m_logPhone->ensureCursorVisible();
        }
    }
}

// 解析 CSV：支持 UTF-8（带/不带 BOM）和 GBK；状态机拆分，
// 正确处理引号包裹的单元格（含换行、转义 ""），按 '\r\n' / '\n' 结束一行
QList<QStringList> SettingsDialog::parseCsv(const QString &path) const
{
    QList<QStringList> rows;
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return rows;
    const QByteArray raw = f.readAll();
    f.close();

    QString text;
    if (raw.startsWith("\xEF\xBB\xBF")) {
        text = QString::fromUtf8(raw.mid(3));
    } else {
        QStringDecoder decUtf8(QStringConverter::Utf8);
        text = decUtf8(raw);
        if (decUtf8.hasError()) { // 不是合法 UTF-8，按 GBK（中文 Windows 系统编码）
            QStringDecoder decGbk(QStringConverter::System);
            text = decGbk(raw);
        }
    }

    QStringList cells;
    QString field;
    bool inQuote = false;
    auto endField = [&] {
        cells << field.trimmed();
        field.clear();
    };
    auto endRow = [&] {
        endField();
        // 丢弃整行为空的行（文件末尾的空行等）
        for (const QString &c : cells)
            if (!c.isEmpty()) { rows << cells; break; }
        cells.clear();
    };
    for (int i = 0; i < text.size(); ++i) {
        const QChar c = text.at(i);
        if (inQuote) {
            if (c == QLatin1Char('"')) {
                if (i + 1 < text.size() && text.at(i + 1) == QLatin1Char('"')) {
                    field += QLatin1Char('"'); // 转义的 ""
                    ++i;
                } else {
                    inQuote = false;
                }
            } else {
                field += c; // 引号内的换行/逗号都是内容
            }
        } else if (c == QLatin1Char('"')) {
            inQuote = true;
        } else if (c == QLatin1Char(',')) {
            endField();
        } else if (c == QLatin1Char('\r')) {
            if (i + 1 < text.size() && text.at(i + 1) == QLatin1Char('\n')) ++i;
            endRow();
        } else if (c == QLatin1Char('\n')) {
            endRow();
        } else {
            field += c;
        }
    }
    if (!field.isEmpty() || !cells.isEmpty())
        endRow(); // 末行无换行符
    return rows;
}

void SettingsDialog::onPickCsv()
{
    const QString path = QFileDialog::getOpenFileName(
        this, QStringLiteral("选择 CSV 文件"), QString(),
        QStringLiteral("CSV 文件 (*.csv)"));
    if (path.isEmpty()) return;
    m_csvPath = path;
    m_csvRows = parseCsv(path);
    m_importFileLabel->setText(QFileInfo(path).fileName());
    m_importResult->clear();

    // 自动识别表头行：命中任一已知表头名即认为是表头，其上的标题/说明等无关行自动丢弃
    // （「南都海韵…线束型号对照表」用的是 物料代码/品名、线束名称、海韵型号 等列名）
    static const QStringList headerNames = {
        QStringLiteral("型号"), QStringLiteral("海韵型号"), QStringLiteral("线束名称"),
        QStringLiteral("物料代码"), QStringLiteral("品名"),
        QStringLiteral("物料编码"), QStringLiteral("名称")};
    int headerRow = -1;
    for (int r = 0; r < m_csvRows.size(); ++r) {
        bool hasHeader = false;
        for (const QString &cell : m_csvRows.at(r))
            if (headerNames.contains(cell.trimmed())) { hasHeader = true; break; }
        if (hasHeader) { headerRow = r; break; }
    }
    if (headerRow > 0)
        m_csvRows = m_csvRows.mid(headerRow); // 前 N 行无关内容自动跳过

    // 线束型号对照表：自动把导入分类切到「线束」
    if (headerRow >= 0 && !m_csvRows.isEmpty()) {
        const QStringList &hdr = m_csvRows.first();
        if (hdr.contains(QStringLiteral("海韵型号")) || hdr.contains(QStringLiteral("线束名称"))) {
            const int idx = m_cmbImportCat->findText(QStringLiteral("线束"));
            if (idx >= 0) m_cmbImportCat->setCurrentIndex(idx);
        }
    }

    // 预览全部行：文件里有多少行就显示多少行
    const int rows = m_csvRows.size();
    int cols = 0;
    for (int i = 0; i < rows; ++i) cols = qMax(cols, m_csvRows[i].size());
    m_importPreview->setRowCount(rows);
    m_importPreview->setColumnCount(cols);
    for (int r = 0; r < rows; ++r)
        for (int c = 0; c < m_csvRows[r].size(); ++c)
            m_importPreview->setItem(r, c, new QTableWidgetItem(m_csvRows[r].at(c)));

    QString msg;
    if (headerRow < 0)
        msg = QStringLiteral("警告：未找到表头行（型号/海韵型号/品名/线束名称等），请检查文件");
    else if (headerRow > 0)
        msg = QStringLiteral("已自动跳过前 %1 行无关内容，").arg(headerRow);
    msg += QStringLiteral("共 %1 行数据，全部显示，导入时按此全量执行")
               .arg(qMax(0, m_csvRows.size() - 1));
    m_importResult->setText(msg);
}

void SettingsDialog::onImport()
{
    if (m_csvRows.size() < 2) {
        NdMessageBox::warning(this, QStringLiteral("提示"), QStringLiteral("请先选择包含表头和数据的 CSV 文件"));
        return;
    }
    const QStringList header = m_csvRows.first();
    auto colOf = [&header](const QString &name) {
        for (int i = 0; i < header.size(); ++i)
            if (header.at(i).trimmed() == name) return i;
        return -1;
    };

    const QString cat = m_cmbImportCat->currentText();

    // 「南都海韵…线束型号对照表」专用映射：
    // 物料代码/品名→物料编码、线束名称→名称、海韵型号→型号、南都名称/南都型号/版本号→同名特有字段，
    // 表中其余列（序号/更改履历/状态/负责人/工艺状态/储备等）忽略。
    const bool loomNandu = colOf(QStringLiteral("海韵型号")) >= 0
        || (colOf(QStringLiteral("线束名称")) >= 0 && colOf(QStringLiteral("南都型号")) >= 0);

    // 同一目标字段可能在不同版本表里叫法不同（物料代码 / 品名 / 物料编码…），按别名依次匹配
    auto colOfAny = [&colOf](const QStringList &names) {
        for (const QString &n : names) {
            const int c = colOf(n);
            if (c >= 0) return c;
        }
        return -1;
    };

    int cModel = -1, cName = -1, cCode = -1;
    QList<QPair<int, QString>> extCols; // 列号 -> 特有字段 key

    if (loomNandu) {
        cModel = colOfAny({QStringLiteral("海韵型号"), QStringLiteral("型号")});
        cName = colOfAny({QStringLiteral("线束名称"), QStringLiteral("名称")});
        cCode = colOfAny({QStringLiteral("物料代码"), QStringLiteral("品名"),
                          QStringLiteral("物料编码"), QStringLiteral("物料编号")});
        // 按该分类已配置的字段 key 对齐（已有则复用；缺失则自动登记，导入后即可见）
        const QList<FieldConfig> cfg = DatabaseManager::instance().fieldConfigs(cat);
        auto ensureKey = [&cfg, &cat](const QString &label) -> QString {
            for (const FieldConfig &f : cfg)
                if (f.fieldLabel == label || f.fieldKey == label) return f.fieldKey;
            QString e;
            DatabaseManager::instance().addFieldConfig(cat, label, label, &e);
            return label;
        };
        const QStringList extLabels = {QStringLiteral("南都名称"),
                                       QStringLiteral("南都型号"),
                                       QStringLiteral("版本号")};
        for (const QString &label : extLabels) {
            const int c = colOf(label);
            if (c >= 0)
                extCols.append({c, ensureKey(label)});
        }
    } else {
        // 通用格式
        cModel = colOf(QStringLiteral("型号"));
        cName = colOf(QStringLiteral("名称"));
        cCode = colOf(QStringLiteral("物料编码"));
    }
    if (cModel < 0) {
        NdMessageBox::warning(this, QStringLiteral("提示"),
                              QStringLiteral("表头中未找到「型号」或「海韵型号」列"));
        return;
    }
    const int cHw = colOf(QStringLiteral("硬件版本号"));
    const int cSw = colOf(QStringLiteral("软件版本号"));
    const int cRemark = colOf(QStringLiteral("备注"));
    const int cCreator = colOf(QStringLiteral("录入人"));

    if (!loomNandu) {
        // 其余列作为特有字段（序号/图纸等无关列跳过）
        const QStringList skipCols = {QStringLiteral("序号"), QStringLiteral("图纸"),
                                      QStringLiteral("图片"), QStringLiteral("更改说明"),
                                      QStringLiteral("更改记录")};
        for (int i = 0; i < header.size(); ++i) {
            const QString h = header.at(i).trimmed();
            if (h.isEmpty() || skipCols.contains(h)) continue;
            if (i == cModel || i == cName || i == cCode || i == cHw || i == cSw
                || i == cRemark || i == cCreator)
                continue;
            extCols.append({i, h});
        }
    }
    int okCount = 0, failCount = 0, emptyModelCount = 0;
    QString firstErr;
    int firstErrRow = -1;
    for (int r = 1; r < m_csvRows.size(); ++r) {
        const QStringList &row = m_csvRows.at(r);
        auto cell = [&row](int idx) {
            // 返回非 null 的空串：null QString 会被 QSqlQuery 绑定成 SQL NULL，
            // 触发 material_code 的 NOT NULL 约束
            return (idx >= 0 && idx < row.size()) ? row.at(idx).trimmed()
                                                  : QStringLiteral("");
        };
        Product p;
        p.category = cat;
        p.model = cell(cModel);
        if (p.model.isEmpty()) { ++failCount; ++emptyModelCount; continue; }
        p.name = cell(cName);
        p.materialCode = cell(cCode);
        p.hwVersion = cell(cHw);
        p.swVersion = cell(cSw);
        p.remark = cell(cRemark);
        p.creator = cell(cCreator);
        for (const auto &ec : extCols) {
            const QString v = cell(ec.first);
            if (!v.isEmpty() && !v.startsWith(QLatin1Char('='))) // 跳过 WPS 图片公式等
                p.extFields.insert(ec.second, v);
        }

        QString err;
        if (DatabaseManager::instance().saveProduct(p, QString(), &err) >= 0) ++okCount;
        else {
            ++failCount;
            if (firstErr.isEmpty()) { firstErr = err; firstErrRow = r; }
        }
    }
    QString msg = QStringLiteral("导入完成：成功 %1 条，失败 %2 条").arg(okCount).arg(failCount);
    if (emptyModelCount > 0)
        msg += QStringLiteral("（其中 %1 条型号为空被跳过）").arg(emptyModelCount);
    if (!firstErr.isEmpty())
        msg += QStringLiteral("\n首个失败：第 %1 行，原因：%2").arg(firstErrRow).arg(firstErr);
    m_importResult->setText(msg);
}
