#include "mainwindow.h"
#include "db/databasemanager.h"
#include "entrydialog.h"
#include "managepage.h"
#include "settingsdialog.h"
#include "toolspage.h"
#include "updatedialog.h"
#include "server/lanhttpserver.h"
#include "server/updatemanager.h"
#include "server/remoteapiclient.h"
#include "roledialog.h"

#include <QCoreApplication>
#include <QTimer>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QComboBox>
#include <QNetworkInterface>
#include <QHostAddress>
#include <QClipboard>
#include <QGuiApplication>
#include <QTableWidget>
#include <QHeaderView>
#include <QButtonGroup>
#include <QTextBrowser>
#include <QTextDocument>
#include <QTextOption>
#include <QListWidget>
#include <QFrame>
#include <QScrollBar>
#include <QStyle>
#include <QFileDialog>
#include <QMessageBox>
#include <QSplitter>
#include <QStackedWidget>
#include <QPdfDocument>
#include <QPdfView>
#include <QMouseEvent>
#include <QInputDialog>
#include <QSettings>
#include <QPixmap>
#include <QImageReader>
#include <QPainter>
#include <QIcon>
#include "ndmessagebox.h"
#include "roundedmenu.h"
#include <QDesktopServices>
#include <QUrl>
#include <QFileInfo>
#include <QScrollArea>
#include <QApplication>
#include <QScreen>
#include <QMenu>
#include <QAction>
#include <QProcess>
#include <QToolTip>
#include <QCursor>
#include <QDir>
#include <QContextMenuEvent>
#include <cstdlib>

namespace {
const char *kBgDark = "#22293C";
const char *kBgPanel = "#2B3046";
const char *kGreen = "#43CF7C";
}

MainWindow::MainWindow(QWidget *parent) : QWidget(parent)
{
    setWindowFlags(Qt::FramelessWindowHint);
    setWindowTitle(QStringLiteral("南都图纸管理"));
    resize(1400, 860);

    buildTitleBar();
    buildCategoryTabs();
    buildToolBar();
    buildBody();

    // 页面0：主界面（分类Tab + 工具栏 + 列表/预览）
    QWidget *page0 = new QWidget(this);
    QVBoxLayout *p0 = new QVBoxLayout(page0);
    p0->setContentsMargins(0, 0, 0, 0);
    p0->setSpacing(0);
    QWidget *catBar = findChild<QWidget *>(QStringLiteral("catBar"));
    QWidget *toolBar = findChild<QWidget *>(QStringLiteral("toolBar"));
    QWidget *body = findChild<QWidget *>(QStringLiteral("bodyWid"));
    // 与手机端主页一致：第一行标题栏，第二行搜索框+搜索键，第三行几大分类
    p0->addWidget(toolBar);
    p0->addWidget(catBar);
    p0->addWidget(body, 1);

    // 页面1：录入；页面2：管理；页面3：设置；页面4：工具（库存查询 + 关于）
    m_entryPage = new EntryDialog(this);
    m_managePage = new ManagePage(this);
    m_settingsPage = new SettingsDialog(this);
    m_toolsPage = new ToolsPage(this);

    m_stack = new QStackedWidget(this);
    m_stack->addWidget(page0);
    m_stack->addWidget(m_entryPage);
    m_stack->addWidget(m_managePage);
    m_stack->addWidget(m_settingsPage);
    m_stack->addWidget(m_toolsPage);

    // 管理页「修改」→ 跳转录入页编辑
    connect(m_managePage, &ManagePage::requestEditProduct, this, [this](int pid) {
        m_entryPage->loadProduct(pid);
        m_stack->setCurrentIndex(1);
        m_menuGroup->button(1)->setChecked(true);
    });

    QVBoxLayout *root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);
    QWidget *titleBar = findChild<QWidget *>(QStringLiteral("titleBar"));
    root->addWidget(titleBar);
    // 菜单栏下方的浅灰细线放在共用根布局里，切到任何页面都会保留
    QWidget *titleLine = new QWidget(this);
    titleLine->setObjectName(QStringLiteral("titleLine"));
    titleLine->setFixedHeight(1);
    root->addWidget(titleLine);
    root->addWidget(m_stack, 1);

    // 录入页保存成功 / 取消 → 回到主界面并刷新列表
    connect(m_entryPage, &EntryDialog::saved, this, [this] {
        reloadTable();
        m_stack->setCurrentIndex(0);
        m_menuGroup->button(0)->setChecked(true);
    });
    connect(m_entryPage, &EntryDialog::cancelled, this, [this] {
        m_stack->setCurrentIndex(0);
        m_menuGroup->button(0)->setChecked(true);
    });
    // 备份还原后数据库文件已被替换，重新查询列表
    connect(m_settingsPage, &SettingsDialog::databaseRestored, this, [this] {
        reloadTable();
    });
    // 客户端在「服务器连接」页改了地址后刷新标题栏
    connect(m_settingsPage, &SettingsDialog::serverAddressChanged, this, [this] {
        updateServerInfo();
    });
    // 「本机角色」页切换角色后重启：服务启停、数据库来源都要按新角色重新初始化
    connect(m_settingsPage, &SettingsDialog::roleChanged, this, [this] {
        restartApplication();
    });

    applyStyle();
    m_currentCat.clear(); // 默认「所有类别」：不限分类（与手机端首页一致）
    reloadTable();

    buildLoginOverlay();

    // 只有「服务器主机」提供网页查询与手机 App 同步服务；客户端只记录服务器地址
    if (DatabaseManager::isServer()) {
        m_httpServer = new LanHttpServer(this);
        m_serverPort = static_cast<quint16>(DatabaseManager::serverPort());
        m_httpServer->start(m_serverPort);
        if (m_settingsPage)
            m_settingsPage->setServer(m_httpServer, m_serverPort); // 「手机同步」tab 实时信息
    } else {
        m_httpServer = nullptr;
        m_serverPort = static_cast<quint16>(DatabaseManager::serverPort());
    }
    updateServerInfo();

    // 状态栏：服务器显示手机同步进度，客户端显示数据来源
    m_syncStatusLabel = new QLabel(DatabaseManager::isServer()
        ? QStringLiteral("手机同步：暂无设备连接")
        : QStringLiteral("客户端：数据来自服务器主机"), this);
    m_syncStatusLabel->setObjectName(QStringLiteral("syncStatus"));
    m_syncStatusLabel->setStyleSheet(QStringLiteral(
        "background:#22293C;color:#8a90a6;padding:6px 14px;font-size:13px;"));

    // 状态栏右侧：服务器显示已连接客户端 IP（客户端模式下隐藏）
    m_clientStatusLabel = new QLabel(QStringLiteral("已连接客户端：无"), this);
    m_clientStatusLabel->setObjectName(QStringLiteral("clientStatus"));
    m_clientStatusLabel->setStyleSheet(QStringLiteral(
        "background:#22293C;color:#8a90a6;padding:6px 14px;font-size:13px;"));
    m_clientStatusLabel->setVisible(DatabaseManager::isServer());

    // 状态栏底层横向布局：左=手机同步/客户端连接，右=已连接客户端 IP
    QWidget *statusBar = new QWidget(this);
    statusBar->setObjectName(QStringLiteral("statusBar"));
    QHBoxLayout *sbl = new QHBoxLayout(statusBar);
    sbl->setContentsMargins(0, 0, 0, 0);
    sbl->setSpacing(0);
    sbl->addWidget(m_syncStatusLabel);
    sbl->addStretch(1);
    sbl->addWidget(m_clientStatusLabel);
    root->addWidget(statusBar);

    m_syncTimer = new QTimer(this);
    connect(m_syncTimer, &QTimer::timeout, this, &MainWindow::refreshSyncStatus);
    m_syncTimer->start(1000);
}

MainWindow::~MainWindow()
{
    // 析构时先停掉定时器，并断开所有子对象发往本窗口的信号。
    // 子控件（尤其 QComboBox）在进入基类 ~QObject 销毁时会发出 currentIndexChanged，
    // 而此刻本对象已不再是 MainWindow，连接若仍存在会触发 Qt 断言
    // “Called object is not the correct type (class destructor may have already run)”。
    if (m_syncTimer)
        m_syncTimer->stop();
    const QList<QObject *> kids = findChildren<QObject *>();
    for (QObject *o : kids)
        disconnect(o, nullptr, this, nullptr);
}

// 自绘标题栏按钮图标：线框风格，与深色主题协调
static QIcon makeTitleIcon(const QString &type)
{
    const int s = 32;
    QPixmap pm(s, s);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(QPen(QColor(QStringLiteral("#cfd3e0")), 1.8, Qt::SolidLine, Qt::RoundCap));
    const QRectF r(10, 10, 12, 12);
    if (type == QLatin1String("min"))
        p.drawLine(QPointF(r.left(), r.center().y()), QPointF(r.right(), r.center().y()));
    else if (type == QLatin1String("max"))
        p.drawRect(r);
    else {
        p.drawLine(r.topLeft(), r.bottomRight());
        p.drawLine(r.topRight(), r.bottomLeft());
    }
    return QIcon(pm);
}

