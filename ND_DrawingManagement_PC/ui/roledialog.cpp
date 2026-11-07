#include "roledialog.h"

#include <QApplication>
#include <QScreen>
#include <QButtonGroup>
#include <QAbstractButton>
#include <QPushButton>
#include <QLabel>
#include <QLineEdit>
#include <QGridLayout>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QEventLoop>
#include <QKeyEvent>
#include <QPainter>

RoleSelectDialog::RoleSelectDialog(const RoleChoice &current, QWidget *parent)
    : QWidget(parent)
{
    m_result = current;
    m_result.ok = false;

    // 顶层无边框窗口 + 真正透明背景，遮罩才能覆盖整个屏幕
    setWindowFlags(Qt::FramelessWindowHint | Qt::Window | Qt::WindowStaysOnTopHint);
    setAttribute(Qt::WA_TranslucentBackground, true);
    setAttribute(Qt::WA_StyledBackground, true);
    setWindowTitle(QStringLiteral("选择本机角色"));
    setObjectName(QStringLiteral("roleMask"));
    setFocusPolicy(Qt::StrongFocus);
    setStyleSheet(QStringLiteral(
        "QWidget#roleMask{background:rgba(0,0,0,0.62);}"
        "QWidget#roleCard{background:#22293C;border:1px solid rgba(255,255,255,0.14);"
        "border-radius:12px;}"
        "QWidget#roleCard QLabel{background:transparent;color:rgba(255,255,255,0.85);"
        "font-size:14px;}"
        "QLabel#roleTitle{font-size:21px;font-weight:bold;color:#FFFFFF;}"
        "QLabel#roleHint{color:rgba(255,255,255,0.62);font-size:13px;}"
        "QLabel#roleStatus{color:#E5C07B;font-size:13px;}"
        "QPushButton#roleChoice{background:#2C344A;color:rgba(255,255,255,0.88);"
        "border:1px solid #4A5468;border-radius:8px;padding:12px 14px;"
        "text-align:left;font-size:14px;}"
        "QPushButton#roleChoice:hover{background:#333D55;}"
        "QPushButton#roleChoice:checked{background:#1E3B2C;border:1px solid #43CF7C;"
        "color:#43CF7C;}"
        "QPushButton#roleChoice:disabled{color:rgba(255,255,255,0.45);}"
        "QLineEdit{background:#242B3E;border:1px solid #4A5468;border-radius:6px;"
        "padding:7px 10px;color:#FFFFFF;font-size:14px;"
        "selection-background-color:#43CF7C;}"
        "QLineEdit:disabled{color:rgba(255,255,255,0.45);background:#1F2536;}"
        "QPushButton#roleOk{background:#43CF7C;color:#0B1F14;border:none;border-radius:6px;"
        "padding:9px 32px;font-weight:bold;font-size:14px;}"
        "QPushButton#roleOk:hover{background:#5CE0A0;}"
        "QPushButton#roleCancel{background:rgba(224,108,117,0.85);color:#fff;"
        "border:none;border-radius:6px;padding:9px 28px;font-size:14px;}"
        "QPushButton#roleCancel:hover{background:rgba(232,130,138,0.95);}"));

    QGridLayout *outer = new QGridLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);

    QWidget *card = new QWidget(this);
    card->setObjectName(QStringLiteral("roleCard"));
    card->setFixedWidth(600);
    QVBoxLayout *cl = new QVBoxLayout(card);
    cl->setContentsMargins(30, 24, 30, 22);
    cl->setSpacing(12);

    QLabel *title = new QLabel(QStringLiteral("选择本机角色"), card);
    title->setObjectName(QStringLiteral("roleTitle"));
    title->setAlignment(Qt::AlignCenter);
    QLabel *hint = new QLabel(
        QStringLiteral("请选择这台电脑在图纸管理系统中的角色（整个系统只需一台服务器主机）："), card);
    hint->setObjectName(QStringLiteral("roleHint"));
    hint->setWordWrap(true);
    cl->addWidget(title);
    cl->addWidget(hint);

    m_roleGroup = new QButtonGroup(card);
    m_roleGroup->setExclusive(true);
    m_btnServer = new QPushButton(
        QStringLiteral("服务器主机\n保存全部图纸与数据库，负责录入、管理、账号、备份和手机 App 同步"), card);
    m_btnClient = new QPushButton(
        QStringLiteral("客户端\n本机不保存图纸和数据库，通过服务器地址访问；未登录只能查看，登录后可以录入和修改"), card);
    for (QPushButton *b : {m_btnServer, m_btnClient}) {
        b->setObjectName(QStringLiteral("roleChoice"));
        b->setCheckable(true);
        b->setCursor(Qt::PointingHandCursor);
        // 点击后不获取键盘焦点，避免选中卡片上出现一圈虚线焦点框
        b->setFocusPolicy(Qt::NoFocus);
        m_roleGroup->addButton(b);
    }
    const bool curClient = (current.role == QLatin1String("client"));
    (curClient ? m_btnClient : m_btnServer)->setChecked(true);
    cl->addWidget(m_btnServer);
    cl->addWidget(m_btnClient);
    connect(m_roleGroup, &QButtonGroup::buttonClicked, this, [this](QAbstractButton *) {
        onRoleChanged();
    });

    QHBoxLayout *addr = new QHBoxLayout();
    addr->addWidget(new QLabel(QStringLiteral("服务器地址："), card));
    m_edtHost = new QLineEdit(card);
    m_edtHost->setPlaceholderText(QStringLiteral("例如 192.168.1.10 或服务器计算机名"));
    m_edtHost->setText(current.host);
    addr->addWidget(m_edtHost, 1);
    addr->addWidget(new QLabel(QStringLiteral("端口："), card));
    m_edtPort = new QLineEdit(card);
    m_edtPort->setFixedWidth(90);
    m_edtPort->setText(QString::number(current.port > 0 ? current.port : 8080));
    addr->addWidget(m_edtPort);
    cl->addLayout(addr);

    m_status = new QLabel(card);
    m_status->setObjectName(QStringLiteral("roleStatus"));
    m_status->setWordWrap(true);
    m_status->setFixedHeight(20);
    cl->addWidget(m_status);

    QHBoxLayout *btns = new QHBoxLayout();
    btns->addStretch();
    QPushButton *cancel = new QPushButton(QStringLiteral("取 消"), card);
    cancel->setObjectName(QStringLiteral("roleCancel"));
    cancel->setCursor(Qt::PointingHandCursor);
    QPushButton *ok = new QPushButton(QStringLiteral("确 定"), card);
    ok->setObjectName(QStringLiteral("roleOk"));
    ok->setCursor(Qt::PointingHandCursor);
    ok->setDefault(true);
    btns->addWidget(cancel);
    btns->addWidget(ok);
    cl->addLayout(btns);

    connect(cancel, &QPushButton::clicked, this, [this] { finish(false); });
    connect(ok, &QPushButton::clicked, this, [this] { finish(true); });
    connect(m_edtHost, &QLineEdit::returnPressed, this, [this] { finish(true); });
    connect(m_edtPort, &QLineEdit::returnPressed, this, [this] { finish(true); });

    outer->addWidget(card, 0, 0, Qt::AlignCenter);
    onRoleChanged();
}