// 搜索框内的一键清除按钮：平时灰色空心圆圈 + 灰叉，悬停/按下时实心红圆 + 红叉
// （亮红圆上画同色亮红叉会看不见，叉用深红，仍属红色系且对比清晰）
static QIcon makeClearSearchIcon()
{
    static const QColor gray(QStringLiteral("#8A90A6"));
    static const QColor red(QStringLiteral("#FF5252"));
    static const QColor darkRed(QStringLiteral("#8E1B1B"));
    const auto draw = [](bool hover) {
        const int s = 20;
        QPixmap pm(s, s);
        pm.fill(Qt::transparent);
        QPainter p(&pm);
        p.setRenderHint(QPainter::Antialiasing);
        const QRectF circle(2.5, 2.5, 15, 15);
        if (hover) {
            p.setPen(Qt::NoPen);
            p.setBrush(red);
            p.drawEllipse(circle);
        } else {
            p.setBrush(Qt::NoBrush);
            p.setPen(QPen(gray, 1.6, Qt::SolidLine, Qt::RoundCap));
            p.drawEllipse(circle);
        }
        p.setPen(QPen(hover ? darkRed : gray, 1.8, Qt::SolidLine, Qt::RoundCap));
        const QRectF r(7, 7, 6, 6);
        p.drawLine(r.topLeft(), r.bottomRight());
        p.drawLine(r.topRight(), r.bottomLeft());
        p.end();
        return pm;
    };

    QIcon icon;
    // 输入框内部按钮在鼠标悬停/按下时会取 Active / Selected 状态的图标
    const QIcon::State states[] = {QIcon::On, QIcon::Off};
    for (QIcon::State st : states) {
        icon.addPixmap(draw(false), QIcon::Normal, st);
        icon.addPixmap(draw(true), QIcon::Active, st);
        icon.addPixmap(draw(true), QIcon::Selected, st);
    }
    return icon;
}

// ---------- 标题栏 ----------
void MainWindow::buildTitleBar()
{
    QWidget *bar = new QWidget(this);
    bar->setObjectName(QStringLiteral("titleBar"));
    bar->setFixedHeight(60);
    QHBoxLayout *lay = new QHBoxLayout(bar);
    lay->setContentsMargins(10, 0, 10, 0);
    lay->setSpacing(15);

    QLabel *logo = new QLabel(bar);
    QPixmap pm(QStringLiteral(":/image/logo.png"));
    if (!pm.isNull())
        logo->setPixmap(pm.scaled(50, 50, Qt::KeepAspectRatioByExpanding));
    QLabel *title = new QLabel(QStringLiteral("南都图纸管理"), bar);
    title->setObjectName(QStringLiteral("appTitle"));
    lay->addWidget(logo);
    lay->addWidget(title);

    // 顶部菜单：主界面 / 录入 / 管理 / 设置 / 工具（沿用电器盒检测工程风格）
    QHBoxLayout *menuLay = new QHBoxLayout();
    menuLay->setSpacing(30);
    menuLay->setContentsMargins(50, 0, 0, 0);
    m_menuGroup = new QButtonGroup(this);
    m_menuGroup->setExclusive(true);
    const QStringList menus = {QStringLiteral("主界面"), QStringLiteral("录入"),
                               QStringLiteral("管理"), QStringLiteral("设置"), QStringLiteral("工具")};
    for (int i = 0; i < menus.size(); ++i) {
        QPushButton *b = new QPushButton(menus.at(i), bar);
        b->setObjectName(QStringLiteral("menuBtn"));
        b->setFixedSize(80, 60);
        b->setCheckable(true);
        b->setFocusPolicy(Qt::NoFocus);
        b->setCursor(Qt::PointingHandCursor);
        m_menuGroup->addButton(b, i);
        menuLay->addWidget(b, 0, Qt::AlignHCenter);
        if (i == 0) b->setChecked(true);
    }
    lay->addLayout(menuLay);
    lay->addStretch();

    connect(m_menuGroup, &QButtonGroup::idClicked, this, [this](int id) {
        switch (id) {
        case 0: // 主界面
            m_stack->setCurrentIndex(0);
            break;
        case 1: // 录入：已登录直接进入，未登录先验证
            if (!m_currentUser.isEmpty()) {
                m_entryPage->resetForm();
                m_stack->setCurrentIndex(1);
            } else {
                m_pwdTargetPage = 1;
                m_pwdTitle->setText(QStringLiteral("录入验证"));
                m_userEdit->clear();
                m_pwdEdit->clear();
                m_pwdError->clear();
                m_pwdOverlay->showOverlay();
                m_userEdit->setFocus();
                m_menuGroup->button(m_stack->currentIndex())->setChecked(true);
            }
            break;
        case 2: // 管理：账号密码正确即可修改，不再限定管理员角色
            if (!m_currentUser.isEmpty()) {
                m_managePage->refresh();
                m_stack->setCurrentIndex(2);
            } else {
                m_pwdTargetPage = 2;
                m_pwdTitle->setText(QStringLiteral("管理验证"));
                m_userEdit->clear();
                m_pwdEdit->clear();
                m_pwdError->clear();
                m_pwdOverlay->showOverlay();
                m_userEdit->setFocus();
                m_menuGroup->button(m_stack->currentIndex())->setChecked(true);
            }
            break;
        case 3: // 设置：涉及数据库修改，同样要求登录
            if (!m_currentUser.isEmpty()) {
                m_stack->setCurrentIndex(3);
            } else {
                m_pwdTargetPage = 3;
                m_pwdTitle->setText(QStringLiteral("设置验证"));
                m_userEdit->clear();
                m_pwdEdit->clear();
                m_pwdError->clear();
                m_pwdOverlay->showOverlay();
                m_userEdit->setFocus();
                m_menuGroup->button(m_stack->currentIndex())->setChecked(true);
            }
            break;
        case 4: // 工具：只查询 ERP 存货档案，不改产品数据库，未登录用户也可直接使用
            m_toolsPage->onPageShown();
            m_stack->setCurrentIndex(4);
            break;
        }
    });

    // 右侧信息列：登录状态 / 服务器地址 / 版本号三行统一放到「服务器」这一列，
    // 共用 #titleInfo 样式（登录标签的灰色 + 悬停绿色），行间距为 0
    lay->addStretch();
    QWidget *infoBox = new QWidget(bar);
    QVBoxLayout *infoLay = new QVBoxLayout(infoBox);
    infoLay->setContentsMargins(0, 0, 0, 0);
    infoLay->setSpacing(0);

    m_loginLabel = new QLabel(infoBox);
    m_loginLabel->setObjectName(QStringLiteral("titleInfo"));
    m_loginLabel->setCursor(Qt::PointingHandCursor);
    m_loginLabel->setToolTip(QStringLiteral("点击登录或退出"));
    m_loginLabel->installEventFilter(this);
    infoLay->addWidget(m_loginLabel);
    updateLoginLabel();

    m_serverInfo = new QLabel(infoBox);
    m_serverInfo->setObjectName(QStringLiteral("titleInfo"));
    m_serverInfo->setCursor(Qt::PointingHandCursor);
    m_serverInfo->setToolTip(QStringLiteral("点击打开网页查询"));
    m_serverInfo->installEventFilter(this);
    infoLay->addWidget(m_serverInfo);

    m_versionLabel = new QLabel(infoBox);
    m_versionLabel->setObjectName(QStringLiteral("titleInfo"));
    m_versionLabel->setCursor(Qt::PointingHandCursor);
    m_versionLabel->setToolTip(QStringLiteral("点击检查版本更新"));
    m_versionLabel->installEventFilter(this);
    infoLay->addWidget(m_versionLabel);
    lay->addWidget(infoBox);
    lay->addSpacing(12);

    auto mkBtn = [bar](const QString &iconType, const QString &name) {
        QPushButton *b = new QPushButton(bar);
        b->setObjectName(name);
        b->setIcon(makeTitleIcon(iconType));
        b->setIconSize(QSize(16, 16));
        b->setFixedSize(36, 26);
        b->setCursor(Qt::PointingHandCursor);
        return b;
    };
    QPushButton *btnMin = mkBtn(QStringLiteral("min"), QStringLiteral("btnMin"));
    QPushButton *btnMax = mkBtn(QStringLiteral("max"), QStringLiteral("btnMax"));
    QPushButton *btnClose = mkBtn(QStringLiteral("close"), QStringLiteral("btnClose"));
    lay->addWidget(btnMin);
    lay->addWidget(btnMax);
    lay->addWidget(btnClose);

    connect(btnMin, &QPushButton::clicked, this, &QWidget::showMinimized);
    connect(btnMax, &QPushButton::clicked, this, [this] {
        isMaximized() ? showNormal() : showMaximized();
    });
    connect(btnClose, &QPushButton::clicked, this, &QWidget::close);
}

// 取局域网 IPv4 地址（排除回环与链路本地 169.254.*）
static QString lanIpAddress()
{
    const QList<QHostAddress> list = QNetworkInterface::allAddresses();
    for (const QHostAddress &a : list) {
        if (a.protocol() == QAbstractSocket::IPv4Protocol
            && a != QHostAddress::LocalHost
            && !a.isInSubnet(QHostAddress(QStringLiteral("169.254.0.0")), 16))
            return a.toString();
    }
    return QStringLiteral("127.0.0.1");
}

void MainWindow::updateServerInfo()
{
    if (!m_serverInfo) return;
    const bool server = DatabaseManager::isServer();
    // 服务器显示本机 IP；客户端显示要连接的服务器地址，两者点击都打开同一个网页
    const QString ip = server ? lanIpAddress() : DatabaseManager::serverHost();
    const int port = server ? m_serverPort : DatabaseManager::serverPort();
    m_accessUrl = QStringLiteral("http://%1:%2").arg(ip).arg(port);
    // 三行统一「三字标签 + 全角空格 + 值」，右半列起始位置对齐
    m_serverInfo->setText(QStringLiteral("%1　%2:%3")
                              .arg(server ? QStringLiteral("服务器") : QStringLiteral("客户端"),
                                   ip, QString::number(port)));
    m_serverInfo->setToolTip(QStringLiteral("点击打开网页：%1").arg(m_accessUrl));
    if (m_versionLabel)
        m_versionLabel->setText(QStringLiteral("版本号　%1")
                                    .arg(UpdateManager::instance().localVersion()));
}

void MainWindow::refreshSyncStatus()
{
    if (!m_syncStatusLabel) return;
    // 客户端不跑同步服务：状态栏显示与服务器主机的连通情况
    if (!DatabaseManager::isServer()) {
        const bool ok = RemoteApiClient::instance().reachable();
        m_syncStatusLabel->setText(ok
            ? QStringLiteral("客户端：已连接服务器主机 %1:%2，图纸与数据库都在服务器上")
                .arg(DatabaseManager::serverHost()).arg(DatabaseManager::serverPort())
            : QStringLiteral("客户端：未连接服务器，请确认服务器主机程序已启动、地址与端口正确"));
        m_syncStatusLabel->setStyleSheet(ok
            ? "background:#1E3B2C;color:#43CF7C;padding:6px 14px;font-size:13px;"
            : "background:#3A2630;color:#E8A0A8;padding:6px 14px;font-size:13px;");
        return;
    }
    if (!m_httpServer) return;

    // 右侧状态条：已连接客户端（近 2 分钟有活动即视为在线）
    const QList<LanHttpServer::ClientConn> cls = m_httpServer->clients();
    if (cls.isEmpty()) {
        m_clientStatusLabel->setText(QStringLiteral("已连接客户端：无"));
        m_clientStatusLabel->setStyleSheet(
            "background:#22293C;color:#8a90a6;padding:6px 14px;font-size:13px;");
    } else {
        QStringList parts;
        for (const LanHttpServer::ClientConn &c : cls)
            parts.append(c.account.isEmpty()
                ? c.ip
                : QStringLiteral("%1（%2）").arg(c.account).arg(c.ip));
        m_clientStatusLabel->setText(
            QStringLiteral("已连接客户端（%1）：%2").arg(cls.size()).arg(parts.join(QStringLiteral("，"))));
        m_clientStatusLabel->setStyleSheet(
            "background:#1E3B2C;color:#43CF7C;padding:6px 14px;font-size:13px;");
    }

    const LanHttpServer::SyncActivity a = m_httpServer->activity();
    const qint64 secs = a.lastSeen.isValid()
        ? a.lastSeen.secsTo(QDateTime::currentDateTime()) : -1;
    // 单张大图传输可能超过 30s，放宽到 120s，避免传输途中被误判为「无设备」
    if (!a.active || secs < 0 || secs > 120) {
        m_syncStatusLabel->setText(QStringLiteral("手机同步：暂无设备连接"));
        m_syncStatusLabel->setStyleSheet(
            "background:#22293C;color:#8a90a6;padding:6px 14px;font-size:13px;");
        return;
    }
    QString ip = a.clientIp;
    if (ip.startsWith(QStringLiteral("::ffff:")))
        ip = ip.mid(7);
    m_syncStatusLabel->setText(
        QStringLiteral("已连接设备 %1｜%2（%3 秒前）").arg(ip, a.action).arg(secs));
    m_syncStatusLabel->setStyleSheet(
        "background:#1E3B2C;color:#43CF7C;padding:6px 14px;font-size:13px;");
}

// ---------- 分类 Tab ----------
void MainWindow::buildCategoryTabs()
{
    QWidget *bar = new QWidget(this);
    bar->setObjectName(QStringLiteral("catBar"));
    bar->setFixedHeight(40);
    QHBoxLayout *lay = new QHBoxLayout(bar);
    lay->setContentsMargins(10, 0, 10, 0);
    lay->setSpacing(2);

    m_catGroup = new QButtonGroup(this);
    m_catGroup->setExclusive(true);
    // 首位「所有类别」：不限分类（id=0）；其后为几大分类（id=1..N）
    const QStringList cats = QStringList{QStringLiteral("所有类别")}
                             + DatabaseManager::categories();
    for (int i = 0; i < cats.size(); ++i) {
        QPushButton *b = new QPushButton(cats.at(i), bar);
        b->setObjectName(QStringLiteral("catTab"));
        b->setCheckable(true);
        b->setCursor(Qt::PointingHandCursor);
        // 点击后不获取键盘焦点，避免文字周围出现白色焦点虚框（与主菜单按钮一致）
        b->setFocusPolicy(Qt::NoFocus);
        if (i == 0) b->setChecked(true);
        m_catGroup->addButton(b, i);
        lay->addWidget(b);
    }
    lay->addStretch();
    connect(m_catGroup, &QButtonGroup::idClicked, this, &MainWindow::onCategoryChanged);
}

// ---------- 工具栏 ----------
void MainWindow::buildToolBar()
{
    QWidget *bar = new QWidget(this);
    bar->setObjectName(QStringLiteral("toolBar"));
    bar->setFixedHeight(48);
    QHBoxLayout *lay = new QHBoxLayout(bar);
    lay->setContentsMargins(10, 6, 10, 6);
    lay->setSpacing(8);

    m_searchEdit = new QLineEdit(bar);
    m_searchEdit->setPlaceholderText(QStringLiteral("型号 / 物料编码 / 名称…"));
    m_searchEdit->setFixedWidth(320);
    m_btnSearch = new QPushButton(QStringLiteral("搜 索"), bar);
    m_btnSearch->setObjectName(QStringLiteral("btnSearch"));

    m_cmbDrawingType = new QComboBox(bar);
    m_cmbDrawingType->addItem(QStringLiteral("图纸：全部"), -1);
    m_cmbDrawingType->addItem(QStringLiteral("下发图"), TypeIssued);
    m_cmbDrawingType->addItem(QStringLiteral("会签图"), TypeCountersign);

    lay->addWidget(m_searchEdit);
    lay->addWidget(m_btnSearch);
    lay->addWidget(m_cmbDrawingType);

    lay->addStretch();

    // 搜索框右侧红色叉号：点一下清空并恢复全部列表；无输入时不显示
    auto *clearAct = new QAction(makeClearSearchIcon(),
                                 QStringLiteral("清除搜索内容"), m_searchEdit);
    clearAct->setToolTip(QStringLiteral("清除搜索内容"));
    clearAct->setVisible(false);
    m_searchEdit->addAction(clearAct, QLineEdit::TrailingPosition);
    // 显式开启内部按钮的鼠标跟踪，保证 QSS :hover 与图标 Active 状态都能生效
    for (QObject *o : clearAct->associatedObjects()) {
        if (QWidget *w = qobject_cast<QWidget *>(o)) {
            w->setAttribute(Qt::WA_Hover, true);
            w->setCursor(Qt::PointingHandCursor);
        }
    }
    connect(clearAct, &QAction::triggered, this, [this] {
        m_searchEdit->clear();
        m_searchEdit->setFocus();
        onSearch(); // 清空后立刻恢复全部列表，避免表格还停留在过滤结果
    });
    connect(m_searchEdit, &QLineEdit::textChanged, this,
            [clearAct](const QString &s) { clearAct->setVisible(!s.isEmpty()); });

    connect(m_btnSearch, &QPushButton::clicked, this, &MainWindow::onSearch);
    connect(m_searchEdit, &QLineEdit::returnPressed, this, &MainWindow::onSearch);
    connect(m_cmbDrawingType, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &MainWindow::onSearch);
}