void RoleSelectDialog::onRoleChanged()
{
    const bool client = m_btnClient && m_btnClient->isChecked();
    if (m_edtHost) m_edtHost->setEnabled(client);
    if (m_edtPort) m_edtPort->setEnabled(client);
    if (m_status)
        m_status->setText(client
            ? QStringLiteral("客户端只需填写服务器地址，图纸与数据库都在服务器主机上。")
            : QString());
}

void RoleSelectDialog::finish(bool ok)
{
    if (!ok) {
        m_result.ok = false;
    } else {
        if (m_btnClient->isChecked()) {
            const QString host = m_edtHost->text().trimmed();
            if (host.isEmpty()) {
                m_status->setText(QStringLiteral("请填写服务器主机的 IP 地址或计算机名。"));
                m_edtHost->setFocus();
                return;
            }
            int port = m_edtPort->text().trimmed().toInt();
            if (port <= 0 || port > 65535) port = 8080;
            m_result.role = QStringLiteral("client");
            m_result.host = host;
            m_result.port = port;
        } else {
            m_result.role = QStringLiteral("server");
        }
        m_result.ok = true;
    }
    hide();
    if (m_loop) m_loop->quit();
}

RoleChoice RoleSelectDialog::choose(const RoleChoice &current)
{
    RoleSelectDialog box(current);
    if (QScreen *screen = QApplication::primaryScreen())
        box.setGeometry(screen->geometry()); // 遮罩铺满整块屏幕
    box.show();
    box.raise();
    box.activateWindow();
    box.setFocus();

    QEventLoop loop;
    box.m_loop = &loop;
    loop.exec();
    box.m_loop = nullptr;
    return box.m_result;
}

void RoleSelectDialog::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    // 整个窗口铺一层半透明黑，背后所有内容变暗，聚焦到中央卡片
    p.fillRect(rect(), QColor(0, 0, 0, 179)); // 179/255 ≈ 0.70
}

void RoleSelectDialog::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Escape)
        finish(false);
    else if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter)
        finish(true);
    else
        QWidget::keyPressEvent(event);
}