// ---------- 主体 ----------
void MainWindow::buildBody()
{
    QWidget *body = new QWidget(this);
    body->setObjectName(QStringLiteral("bodyWid"));
    QVBoxLayout *lay = new QVBoxLayout(body);
    lay->setContentsMargins(10, 8, 10, 10);
    lay->setSpacing(8);

    QSplitter *split = new QSplitter(Qt::Horizontal, body);

    // 左列：产品列表 + 底部备注/更改记录。
    // 放进同一列后，备注与更改记录的总宽度始终等于左侧表格宽度，拖动分割条也不会错位。
    QWidget *leftCol = new QWidget(split);
    QVBoxLayout *lcolLay = new QVBoxLayout(leftCol);
    lcolLay->setContentsMargins(0, 0, 0, 0);
    lcolLay->setSpacing(8);

    // 左：产品列表
    m_table = new QTableWidget(leftCol);
    m_table->setObjectName(QStringLiteral("productTable"));
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->verticalHeader()->setVisible(false);
    m_table->verticalHeader()->setDefaultSectionSize(26); // 字号调小后行高同步收紧
    connect(m_table, &QTableWidget::cellClicked, this, &MainWindow::onRowSelected);
    // 双击行复制整行；右键弹出菜单按列复制
    connect(m_table, &QTableWidget::cellDoubleClicked, this, &MainWindow::onTableDoubleClicked);
    m_table->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_table, &QTableWidget::customContextMenuRequested, this, &MainWindow::onTableContextMenu);
    lcolLay->addWidget(m_table, 1);

    // 右：图纸预览
    QWidget *right = new QWidget(split);
    QVBoxLayout *rlay = new QVBoxLayout(right);
    rlay->setContentsMargins(8, 0, 0, 0);
    rlay->setSpacing(6);
    // 图纸类型：预览区只保留这一处展示（下发图 / 会签图 / 待补充）+ 补充信息入口
    QHBoxLayout *chipLay = new QHBoxLayout();
    m_dtypeLabel = new QLabel(QStringLiteral("图纸类型：待补充"), right);
    m_dtypeLabel->setObjectName(QStringLiteral("dtypeLabel"));
    chipLay->addWidget(m_dtypeLabel);
    chipLay->addStretch();
    m_btnSupplement = new QPushButton(QStringLiteral("补充信息"), right);
    m_btnSupplement->setObjectName(QStringLiteral("btnEntry"));
    m_btnSupplement->setCursor(Qt::PointingHandCursor);
    m_btnSupplement->setEnabled(false);
    m_btnSupplement->setToolTip(QStringLiteral("把该条记录已有信息带入录入页补充或修改"));
    chipLay->addWidget(m_btnSupplement);
    rlay->addLayout(chipLay);

    m_previewStack = new QStackedWidget(right);
    // 页0：图片预览（也用作占位提示）
    m_previewLabel = new QLabel(m_previewStack);
    m_previewLabel->setObjectName(QStringLiteral("preview"));
    m_previewLabel->setAlignment(Qt::AlignCenter);
    m_previewLabel->setMinimumHeight(280);
    m_previewLabel->setText(QStringLiteral("暂无图纸"));
    m_previewStack->addWidget(m_previewLabel);
    // 页1：PDF 内置预览
    m_pdfDoc = new QPdfDocument(this);
    m_pdfView = new QPdfView(m_previewStack);
    m_pdfView->setObjectName(QStringLiteral("preview"));
    m_pdfView->setDocument(m_pdfDoc);
    m_pdfView->setPageMode(QPdfView::PageMode::SinglePage);
    m_pdfView->setZoomMode(QPdfView::ZoomMode::FitInView);
    m_previewStack->addWidget(m_pdfView);
    rlay->addWidget(m_previewStack, 1);
    m_previewInfo = new QLabel(QStringLiteral("双击预览区可用系统默认程序打开"), right);
    m_previewInfo->setObjectName(QStringLiteral("previewInfo"));
    m_previewInfo->setAlignment(Qt::AlignCenter);
    rlay->addWidget(m_previewInfo);

    m_previewStack->installEventFilter(this); // 双击外部打开，见 eventFilter
    m_previewLabel->installEventFilter(this); // 尺寸变化后重缩放图片
    m_pdfView->installEventFilter(this);
    m_pdfView->viewport()->installEventFilter(this);
    split->addWidget(leftCol);
    split->addWidget(right);
    split->setChildrenCollapsible(false);
    m_split = split;
    // 左右强制比例：普通窗口 3:2，最大化/全屏 1:1（见 applySplitRatio）
    split->setStretchFactor(0, 3);
    split->setStretchFactor(1, 2);
    lay->addWidget(split, 1);

    // 底部左：特有字段 / 备注 / 更改记录（三栏宽度合计等于左侧表格宽度）
    QHBoxLayout *detailLay = new QHBoxLayout();
    detailLay->setSpacing(8);

    m_extFieldView = new QTextBrowser(leftCol);
    m_extFieldView->setObjectName(QStringLiteral("detailBlk"));
    // 版本号这类长串中间没有空格，按单词换行不会断，改成允许在任意位置折行
    QTextOption extOpt = m_extFieldView->document()->defaultTextOption();
    extOpt.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
    m_extFieldView->document()->setDefaultTextOption(extOpt);
    m_remarkView = new QTextBrowser(leftCol);
    m_remarkView->setObjectName(QStringLiteral("detailBlk"));
    m_changeLogView = new QTextBrowser(leftCol);
    m_changeLogView->setObjectName(QStringLiteral("detailBlk"));
    // 三栏等高，标题都在框内（由 HTML 首行绘制，与「更多图纸」同款）
    // 高度 140：特有字段的值（版本号一类长串）折行后也能完整显示
    m_extFieldView->setFixedHeight(140);
    m_remarkView->setFixedHeight(140);
    m_changeLogView->setFixedHeight(140);
    // 宽度由左列决定，不让文档默认宽度撑大左列（否则表格右侧又会留白）
    m_extFieldView->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    m_remarkView->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    m_changeLogView->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    // 特有字段列略窄（通常 1~4 个短值），备注/更改记录各占一份
    detailLay->addWidget(m_extFieldView, 1);
    detailLay->addWidget(m_remarkView, 1);
    detailLay->addWidget(m_changeLogView, 1);
    lcolLay->addLayout(detailLay);

    // 底部右：更多图纸（放在预览栏内，宽度与上方预览框一致）
    // 同一产品的全部图纸缩略图，点选切换上方预览
    QFrame *moreBox = new QFrame(right);
    moreBox->setObjectName(QStringLiteral("detailBlk")); // 与左侧两栏同一外框样式
    moreBox->setFixedHeight(140); // 与左列三栏等高
    QVBoxLayout *moreLay = new QVBoxLayout(moreBox);
    moreLay->setContentsMargins(0, 0, 0, 0); // 内边距交给 #detailBlk 的 padding
    moreLay->setSpacing(2);
    m_moreTitle = new QLabel(QStringLiteral("更多图纸"), moreBox);
    m_moreTitle->setObjectName(QStringLiteral("detailTitle"));
    m_moreTitle->hide(); // 无选中行时与另两栏一样保持空白
    moreLay->addWidget(m_moreTitle);
    m_moreDrawings = new QListWidget(moreBox);
    m_moreDrawings->setObjectName(QStringLiteral("moreList")); // 透明、无边框，融入外框
    m_moreDrawings->setViewMode(QListView::IconMode);
    m_moreDrawings->setFlow(QListView::LeftToRight);
    m_moreDrawings->setWrapping(false);
    m_moreDrawings->setMovement(QListView::Static);
    m_moreDrawings->setResizeMode(QListView::Adjust);
    m_moreDrawings->setIconSize(QSize(68, 46));
    m_moreDrawings->setGridSize(QSize(78, 72));
    m_moreDrawings->setSpacing(2);
    m_moreDrawings->setUniformItemSizes(true);
    m_moreDrawings->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_moreDrawings->setTextElideMode(Qt::ElideMiddle);
    m_moreDrawings->setFrameShape(QFrame::NoFrame);
    moreLay->addWidget(m_moreDrawings, 1);
    rlay->addWidget(moreBox);

    // 点缩略图 → 上方预览切换为该图纸
    connect(m_moreDrawings, &QListWidget::itemClicked, this, [this](QListWidgetItem *it) {
        const int idx = it ? it->data(Qt::UserRole).toInt() : -1;
        if (idx >= 0 && idx < m_drawings.size())
            previewDrawing(m_drawings.at(idx));
    });

    connect(m_btnSupplement, &QPushButton::clicked, this, &MainWindow::onSupplementClicked);
}

// 预览区双击 → 系统默认程序打开图纸
bool MainWindow::eventFilter(QObject *watched, QEvent *event)
{
    // 预览框尺寸变化（首次进入分类、窗口缩放、拖动分割条）时按新尺寸重缩放图片
    if (watched == m_previewLabel && event->type() == QEvent::Resize)
        updatePreviewPixmap();
    if ((watched == m_pdfView || watched == m_pdfView->viewport()) && event->type() == QEvent::Resize)
        QTimer::singleShot(0, this, [this] { refitPdfView(); });
    if (watched == m_pwdOverlay && event->type() == QEvent::MouseButtonRelease) {
        m_pwdOverlay->hide();
        return true;
    }
    if (watched == m_loginLabel && event->type() == QEvent::MouseButtonRelease) {
        if (m_currentUser.isEmpty()) {
            // 未登录：打开登录卡，登录后回主界面（不再跳设置页，避免一登录就落到「服务器/客户端」切换）
            m_pwdTargetPage = 0;
            m_pwdTitle->setText(QStringLiteral("用户登录"));
            m_userEdit->clear();
            m_pwdEdit->clear();
            m_pwdError->clear();
            m_pwdOverlay->showOverlay();
            m_userEdit->setFocus();
        } else {
            // 已登录：确认退出
            if (NdMessageBox::question(this, QStringLiteral("退出登录"),
                                       QStringLiteral("确定退出当前账号 %1？").arg(m_currentUser))) {
                m_currentUser.clear();
                m_isAdmin = false;
                DatabaseManager::instance().setCurrentOperator(QString());
                m_entryPage->setCurrentUser(QString());
                updateLoginLabel();
                m_stack->setCurrentIndex(0);
                m_menuGroup->button(0)->setChecked(true);
            }
        }
        return true;
    }
    if ((watched == m_previewStack || watched == m_previewLabel || watched == m_pdfView
        || watched == m_pdfView->viewport())
        && event->type() == QEvent::MouseButtonDblClick) {
        onPreviewDoubleClicked();
        return true;
    }
    // 图纸预览框右键：用资源管理器打开所在路径
    if ((watched == m_previewStack || watched == m_previewLabel || watched == m_pdfView
        || watched == m_pdfView->viewport())
        && event->type() == QEvent::ContextMenu) {
        auto *ce = static_cast<QContextMenuEvent *>(event);
        onPreviewContextMenu(ce->globalPos());
        return true;
    }
    if (watched == m_serverInfo && event->type() == QEvent::MouseButtonPress) {
        if (!m_accessUrl.isEmpty())
            QDesktopServices::openUrl(QUrl(m_accessUrl)); // 直接打开网页查询
        return true;
    }
    if (watched == m_versionLabel && event->type() == QEvent::MouseButtonPress) {
        UpdateDialog::checkForUpdate(this);
        return true;
    }
    return QWidget::eventFilter(watched, event);
}

void MainWindow::onCategoryChanged(int index)
{
    // id=0 →「所有类别」：不限分类；id=1..N → 对应各大分类
    m_currentCat = (index <= 0) ? QString()
                                : DatabaseManager::categories().value(index - 1);
    onSearch();
}

void MainWindow::onSearch() { reloadTable(); }

void MainWindow::reloadTable()
{
    const int dtype = m_cmbDrawingType->currentData().toInt();
    const QString kw = m_searchEdit->text().trimmed();
    // 搜索范围由选中的页签决定：「所有类别」搜索全部分类，具体分类只搜该分类
    const bool all = m_currentCat.isEmpty();
    m_products = DatabaseManager::instance().searchProducts(m_currentCat, kw, dtype);

    // 特有字段列：仅选中具体分类时按该分类配置显示。
    // 「所有类别」下不显示特有字段；分类没有的字段（如“开关”无软件版本号）也不会出现。
    const QList<FieldConfig> extCols = all
        ? QList<FieldConfig>()
        : DatabaseManager::instance().fieldConfigs(m_currentCat);

    // 列：序号 物料编码 名称 型号 [分类] [特有字段...] 图纸 图纸时间 录入时间 备注
    QStringList headers;
    headers << QStringLiteral("序号") << QStringLiteral("物料编码") << QStringLiteral("名称")
            << QStringLiteral("型号");
    if (all)
        headers << QStringLiteral("分类"); // 「所有类别」下标明每条来源分类
    for (const FieldConfig &f : extCols)
        headers << f.fieldLabel;
    headers << QStringLiteral("图纸") << QStringLiteral("图纸时间")
            << QStringLiteral("录入时间") << QStringLiteral("备注");

    m_table->clear();
    m_table->setColumnCount(headers.size());
    m_table->setHorizontalHeaderLabels(headers);
    m_table->setRowCount(m_products.size());

    for (int r = 0; r < m_products.size(); ++r) {
        const Product &p = m_products.at(r);
        int c = 0;
        auto set = [&](const QString &text) {
            // 单元格内容统一居中
            auto *it = new QTableWidgetItem(text);
            it->setTextAlignment(Qt::AlignCenter);
            m_table->setItem(r, c++, it);
        };
        set(QString::number(r + 1));
        set(p.materialCode);
        set(p.name);
        set(p.model);
        if (all)
            set(p.category);
        for (const FieldConfig &f : extCols)
            set(p.extFields.value(f.fieldKey));
        set(p.hasDrawing ? QStringLiteral("●") : QStringLiteral("—"));
        // 图纸时间优先展示，无图纸则显示录入日期
        set(p.drawingDate.isValid() ? p.drawingDate.toString(QStringLiteral("yyyy-MM-dd"))
                                    : p.createdAt.toString(QStringLiteral("yyyy-MM-dd")));
        set(p.createdAt.toString(QStringLiteral("yyyy-MM-dd hh:mm")));
        set(p.remark);
    }
    m_table->resizeColumnsToContents();

    m_previewStack->setCurrentWidget(m_previewLabel);
    m_previewPix = QPixmap();
    m_previewLabel->setText(QStringLiteral("暂无图纸"));
    m_previewLabel->setPixmap(QPixmap());
    m_extFieldView->clear();
    m_remarkView->clear();
    m_changeLogView->clear();
    m_moreDrawings->clear();
    m_drawings.clear();
    m_moreTitle->hide(); // 无选中行时三栏都保持空白

    // 刷新后无选中行：补充信息按钮禁用，图纸类型回到待补充
    m_selectedProductId = -1;
    m_btnSupplement->setEnabled(false);
    m_dtypeLabel->setText(QStringLiteral("图纸类型：待补充"));

    // 列数/列宽变了（切换分类会增减特有字段列），下一轮事件循环按新宽度重新分配左右
    QTimer::singleShot(0, this, [this] { applySplitRatio(); });
}

// 左右强制比例：普通窗口 3:2，最大化/全屏 1:1。
// 窗口尺寸一变就按比例重排，保证列表不会被表格列宽撑得忽宽忽窄。
void MainWindow::applySplitRatio()
{
    if (!m_split)
        return;
    const int avail = m_split->width() - m_split->handleWidth();
    if (avail <= 200)
        return;

    const bool full = isMaximized() || isFullScreen();
    const int lw = full ? 1 : 3;
    const int rw = full ? 1 : 2;
    m_split->setStretchFactor(0, lw);
    m_split->setStretchFactor(1, rw);

    const int left = avail * lw / (lw + rw);
    const QList<int> cur = m_split->sizes();
    if (cur.size() == 2 && qAbs(cur.at(0) - left) <= 2
        && qAbs(cur.at(1) - (avail - left)) <= 2)
        return; // 已经是目标比例，避免反复重排

    m_split->setSizes({left, avail - left});
}

void MainWindow::onRowSelected(int row, int column)
{
    Q_UNUSED(column);
    if (row < 0 || row >= m_products.size()) return;
    m_selectedProductId = m_products.at(row).id;
    m_btnSupplement->setEnabled(true); // 选中行后才允许补充信息
    showProductDetail(m_selectedProductId);
}

// 双击行：复制整行数据（制表符分隔，可直接粘贴到 Excel）
void MainWindow::onTableDoubleClicked(int row, int column)
{
    Q_UNUSED(column);
    if (row < 0 || row >= m_table->rowCount()) return;
    QStringList cells;
    for (int c = 0; c < m_table->columnCount(); ++c) {
        const QTableWidgetItem *it = m_table->item(row, c);
        cells << (it ? it->text() : QString());
    }
    QGuiApplication::clipboard()->setText(cells.join(QChar('\t')));
    QToolTip::showText(QCursor::pos(), QStringLiteral("已复制整行数据"), m_table);
}

// 右键表格：选中该行，并可按列复制对应的单元格数据
void MainWindow::onTableContextMenu(const QPoint &pos)
{
    Q_UNUSED(pos); // 统一以鼠标全局位置换算，避免不同平台坐标系差异
    const QPoint vp = m_table->viewport()->mapFromGlobal(QCursor::pos());
    const int row = m_table->rowAt(vp.y());
    if (row < 0 || row >= m_table->rowCount()) return;
    const int clickedCol = m_table->columnAt(vp.x());

    // 右键同时选中该行，右侧详情/预览随之刷新
    m_table->selectRow(row);
    if (row < m_products.size())
        onRowSelected(row, qMax(0, clickedCol));

    RoundedMenu menu(this);
    QAction *aRow = menu.addAction(QStringLiteral("复制整行数据"));
    menu.addSeparator();
    QList<QAction *> colActs;
    for (int c = 0; c < m_table->columnCount(); ++c) {
        const QTableWidgetItem *h = m_table->horizontalHeaderItem(c);
        const QString name = h ? h->text() : QString::number(c + 1);
        QAction *a = menu.addAction(QStringLiteral("复制「%1」").arg(name));
        if (c == clickedCol) { // 高亮当前右键所在列
            QFont f = a->font();
            f.setBold(true);
            a->setFont(f);
        }
        colActs << a;
    }

    QAction *chosen = menu.exec(QCursor::pos());
    if (!chosen) return;

    QString text;
    if (chosen == aRow) {
        QStringList cells;
        for (int c = 0; c < m_table->columnCount(); ++c) {
            const QTableWidgetItem *it = m_table->item(row, c);
            cells << (it ? it->text() : QString());
        }
        text = cells.join(QChar('\t'));
    } else {
        const int c = colActs.indexOf(chosen);
        if (c < 0) return;
        const QTableWidgetItem *it = m_table->item(row, c);
        text = it ? it->text() : QString();
    }
    QGuiApplication::clipboard()->setText(text);
    QToolTip::showText(QCursor::pos(), QStringLiteral("已复制"), m_table);
}

// 补充信息：把已有数据带入录入页（编辑模式），保存后更新修改时间
void MainWindow::onSupplementClicked()
{
    if (m_selectedProductId < 0) return;
    if (m_currentUser.isEmpty()) {
        NdMessageBox::information(this, QStringLiteral("提示"),
                                 QStringLiteral("请先登录后再补充或修改图纸信息"));
        return;
    }
    m_entryPage->loadProduct(m_selectedProductId);
    m_stack->setCurrentIndex(1);
    m_menuGroup->button(1)->setChecked(true);
}

void MainWindow::showProductDetail(int productId)
{
    DatabaseManager &db = DatabaseManager::instance();
    const Product p = db.productById(productId);

    // 详情：分类特有字段（如控制器类的护套针数 / 硬件版本号 / 软件版本号）
    // 用 <br/> 逐行拼接，与备注/更改记录一致；用 <div> 会带段落间距，首行前多出空行
    const QList<FieldConfig> exts = DatabaseManager::instance().fieldConfigs(p.category);
    QStringList extLines;
    for (const FieldConfig &f : exts) {
        const QString v = p.extFields.value(f.fieldKey);
        extLines << QStringLiteral("%1：%2")
                        .arg(f.fieldLabel.toHtmlEscaped(),
                             v.isEmpty() ? QStringLiteral("<span style='color:#666'>—</span>")
                                         : v.toHtmlEscaped());
    }
    QString extHtml = QStringLiteral("<b style='color:#8a90a6'>特有字段</b><br/>");
    extHtml += extLines.isEmpty() ? QStringLiteral("<span style='color:#666'>无</span>")
                                  : extLines.join(QStringLiteral("<br/>"));
    m_extFieldView->setHtml(extHtml);

    // 详情：备注
    QString remarkHtml = QStringLiteral("<b style='color:#8a90a6'>备注</b><br/>%1")
                             .arg(p.remark.toHtmlEscaped());
    m_remarkView->setHtml(remarkHtml);

    QString logHtml = QStringLiteral("<b style='color:#8a90a6'>更改记录</b><br/>");
    const QList<ChangeLog> logs = db.changeLogsOf(productId);
    if (logs.isEmpty())
        logHtml += QStringLiteral("<span style='color:#666'>暂无</span>");
    for (const ChangeLog &c : logs) {
        logHtml += QStringLiteral("<div>%1　%2 —— %3</div>")
                       .arg(c.createdAt.toString(QStringLiteral("yyyy-MM-dd")),
                            c.content.toHtmlEscaped(), c.oper.toHtmlEscaped());
    }
    m_changeLogView->setHtml(logHtml);

    // 更多图纸：列出该产品全部图纸（下发图 + 会签图），缩略图点选切换上方预览
    m_moreTitle->show(); // 与另两栏一致：选中记录后标题显示在框内
    m_drawings = db.drawingsOf(productId);
    m_moreDrawings->clear();
    const int thumbLimit = 8; // 只为前几张生成缩略图，图纸很多时避免卡顿
    for (int i = 0; i < m_drawings.size(); ++i) {
        const Drawing &dg = m_drawings.at(i);
        auto *it = new QListWidgetItem(m_moreDrawings);
        it->setData(Qt::UserRole, i);
        it->setText(QStringLiteral("图纸%1").arg(i + 1));
        it->setTextAlignment(static_cast<int>(Qt::AlignHCenter | Qt::AlignTop));
        it->setToolTip(QStringLiteral("%1\n%2\n%3")
                           .arg(QFileInfo(dg.filePath).fileName(),
                                dg.dtype == TypeCountersign ? QStringLiteral("会签图")
                                                            : QStringLiteral("下发图"),
                                dg.drawingDate.isValid()
                                    ? dg.drawingDate.toString(QStringLiteral("yyyy-MM-dd"))
                                    : QStringLiteral("未标注日期")));

        QPixmap thumb;
        if (i < thumbLimit) {
            if (dg.format == QStringLiteral("pdf")) {
                QPdfDocument doc;
                if (doc.load(dg.filePath) == QPdfDocument::Error::None && doc.pageCount() > 0) {
                    const QSizeF ps = doc.pagePointSize(0);
                    QSize target(88, 64);
                    if (ps.width() > 0 && ps.height() > 0)
                        target = ps.scaled(QSizeF(88, 64), Qt::KeepAspectRatio).toSize();
                    const QImage img = doc.render(0, target);
                    if (!img.isNull()) {
                        // PDF 页面本身没有背景色，渲染结果是透明底；
                        // 直接贴到深色列表上会变成“深色缩略图”，这里合到白纸上
                        QImage canvas(img.size(), QImage::Format_RGB32);
                        canvas.fill(Qt::white);
                        QPainter p(&canvas);
                        p.drawImage(0, 0, img);
                        p.end();
                        thumb = QPixmap::fromImage(canvas);
                    }
                }
            } else {
                QImageReader r(dg.filePath);
                r.setAutoTransform(true);
                const QImage im = r.read();
                if (!im.isNull())
                    thumb = QPixmap::fromImage(im.scaled(QSize(88, 64), Qt::KeepAspectRatio,
                                                         Qt::SmoothTransformation));
            }
        }
        it->setIcon(thumb.isNull() ? style()->standardIcon(QStyle::SP_FileIcon) : QIcon(thumb));
    }

    if (m_drawings.isEmpty()) {
        m_dtypeLabel->setText(QStringLiteral("图纸类型：待补充"));
        m_previewStack->setCurrentWidget(m_previewLabel);
        m_previewPix = QPixmap();
        m_previewLabel->setText(QStringLiteral("暂无图纸"));
        m_previewLabel->setPixmap(QPixmap());
        m_currentPreviewFile.clear();
        m_previewInfo->setText(QStringLiteral("暂无图纸"));
        return;
    }
    m_moreDrawings->setCurrentRow(0);
    previewDrawing(m_drawings.first());
}

// 把指定图纸加载到右侧预览区（默认首张与缩略图点选共用）
void MainWindow::previewDrawing(const Drawing &d)
{
    m_dtypeLabel->setText(QStringLiteral("图纸类型：%1").arg(
        d.dtype == TypeCountersign ? QStringLiteral("会签图") : QStringLiteral("下发图")));
    m_currentPreviewFile = d.filePath;
    if (d.format == QStringLiteral("pdf")) {
        // PDF 内置预览（Qt PDF）；图片原图一并清掉，避免预览框尺寸变化时
        // updatePreviewPixmap() 又把上一张图片贴回图片页
        m_previewPix = QPixmap();
        if (m_pdfDoc->load(d.filePath) == QPdfDocument::Error::None) {
            m_pdfFitSize = QSize(); // 新文档，下一次按当前尺寸重新适配
            m_previewStack->setCurrentWidget(m_pdfView);
            QTimer::singleShot(0, this, [this] { refitPdfView(); });
        }
        else {
            m_previewLabel->setPixmap(QPixmap());
            m_previewLabel->setText(QStringLiteral("PDF 加载失败"));
            m_previewStack->setCurrentWidget(m_previewLabel);
        }
    } else {
        // 按文件里的 EXIF 方向标记自动转正（手机拍的 JPEG 常带旋转标记）
        QImageReader r(d.filePath);
        r.setAutoTransform(true);
        const QPixmap pm = QPixmap::fromImage(r.read());
        m_previewPix = pm; // 空图时一并清掉，避免沿用上一张
        m_previewStack->setCurrentWidget(m_previewLabel);
        // 切换图纸必须先清掉 label 上的旧图：updatePreviewPixmap() 以「缩放后
        // 尺寸没变」为跳过条件，两张同尺寸图纸切换时会误判而一直显示上一张。
        m_previewLabel->setPixmap(QPixmap());
        if (pm.isNull())
            m_previewLabel->setText(QStringLiteral("图纸加载失败"));
        else
            updatePreviewPixmap();
    }
    m_previewInfo->setText(QStringLiteral("%1　图纸时间：%2　双击打开大图")
                               .arg(QFileInfo(d.filePath).fileName(),
                                    d.drawingDate.toString(QStringLiteral("yyyy-MM-dd"))));
}

// 按预览框“当前实际尺寸”缩放原图。
// 刚进入分类时右列宽度才由 applySplitRatio() 调整过，预览框还没完成布局，
// 此时按旧尺寸缩放的图会明显偏小；这里统一切换到图片页后再缩放，并在预览框
// 尺寸变化（窗口缩放、分割条拖动）时自动重算。
void MainWindow::updatePreviewPixmap()
{
    if (!m_previewLabel || m_previewPix.isNull())
        return;

    const QSize box = m_previewLabel->size() - QSize(8, 8);
    if (box.width() <= 0 || box.height() <= 0) {
        // 布局尚未完成，等下一轮事件循环再算
        QTimer::singleShot(0, this, [this] { updatePreviewPixmap(); });
        return;
    }

    const QPixmap scaled = m_previewPix.scaled(box, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    const QPixmap cur = m_previewLabel->pixmap(Qt::ReturnByValue);
    // 仅「当前已有图 + 尺寸没变」时跳过，避免 setPixmap 引发重复重绘；
    // 切换图纸时 previewDrawing() 会先清空 label，保证这里必定刷新。
    if (!cur.isNull() && cur.size() == scaled.size() && m_previewLabel->text().isEmpty())
        return;

    m_previewLabel->setText(QString());
    m_previewLabel->setPixmap(scaled);
}

// QPdfView 的 FitInView 只在文档加载那一刻按 view 当时的尺寸算页面比例，
// 之后预览框变大（首次进入分类、窗口缩放）不会重算，PDF 页面就显得很小；
// 这里按当前尺寸强制重排一次。尺寸没变则跳过，避免反复重排。
void MainWindow::refitPdfView()
{
    if (!m_pdfView || !m_pdfDoc || m_pdfDoc->pageCount() <= 0)
        return;
    if (m_pdfView->zoomMode() != QPdfView::ZoomMode::FitInView)
        return; // 用户自己缩放过了，不再干预

    const QSize vp = m_pdfView->viewport()->size();
    if (vp == m_pdfFitSize)
        return;
    m_pdfFitSize = vp;

    m_pdfView->setZoomFactor(1.0);
    m_pdfView->setZoomMode(QPdfView::ZoomMode::FitInView);
}

void MainWindow::onPreviewDoubleClicked()
{
    if (!m_currentPreviewFile.isEmpty())
        QDesktopServices::openUrl(QUrl::fromLocalFile(m_currentPreviewFile));
}

// 右键图纸框：用资源管理器打开对应图纸所在路径
void MainWindow::onPreviewContextMenu(const QPoint &globalPos)
{
    const QPoint gp = globalPos.isNull() ? QCursor::pos() : globalPos;
    if (m_currentPreviewFile.isEmpty()) {
        QToolTip::showText(gp, QStringLiteral("当前没有可打开的图纸"), this);
        return;
    }
    // 客户端模式下图纸只按需缓存在本机，第二项改为「清理客户端缓存」
    const bool clientMode = !DatabaseManager::isServer();

    RoundedMenu menu(this);
    QAction *aOpen = menu.addAction(QStringLiteral("用默认程序打开图纸"));
    QAction *aSecond = clientMode
        ? menu.addAction(QStringLiteral("清理客户端缓存"))
        : menu.addAction(QStringLiteral("在资源管理器中打开所在文件夹"));
    QAction *aCopy = menu.addAction(QStringLiteral("复制图纸完整路径"));
    QAction *chosen = menu.exec(gp);
    if (!chosen) return;
    if (chosen == aOpen) {
        QDesktopServices::openUrl(QUrl::fromLocalFile(m_currentPreviewFile));
    } else if (chosen == aCopy) {
        QGuiApplication::clipboard()->setText(
            QDir::toNativeSeparators(m_currentPreviewFile));
        QToolTip::showText(gp, QStringLiteral("已复制图纸路径"), this);
    } else if (chosen == aSecond && clientMode) {
        clearClientCache();
    } else if (chosen == aSecond) {
        // explorer 的 /select,<路径> 必须原样传给 explorer：若把整个参数（含 /select,）
        // 交给 QProcess，路径带空格时 Qt 会给整串加引号，explorer 识别不到 /select 前缀，
        // 便会退回到默认目录（表现就是“打开的文件夹不是图纸所在路径”）。
        // 这里只给路径本身加引号，命令行形如 explorer.exe /select,"D:\...\xxx.jpg"
        const QString native = QDir::toNativeSeparators(m_currentPreviewFile);
        // 必须把 /select, 与路径拆成两个参数：QProcess 只会给含空格的路径单独加引号，
        // 生成 “explorer.exe /select, "E:\...\file.jpg"”，explorer 才能正确识别 /select, 前缀；
        // 若合成一个 token（如 "/select,\"path\""），QProcess 会把整串用引号包起来，
        // explorer 解析不到 /select 前缀，便会退回默认目录（文档）。
        if (!QProcess::startDetached(QStringLiteral("explorer.exe"),
                                     QStringList() << QStringLiteral("/select,") << native)) {
            // 极端情况下启动失败：直接打开所在文件夹
            QDesktopServices::openUrl(
                QUrl::fromLocalFile(QFileInfo(m_currentPreviewFile).absolutePath()));
        }
    }
}

// 客户端：清理本机图纸预览缓存（图纸都保存在服务器，本机文件可随时重新下载）
void MainWindow::clearClientCache()
{
    if (!NdMessageBox::question(this, QStringLiteral("清理客户端缓存"),
            QStringLiteral("将删除本机缓存的图纸文件。\n"
                           "服务器上的图纸不受影响，下次查看时按需重新下载。\n确定清理？"),
            true)) // 蓝色确认
        return;

    if (m_pdfDoc) m_pdfDoc->close(); // 先解除 PDF 对缓存文件的占用，否则删不掉

    QString err;
    qint64 bytes = 0;
    const int n = RemoteApiClient::clearDrawingCache(&err, &bytes);

    // 当前预览 / 缩略图指向的文件已被删除，清空避免再次打开失效
    m_currentPreviewFile.clear();
    m_drawings.clear();
    m_moreDrawings->clear();
    m_dtypeLabel->setText(QStringLiteral("图纸类型：待补充"));
    m_previewStack->setCurrentWidget(m_previewLabel);
    m_previewPix = QPixmap();
    m_previewLabel->setPixmap(QPixmap());
    m_previewLabel->setText(QStringLiteral("缓存已清理"));
    m_previewInfo->setText(QStringLiteral("本机图纸缓存已清理"));

    const QString sizeText = bytes >= 1024 * 1024
        ? QStringLiteral("%1 MB").arg(bytes / 1024.0 / 1024.0, 0, 'f', 1)
        : QStringLiteral("%1 KB").arg(bytes / 1024.0, 0, 'f', 0);
    if (!err.isEmpty())
        NdMessageBox::warning(this, QStringLiteral("清理完成"),
                              QStringLiteral("已删除 %1 个缓存文件（%2）。\n%3")
                                  .arg(n).arg(sizeText, err));
    else if (n > 0)
        NdMessageBox::information(this, QStringLiteral("清理完成"),
                                  QStringLiteral("已删除 %1 个缓存文件，释放 %2。")
                                      .arg(n).arg(sizeText));
    else
        NdMessageBox::information(this, QStringLiteral("清理完成"),
                                  QStringLiteral("本机还没有缓存文件，无需清理。"));
}

// 标题栏拖动
void MainWindow::mousePressEvent(QMouseEvent *event)
{
    QWidget *bar = findChild<QWidget *>(QStringLiteral("titleBar"));
    if (bar && bar->geometry().contains(event->pos()) && event->button() == Qt::LeftButton) {
        m_dragging = true;
        m_dragPos = event->globalPosition().toPoint() - frameGeometry().topLeft();
    }
    QWidget::mousePressEvent(event);
}

void MainWindow::mouseMoveEvent(QMouseEvent *event)
{
    if (m_dragging && (event->buttons() & Qt::LeftButton))
        move(event->globalPosition().toPoint() - m_dragPos);
    QWidget::mouseMoveEvent(event);
}

void MainWindow::mouseReleaseEvent(QMouseEvent *event)
{
    m_dragging = false;
    QWidget::mouseReleaseEvent(event);
}


// 切换角色后必须重启：服务是否启动、数据库走本地 SQLite 还是服务器，都在启动时决定
void MainWindow::restartApplication()
{
    // 按钮在 200ms 延迟内仍可能被连点，重复调用会拉起多个新进程（表现为打开两个界面）
    if (m_restarting)
        return;
    m_restarting = true;

    // 停掉 1 秒一次的同步状态刷新，避免退出过程被定时器/网络请求拖住
    if (m_syncTimer)
        m_syncTimer->stop();

    // 旧窗口先隐藏，新进程起来之前不会同时看到两个界面
    hide();

    QTimer::singleShot(200, qApp, [this] {
        const QString exe = QCoreApplication::applicationFilePath();
        const QStringList args = QCoreApplication::arguments().mid(1);
        if (!QProcess::startDetached(exe, args)) {
            m_restarting = false;
            show();
            NdMessageBox::warning(this, QStringLiteral("重启失败"),
                                  QStringLiteral("无法自动重启程序，请手动关闭后重新打开。"));
            return;
        }
        // 客户端模式下可能有正在等待响应的同步请求，普通 quit() 会被嵌套事件循环拖住，
        // 旧进程残留就变成「两个界面」；这里再加一道强制退出兜底
        QTimer::singleShot(300, qApp, [] { std::_Exit(0); });
        QCoreApplication::quit();
    });
}

// ---------- 登录/验证：黑色半透明遮罩 + 账号密码卡 ----------
void MainWindow::buildLoginOverlay()
{
    m_pwdOverlay = new OverlayMask(this);
    m_pwdOverlay->enableOverlay(); // 全屏半透明黑遮罩，与其它弹窗统一
    m_pwdOverlay->hide();

    QGridLayout *lay = new QGridLayout(m_pwdOverlay);
    lay->setContentsMargins(0, 0, 0, 0);

    QWidget *card = new QWidget(m_pwdOverlay);
    card->setObjectName(QStringLiteral("aboutCard"));
    card->setFixedSize(420, 360);
    QVBoxLayout *cl = new QVBoxLayout(card);
    cl->setContentsMargins(30, 24, 30, 22);
    cl->setSpacing(10);

    m_pwdTitle = new QLabel(QStringLiteral("用户登录"), card);
    m_pwdTitle->setObjectName(QStringLiteral("aboutTitle"));
    m_pwdTitle->setAlignment(Qt::AlignCenter);

    m_userEdit = new QLineEdit(card);
    m_userEdit->setPlaceholderText(QStringLiteral("账号"));
    m_pwdEdit = new QLineEdit(card);
    m_pwdEdit->setEchoMode(QLineEdit::Password);
    m_pwdEdit->setPlaceholderText(QStringLiteral("密码"));

    m_pwdError = new QLabel(card);
    m_pwdError->setObjectName(QStringLiteral("pwdError"));
    m_pwdError->setAlignment(Qt::AlignCenter);
    m_pwdError->setWordWrap(true);
    m_pwdError->setMinimumHeight(18);

    QHBoxLayout *btns = new QHBoxLayout();
    btns->addStretch();
    QPushButton *btnCancel = new QPushButton(QStringLiteral("取 消"), card);
    btnCancel->setObjectName(QStringLiteral("btnDanger"));
    btnCancel->setCursor(Qt::PointingHandCursor);
    QPushButton *btnOk = new QPushButton(QStringLiteral("确 定"), card);
    btnOk->setObjectName(QStringLiteral("btnEntry"));
    btnOk->setCursor(Qt::PointingHandCursor);
    btnOk->setDefault(true);
    btns->addWidget(btnCancel);
    btns->addWidget(btnOk);
    btns->addStretch();

    cl->addWidget(m_pwdTitle);
    cl->addSpacing(6);
    cl->addWidget(m_userEdit);
    cl->addWidget(m_pwdEdit);
    cl->addWidget(m_pwdError);
    cl->addLayout(btns);
    // 兜底入口：客户端连不上服务器时账号无法验证，仍可从这里改角色/地址，
    // 不必手工删除注册表里的 system/role
    QPushButton *btnRole = new QPushButton(
        QStringLiteral("切换本机角色（服务器主机 / 客户端）"), card);
    btnRole->setObjectName(QStringLiteral("btnGhost"));
    btnRole->setCursor(Qt::PointingHandCursor);
    cl->addWidget(btnRole);
    lay->addWidget(card, 0, 0, Qt::AlignCenter);

    // 验证：账号密码正确即可录入 / 管理 / 设置；未登录的客户端只能只读查看
    auto verify = [this] {
        const QString user = m_userEdit->text().trimmed();
        const QString role = DatabaseManager::instance().userRole(user, m_pwdEdit->text());
        if (role.isEmpty()) {
            // 客户端区分「账号密码错」和「连不上服务器」，避免用户以为只是密码输错
            const QString remoteErr = DatabaseManager::isServer()
                ? QString() : RemoteApiClient::instance().lastError();
            m_pwdError->setText(remoteErr.isEmpty()
                ? QStringLiteral("账号或密码错误，请重试") : remoteErr);
            m_pwdEdit->clear();
            m_pwdEdit->setFocus();
            return;
        }
        m_pwdOverlay->hide();
        m_currentUser = user;
        m_isAdmin = (role == QLatin1String("admin"));
        DatabaseManager::instance().setCurrentOperator(user);
        m_entryPage->setCurrentUser(user);
        updateLoginLabel();

        if (m_pwdTargetPage == 1) {
            m_entryPage->resetForm();
            m_stack->setCurrentIndex(1);
            m_menuGroup->button(1)->setChecked(true);
        } else if (m_pwdTargetPage == 2) {
            m_managePage->refresh();
            m_stack->setCurrentIndex(2);
            m_menuGroup->button(2)->setChecked(true);
        } else if (m_pwdTargetPage == 3) {
            m_stack->setCurrentIndex(3);
            m_menuGroup->button(3)->setChecked(true);
        } else {
            // 从标题栏「登录」进入：登录成功直接回主界面，不跳服务器/客户端设置
            m_stack->setCurrentIndex(0);
            m_menuGroup->button(0)->setChecked(true);
        }
    };
    connect(btnOk, &QPushButton::clicked, this, verify);
    connect(m_pwdEdit, &QLineEdit::returnPressed, this, verify);
    connect(m_userEdit, &QLineEdit::returnPressed, this, [this] { m_pwdEdit->setFocus(); });
    connect(btnCancel, &QPushButton::clicked, m_pwdOverlay, &QWidget::hide);
    connect(btnRole, &QPushButton::clicked, this, [this] {
        RoleChoice cur;
        cur.role = DatabaseManager::isServer() ? QStringLiteral("server")
                                              : QStringLiteral("client");
        cur.host = DatabaseManager::serverHost();
        cur.port = DatabaseManager::serverPort();
        const RoleChoice r = RoleSelectDialog::choose(cur);
        if (!r.ok) return;
        if (r.role == QLatin1String("client")) {
            DatabaseManager::setServerHost(r.host);
            DatabaseManager::setServerPort(r.port);
        }
        DatabaseManager::setSystemRole(r.role);
        m_pwdOverlay->hide();
        restartApplication();
    });

    m_pwdOverlay->installEventFilter(this); // 点击遮罩空白处关闭，见 eventFilter
}

void MainWindow::updateLoginLabel()
{
    if (!m_loginLabel) return;
    if (m_currentUser.isEmpty())
        m_loginLabel->setText(QStringLiteral("未登录　点击登录"));
    else
        m_loginLabel->setText(QStringLiteral("已登录　%1%2").arg(m_currentUser)
                                  .arg(m_isAdmin ? QStringLiteral("（管理员）") : QString()));
}

void MainWindow::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    if (m_pwdOverlay && m_pwdOverlay->isVisible())
        m_pwdOverlay->syncOverlay(); // 屏幕/窗口变化后重新贴合整块屏幕
    // 窗口尺寸变化后按强制比例重排左右两栏（普通 3:2、最大化/全屏 1:1）
    QTimer::singleShot(0, this, [this] { applySplitRatio(); });
}

// 最大化/还原切换可能不改变控件尺寸，这里补一次比例重排
void MainWindow::changeEvent(QEvent *event)
{
    QWidget::changeEvent(event);
    if (event->type() == QEvent::WindowStateChange)
        QTimer::singleShot(0, this, [this] { applySplitRatio(); });
}

void MainWindow::applyStyle()
{
    qApp->setStyleSheet(QStringLiteral(R"(
QWidget { background:#2B3046; color:#ffffff; font-size:14px; }
#titleBar { background:#2B3046; }
/* 菜单栏下方浅灰细线：所有页面共用 */
#titleLine { background:rgba(255,255,255,0.08); border:none; }
/* 标题栏右侧信息列：登录状态 / 服务器地址 / 版本号
   三行统一字号与颜色（登录标签的灰色），悬停/点击可操作项变绿 */
#titleInfo { background:transparent; color:#9FB0C9; font-size:13px; padding:0 8px; }
#titleInfo:hover { color:#43CF7C; }
#appTitle { color:white; font-size:25px; }
#menuBtn { color:white; font-size:17px; border-radius:0px; background:transparent; border:none; }
#menuBtn:checked { color:#1a67c0; background:#273d64; border-bottom:1px solid #1673db; }
#btnMin,#btnMax,#btnClose { background:transparent; border:none; color:#cfd3e0; border-radius:4px; }
#btnMin:hover,#btnMax:hover { background:rgba(255,255,255,0.12); }
#btnClose:hover { background:#E8463A; color:#fff; }
#catBar { background:#2B3046; border-top:1px solid rgba(255,255,255,0.08); }
/* 分类标签：与顶部菜单同款风格 */
#catTab { background:transparent; border:none; color:#ffffff; font-size:16px;
          padding:10px 22px; border-radius:0px; }
#catTab:hover:!checked { color:#43CF7C; }
#catTab:checked { color:#43CF7C; background:#1E3B2C; border-bottom:2px solid #43CF7C; }

/* 设置页 Tab：与分类标签同款风格 */
QTabWidget::pane { border:none; background:transparent; }
QTabBar::tab { background:transparent; color:#ffffff; font-size:16px;
               padding:10px 26px; border:none; }
QTabBar::tab:hover:!selected { color:#43CF7C; }
QTabBar::tab:selected { color:#43CF7C; background:#1E3B2C; border-bottom:2px solid #43CF7C; }
/* 主界面顶部细线已由共用 #titleLine 提供，这里不再重复描边 */
#toolBar { background:#2B3046; }
QLineEdit { background:#22293C; border:1px solid #4A5468;
            border-radius:8px; padding:6px 10px; color:#fff; }
QLineEdit:focus { border:1px solid #43CF7C; }
/* 输入框内的一键清除按钮：透明底，避免深色主题下出现方块背景 */
QLineEdit QToolButton { background:transparent; border:none; padding:2px;
                        border-radius:10px; }
QLineEdit QToolButton:hover { background:rgba(255,82,82,0.16); }
QLineEdit QToolButton:pressed { background:rgba(255,82,82,0.30); }
QPushButton#btnSearch, #btnEntry { background:#43CF7C; color:#0B1F14; border:none;
            border-radius:8px; padding:7px 18px; font-weight:bold; }
QPushButton#btnSearch:hover, #btnEntry:hover { background:#5CE0A0; }
QPushButton#btnEntry:disabled { background:rgba(67,207,124,0.3); color:rgba(11,31,20,0.5); }
#btnDanger { background:#E06C75; color:#ffffff; border:none;
            border-radius:8px; padding:7px 18px; font-weight:bold; }
#btnDanger:hover { background:#C9606A; }
#btnDanger:pressed { background:#B05560; }
#pwdError { background:transparent; color:#E06C75; font-size:12px; }
/* 检查更新卡片中的状态/提示文字（与登录卡的深色卡片配套） */
#updateStatus { background:transparent; color:rgba(255,255,255,0.78); font-size:13px; }
#btnGhost { background:transparent; border:1px solid #4A5468;
            color:rgba(255,255,255,0.85); border-radius:8px; padding:7px 18px; }
QComboBox { background:#22293C; border:1px solid #4A5468;
            border-radius:8px; padding:5px 10px; color:#fff; }
QComboBox:focus { border:1px solid #43CF7C; }
QComboBox::drop-down { subcontrol-origin:padding; subcontrol-position:center right;
            width:24px; border:none; }
QComboBox::down-arrow { image:url(:/image/arrow_down.svg); width:10px; height:10px;
            margin-right:8px; }
QComboBox QAbstractItemView { background:#22293C; color:#fff; }
#productTable { background:#2B3046; gridline-color:rgba(255,255,255,0.08);
            border:none; color:rgba(255,255,255,0.85); font-size:13px; }
QGroupBox { border:1px solid #4A5468; border-radius:10px;
            margin-top:18px; padding-top:8px; }
QGroupBox::title { subcontrol-origin:margin; subcontrol-position:top left;
            left:12px; padding:0 8px; color:rgba(255,255,255,0.8); }
QDateEdit { background:#22293C; border:1px solid #4A5468;
            border-radius:8px; padding:6px 10px; color:#fff; }
QDateEdit::drop-down { subcontrol-origin:padding; subcontrol-position:center right;
            width:24px; border:none; }
QDateEdit::down-arrow { image:url(:/image/arrow_down.svg); width:10px; height:10px;
            margin-right:8px; }
QHeaderView::section { background:#22293C; color:rgba(255,255,255,0.55);
            border:none; border-bottom:1px solid rgba(255,255,255,0.1);
            padding:4px 6px; font-size:12px; }
#productTable::item:selected { background:rgba(67,207,124,0.18); color:#fff; }
#chip { background:transparent; border:1px solid #4A5468;
        color:rgba(255,255,255,0.7); border-radius:10px; padding:4px 14px; }
#chip:checked { background:#43CF7C; color:#0B1F14; border-color:#43CF7C; font-weight:bold; }
#preview { background:#22293C; border:1px dashed #4A5468;
           border-radius:8px; color:rgba(255,255,255,0.5); }
#previewInfo { color:rgba(255,255,255,0.5); font-size:12px; }
/* 上传/下载安装包进度条 */
QProgressBar { background:#22293C; border:1px solid #4A5468; border-radius:6px;
               height:18px; text-align:center; color:#ffffff; }
QProgressBar::chunk { background:#43CF7C; border-radius:5px; }
#dtypeLabel { color:#cfd3e0; font-size:13px; }
#detailBlk { background:#22293C; border:1px solid #4A5468;
             border-radius:8px; color:rgba(255,255,255,0.85); padding:6px; }
/* 「更多图纸」：标题在框内，与备注/更改记录同款；列表透明融入外框 */
#detailTitle { background:transparent; color:#8a90a6; font-size:12px; font-weight:bold; }
#moreList { background:transparent; border:none; padding:0; }
#moreList::item { color:rgba(255,255,255,0.85); }
#moreList::item:selected { background:rgba(67,207,124,0.18); color:#fff; border-radius:6px; }
/* 设置页「手机同步」（仅服务器主机）：信息分组与传输日志统一绿色边框 */
#infoBlock { background:#22293C; border:1px solid #43CF7C;
             border-radius:8px; padding:2px; }
#logPhone { background:#22293C; border:1px solid #43CF7C;
            border-radius:8px; color:rgba(255,255,255,0.85); padding:6px; }
QSplitter::handle { background:rgba(255,255,255,0.08); width:2px; }

/* 滚动条：细、无箭头、深色，表格/日志/预览统一（默认样式在深色界面里又粗又突兀） */
QScrollBar:vertical { background:transparent; width:9px; margin:0px; border:none; }
QScrollBar::handle:vertical { background:rgba(255,255,255,0.20);
            border-radius:4px; min-height:28px; }
QScrollBar::handle:vertical:hover { background:rgba(67,207,124,0.55); }
QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height:0px; background:transparent; }
QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical { background:transparent; }
QScrollBar:horizontal { background:transparent; height:9px; margin:0px; border:none; }
QScrollBar::handle:horizontal { background:rgba(255,255,255,0.20);
            border-radius:4px; min-width:28px; }
QScrollBar::handle:horizontal:hover { background:rgba(67,207,124,0.55); }
QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal { width:0px; background:transparent; }
QScrollBar::add-page:horizontal, QScrollBar::sub-page:horizontal { background:transparent; }

/* 右键菜单：鼠标悬停/选中项变绿色 */
QMenu { background:#22293C; border:1px solid #4A5468; border-radius:8px;
        padding:4px; color:rgba(255,255,255,0.9); }
QMenu::item { background:transparent; padding:6px 26px 6px 18px; border-radius:6px; }
QMenu::item:selected { background:#1E3B2C; color:#43CF7C; }
QMenu::separator { height:1px; background:rgba(255,255,255,0.10); margin:5px 10px; }

/* 工具页「关于」tab 的信息卡片；#aboutOverlay 保留兼容，实际遮罩统一由 OverlayMask 绘制 */
#aboutOverlay { background:rgba(0,0,0,179); }
#aboutCard { background:#22293C; border:1px solid rgba(255,255,255,0.14); border-radius:14px; }
#aboutCard QLabel { background:transparent; }
#aboutCard QScrollArea, #aboutCard QScrollArea > QWidget > QWidget { background:transparent; }
#aboutTitle { color:#ffffff; font-size:23px; font-weight:bold; }
#aboutInfo { color:rgba(255,255,255,0.9); font-size:15px; line-height:1.75; }
#aboutTechTitle { color:#43CF7C; font-size:17px; font-weight:bold; }
#aboutGroup { color:rgba(255,255,255,0.62); font-size:13px; font-weight:bold; }
#aboutTech { color:rgba(255,255,255,0.92); font-size:15px; line-height:1.85; }
#aboutLine { background:rgba(255,255,255,0.14); border:none; }
)"));
}
