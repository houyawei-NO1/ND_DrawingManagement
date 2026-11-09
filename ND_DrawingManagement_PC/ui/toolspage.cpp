#include "toolspage.h"
#include "db/databasemanager.h"
#include "server/remoteapiclient.h"
#include "server/updatemanager.h"
#include "util/stocktypes.h"
#include "ndmessagebox.h"
#include "roundedmenu.h"
#include "updatedialog.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QTabWidget>
#include <QComboBox>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QAbstractItemView>
#include <QHeaderView>
#include <QLineEdit>
#include <QLabel>
#include <QPushButton>
#include <QFileDialog>
#include <QFileInfo>
#include <QApplication>
#include <QGuiApplication>
#include <QClipboard>
#include <QToolTip>
#include <QMenu>
#include <QAction>
#include <QShortcut>
#include <QKeySequence>
#include <QCursor>
#include <QSignalBlocker>
#include <QFont>
#include <QRegularExpression>
#include <QScrollArea>
#include <QFrame>
#include <QPixmap>
#include <QCoreApplication>
#include <QStandardPaths>
#include <QDir>
#include <QFile>
#include <QTimer>
#include <QEventLoop>

namespace {
// 服务端返回 13 列：
// 0 行号 1 存货编号 2 存货全名 3 存货型号 4 存货规格 5 存货产地
// 6 缺省供应商 7 库存数量 8 建档人 9 建档时间 10 修改人 11 修改时间 12 封装
// -1 表示该列不是原始数据，而是从全名 / 型号 / 规格即时解析出来的
const int kStockColumnCount = 8;
const int kStockVoltageCol = 5;
const int kStockToleranceCol = 6;
const int kStockOriginCol = 7;
const int kStockSourceColumn[kStockColumnCount] = {1, 2, 3, 4, 12, -1, -1, 5};

// ERP 里耐压和精度写在名称里，写法并不统一，例如：
//   贴片电容/100nF /CL31B104KBCNFNC / 50V 10% C1206
//   贴片电容/ 4.7nF  CGA3E2C0G1H472JT0Y0N/ (C0603/50V/5%)
//   电器盒贴片电容/100nF ±10% 100V/0603/CGA3E3X7S2A104KT0Y0N
// 只做容错抽取：解析不到就留空，不影响其它列，也不用改数据库或重新导入
struct StockExtra {
    QString voltage;
    QString tolerance;
};

StockExtra parseStockExtra(const QStringList &src)
{
    // 数字 + 可选 kV + V；前后不能紧贴字母/数字，避免把型号里的片段当成耐压
    static const QRegularExpression voltRe(QStringLiteral(
        "(?<![A-Za-z0-9.])(\\d{1,4}(?:\\.\\d+)?)\\s*([kK])?[vV](?![A-Za-z0-9])"));
    // 5% / 10% / ±10% / +-5%
    static const QRegularExpression tolRe(QStringLiteral(
        "(±|\\+/-|\\+-)?\\s*(\\d{1,2}(?:\\.\\d+)?)\\s*%"));

    QString text;
    // 按「全名 → 型号 → 规格」拼接，优先采用出现在全名里的参数
    const int textCols[3] = {2, 3, 4};
    for (int i = 0; i < 3; ++i) {
        const int idx = textCols[i];
        if (idx < src.size())
            text += src.at(idx) + QChar(' ');
    }

    StockExtra out;
    const QRegularExpressionMatch vm = voltRe.match(text);
    if (vm.hasMatch())
        out.voltage = vm.captured(1)
                      + (vm.captured(2).isEmpty() ? QString() : QStringLiteral("k"))
                      + QStringLiteral("V");
    const QRegularExpressionMatch tm = tolRe.match(text);
    if (tm.hasMatch())
        out.tolerance = (tm.captured(1) == QStringLiteral("±")
                             ? QStringLiteral("±") : QString())
                        + tm.captured(2) + QStringLiteral("%");
    return out;
}

QString stockTableRowText(const QTableWidget *t, int row)
{
    QStringList cells;
    for (int c = 0; c < t->columnCount(); ++c)
        cells.append(t->item(row, c) ? t->item(row, c)->text() : QString());
    return cells.join(QChar('\t'));
}

void copyStockText(QWidget *owner, const QString &text, const QString &tip)
{
    QGuiApplication::clipboard()->setText(text);
    QToolTip::showText(QCursor::pos(), tip, owner);
}
} // namespace

ToolsPage::ToolsPage(QWidget *parent) : QWidget(parent)
{
    setWindowTitle(QStringLiteral("工具"));

    QVBoxLayout *root = new QVBoxLayout(this);
    m_tabs = new QTabWidget(this);
    m_tabs->addTab(buildStockPage(), QStringLiteral("库存查询"));
    m_tabs->addTab(buildAboutPage(), QStringLiteral("关于"));
    root->addWidget(m_tabs);

    // 切到「关于」才查服务器上的手机版本号，避免离线时打开工具页就卡住
    connect(m_tabs, &QTabWidget::currentChanged, this, [this](int idx) {
        if (idx >= 0 && m_tabs->tabText(idx) == QStringLiteral("关于"))
            QTimer::singleShot(0, this, [this] { refreshMobileVersion(); });
    });
}

void ToolsPage::onPageShown()
{
    // 客户端离线时 combo 会被清空，下次进入工具页会再试一次；已有批次则不再发请求
    if (m_cmbStockBatch && m_cmbStockBatch->count() == 0)
        refreshStockBatches();
}

// ==================== 库存查询 ====================
QWidget *ToolsPage::buildStockPage()
{
    QWidget *page = new QWidget(this);
    QVBoxLayout *lay = new QVBoxLayout(page);
    lay->setContentsMargins(16, 14, 16, 14);
    lay->setSpacing(10);

    QHBoxLayout *top = new QHBoxLayout();
    QLabel *tip = new QLabel(QStringLiteral(
        "管家婆存货档案导入后保存在服务器工作目录的「数据库」文件夹；数据较多，输入关键字点「搜索」才展示。"), page);
    tip->setObjectName(QStringLiteral("previewInfo"));
    top->addWidget(tip, 1);
    QPushButton *btnImport = new QPushButton(QStringLiteral("导入管家婆存货档案"), page);
    btnImport->setObjectName(QStringLiteral("btnEntry"));
    btnImport->setCursor(Qt::PointingHandCursor);
    connect(btnImport, &QPushButton::clicked, this, &ToolsPage::onPickStockFile);
    top->addWidget(btnImport);
    QPushButton *btnDel = new QPushButton(QStringLiteral("删除当前数据"), page);
    btnDel->setObjectName(QStringLiteral("btnDanger"));
    btnDel->setCursor(Qt::PointingHandCursor);
    connect(btnDel, &QPushButton::clicked, this, &ToolsPage::onDeleteStockBatch);
    top->addWidget(btnDel);
    lay->addLayout(top);

    QHBoxLayout *line2 = new QHBoxLayout();
    line2->addWidget(new QLabel(QStringLiteral("当前数据："), page));
    m_cmbStockBatch = new QComboBox(page);
    m_cmbStockBatch->setMinimumWidth(280);
    line2->addWidget(m_cmbStockBatch);
    m_lblStockInfo = new QLabel(QStringLiteral("尚未导入"), page);
    m_lblStockInfo->setObjectName(QStringLiteral("previewInfo"));
    line2->addWidget(m_lblStockInfo, 1);
    lay->addLayout(line2);

    QHBoxLayout *line3 = new QHBoxLayout();
    m_edtStockKeyword = new QLineEdit(page);
    m_edtStockKeyword->setPlaceholderText(
        QStringLiteral("输入型号 / 存货全名 / 编号，例如 4.7nF"));
    m_edtStockKeyword->setClearButtonEnabled(true);
    line3->addWidget(m_edtStockKeyword, 1);
    QPushButton *btnSearch = new QPushButton(QStringLiteral("搜索"), page);
    btnSearch->setObjectName(QStringLiteral("btnSearch"));
    btnSearch->setCursor(Qt::PointingHandCursor);
    connect(btnSearch, &QPushButton::clicked, this, &ToolsPage::onSearchStock);
    connect(m_edtStockKeyword, &QLineEdit::returnPressed, this, &ToolsPage::onSearchStock);
    line3->addWidget(btnSearch);
    lay->addLayout(line3);

    m_lblStockResult = new QLabel(QStringLiteral("尚未搜索，未加载任何数据"), page);
    m_lblStockResult->setObjectName(QStringLiteral("previewInfo"));
    lay->addWidget(m_lblStockResult);

    m_stockTable = new QTableWidget(0, kStockColumnCount, page);
    m_stockTable->setObjectName(QStringLiteral("productTable"));
    m_stockTable->setHorizontalHeaderLabels({
        QStringLiteral("存货编号"), QStringLiteral("存货全名"), QStringLiteral("存货型号"),
        QStringLiteral("存货规格"), QStringLiteral("封装"), QStringLiteral("耐压值"),
        QStringLiteral("精度"), QStringLiteral("存货产地")});
    // 存货全名 / 存货型号用拉伸列占满剩余宽度，其余列固定，长文本不再被挤成省略号
    const int widths[kStockColumnCount] = {110, 0, 0, 110, 70, 80, 80, 160};
    QHeaderView *hh = m_stockTable->horizontalHeader();
    for (int c = 0; c < kStockColumnCount; ++c) {
        if (widths[c] == 0) {
            hh->setSectionResizeMode(c, QHeaderView::Stretch);
        } else {
            hh->setSectionResizeMode(c, QHeaderView::Fixed);
            m_stockTable->setColumnWidth(c, widths[c]);
        }
    }
    m_stockTable->verticalHeader()->setVisible(false);
    m_stockTable->verticalHeader()->setDefaultSectionSize(30);
    m_stockTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_stockTable->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_stockTable, &QTableWidget::cellDoubleClicked,
            this, &ToolsPage::onStockDoubleClicked);
    connect(m_stockTable, &QTableWidget::customContextMenuRequested,
            this, &ToolsPage::onStockContextMenu);
    auto *shortcut = new QShortcut(QKeySequence(QKeySequence::Copy), m_stockTable);
    connect(shortcut, &QShortcut::activated, this, [this] {
        const QList<QTableWidgetItem *> sel = m_stockTable->selectedItems();
        if (!sel.isEmpty())
            copyStockText(m_stockTable, sel.first()->text(), QStringLiteral("已复制单元格"));
    });
    connect(m_cmbStockBatch, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int) {
        m_stockTable->setRowCount(0);
        m_lblStockResult->setText(QStringLiteral("已切换数据，请重新搜索"));
    });
    lay->addWidget(m_stockTable, 1);
    return page;
}

void ToolsPage::refreshStockBatches()
{
    if (!m_cmbStockBatch)
        return;
    // 客户端没连上服务器时不要发同步请求，否则一次 8 秒超时会让切页明显卡住
    if (!DatabaseManager::isServer() && !RemoteApiClient::instance().reachable()) {
        QSignalBlocker blocker(m_cmbStockBatch);
        m_cmbStockBatch->clear();
        m_stockTable->setRowCount(0);
        m_lblStockInfo->setText(QStringLiteral("尚未连接服务器"));
        m_lblStockResult->setText(
            QStringLiteral("请先在设置的「服务器连接」确认服务器地址，再重新进入工具页"));
        return;
    }
    const QString prev = m_cmbStockBatch->currentData().toString();
    QString err;
    const QList<StockBatch> batches = DatabaseManager::instance().stockBatches(&err);
    QSignalBlocker blocker(m_cmbStockBatch);
    m_cmbStockBatch->clear();
    for (const StockBatch &b : batches)
        m_cmbStockBatch->addItem(
            QStringLiteral("%1（%2 条）").arg(b.label).arg(b.rows), b.key);
    if (!prev.isEmpty()) {
        const int i = m_cmbStockBatch->findData(prev);
        if (i >= 0) m_cmbStockBatch->setCurrentIndex(i);
    }
    m_stockTable->setRowCount(0);
    if (m_cmbStockBatch->count() == 0) {
        m_lblStockInfo->setText(QStringLiteral("尚未导入存货档案"));
        m_lblStockResult->setText(err.isEmpty()
            ? QStringLiteral("点击「导入管家婆存货档案」选择 .xls 文件")
            : err);
    } else {
        m_lblStockInfo->setText(QStringLiteral("共 %1 份数据，文件保存在服务器数据库文件夹")
                                    .arg(batches.size()));
        m_lblStockResult->setText(QStringLiteral("输入关键字后点「搜索」，不会一次加载全部数据"));
    }
}

void ToolsPage::fillStockTable(const QList<QStringList> &rows)
{
    m_stockTable->setUpdatesEnabled(false);
    m_stockTable->setRowCount(rows.size());
    for (int r = 0; r < rows.size(); ++r) {
        const QStringList &src = rows.at(r);
        const StockExtra extra = parseStockExtra(src);
        for (int c = 0; c < kStockColumnCount; ++c) {
            const int idx = kStockSourceColumn[c];
            QString v;
            if (idx < 0) {
                if (c == kStockVoltageCol) v = extra.voltage;
                else if (c == kStockToleranceCol) v = extra.tolerance;
            } else if (idx < src.size()) {
                v = src.at(idx);
            }
            QTableWidgetItem *it = new QTableWidgetItem(v);
            const bool left = (c == 1 || c == 2 || c == 3 || c == kStockOriginCol);
            it->setTextAlignment(left ? (Qt::AlignLeft | Qt::AlignVCenter)
                                      : Qt::AlignCenter);
            m_stockTable->setItem(r, c, it);
        }
    }
    m_stockTable->setUpdatesEnabled(true);
}

void ToolsPage::onPickStockFile()
{
    const QString path = QFileDialog::getOpenFileName(
        this, QStringLiteral("选择管家婆存货档案"), QString(),
        QStringLiteral("Excel 97-2003 表格 (*.xls);;所有文件 (*)"));
    if (path.isEmpty())
        return;
    QApplication::setOverrideCursor(Qt::WaitCursor);
    QString err;
    const bool ok = DatabaseManager::instance().importStockFile(
        path, QFileInfo(path).fileName(), &err);
    QApplication::restoreOverrideCursor();
    if (!ok) {
        NdMessageBox::warning(this, QStringLiteral("导入失败"), err);
        return;
    }
    m_edtStockKeyword->clear();
    refreshStockBatches();
    m_lblStockResult->setText(QStringLiteral("导入完成，输入关键字后点「搜索」查看数据"));
    NdMessageBox::information(this, QStringLiteral("导入完成"), QStringLiteral(
        "已归档到服务器工作目录的「数据库」文件夹并解析入库。\n"
        "数据较多，请输入关键字后点击「搜索」。"));
}

void ToolsPage::onDeleteStockBatch()
{
    const QString key = m_cmbStockBatch ? m_cmbStockBatch->currentData().toString() : QString();
    if (key.isEmpty()) {
        NdMessageBox::warning(this, QStringLiteral("删除库存数据"),
                              QStringLiteral("当前没有可删除的数据"));
        return;
    }
    if (!NdMessageBox::question(this, QStringLiteral("删除库存数据"),
                                QStringLiteral("确定删除「%1」及其归档表格文件吗？")
                                    .arg(m_cmbStockBatch->currentText())))
        return;
    QString err;
    if (!DatabaseManager::instance().deleteStockBatch(key, &err)) {
        NdMessageBox::warning(this, QStringLiteral("删除失败"), err);
        return;
    }
    refreshStockBatches();
}

void ToolsPage::onSearchStock()
{
    const QString kw = m_edtStockKeyword->text().trimmed();
    if (kw.isEmpty()) {
        m_lblStockResult->setText(QStringLiteral("请输入关键字后再搜索"));
        return;
    }
    const QString key = m_cmbStockBatch ? m_cmbStockBatch->currentData().toString() : QString();
    if (key.isEmpty()) {
        m_lblStockResult->setText(QStringLiteral("请先导入一份存货档案"));
        return;
    }
    QApplication::setOverrideCursor(Qt::WaitCursor);
    int total = 0;
    QString err;
    const QList<QStringList> rows = DatabaseManager::instance().stockSearch(
        key, kw, 500, &total, &err);
    QApplication::restoreOverrideCursor();
    fillStockTable(rows);
    if (!err.isEmpty()) {
        m_lblStockResult->setText(err);
        NdMessageBox::warning(this, QStringLiteral("查询失败"), err);
        return;
    }
    m_lblStockResult->setText(QStringLiteral("匹配 %1 条，已显示 %2 条%3")
        .arg(total).arg(rows.size())
        .arg(total > rows.size()
                 ? QStringLiteral("（最多显示 500 条，请缩小关键字）")
                 : QString()));
}

void ToolsPage::onStockDoubleClicked(int row, int column)
{
    if (row < 0 || row >= m_stockTable->rowCount()
        || column < 0 || column >= m_stockTable->columnCount())
        return;
    // 双击只复制点中的那一格；整行 / 全部结果仍可用右键菜单复制
    const QTableWidgetItem *it = m_stockTable->item(row, column);
    const QString title = m_stockTable->horizontalHeaderItem(column)
        ? m_stockTable->horizontalHeaderItem(column)->text() : QString();
    copyStockText(m_stockTable, it ? it->text() : QString(),
                  QStringLiteral("已复制「%1」").arg(title));
}

void ToolsPage::onStockContextMenu(const QPoint &pos)
{
    Q_UNUSED(pos);
    const QPoint vp = m_stockTable->viewport()->mapFromGlobal(QCursor::pos());
    const int row = m_stockTable->rowAt(vp.y());
    const int col = m_stockTable->columnAt(vp.x());
    if (row < 0)
        return;
    RoundedMenu menu(this);
    QAction *actRow = menu.addAction(QStringLiteral("复制整行数据"));
    menu.addSeparator();
    QList<QAction *> colActs;
    for (int c = 0; c < m_stockTable->columnCount(); ++c) {
        const QString title = m_stockTable->horizontalHeaderItem(c)
            ? m_stockTable->horizontalHeaderItem(c)->text() : QString::number(c);
        QAction *a = menu.addAction(QStringLiteral("复制「%1」").arg(title));
        if (c == col) {
            QFont f = a->font();
            f.setBold(true);
            a->setFont(f);
        }
        colActs.append(a);
    }
    menu.addSeparator();
    QAction *actAll = menu.addAction(QStringLiteral("复制当前所有结果（含表头）"));
    QAction *picked = menu.exec(QCursor::pos());
    if (!picked)
        return;
    if (picked == actRow) {
        copyStockText(m_stockTable, stockTableRowText(m_stockTable, row),
                      QStringLiteral("已复制整行数据"));
    } else if (picked == actAll) {
        QStringList lines;
        QStringList head;
        for (int c = 0; c < m_stockTable->columnCount(); ++c)
            head.append(m_stockTable->horizontalHeaderItem(c)
                        ? m_stockTable->horizontalHeaderItem(c)->text() : QString());
        lines.append(head.join(QChar('\t')));
        for (int r = 0; r < m_stockTable->rowCount(); ++r)
            lines.append(stockTableRowText(m_stockTable, r));
        copyStockText(m_stockTable, lines.join(QChar('\n')),
                      QStringLiteral("已复制全部结果"));
    } else {
        const int c = colActs.indexOf(picked);
        if (c >= 0) {
            const QTableWidgetItem *it = m_stockTable->item(row, c);
            copyStockText(m_stockTable, it ? it->text() : QString(),
                          QStringLiteral("已复制"));
        }
    }
}

// ==================== 关于 ====================
QWidget *ToolsPage::buildAboutPage()
{
    QWidget *page = new QWidget(this);
    QVBoxLayout *lay = new QVBoxLayout(page);
    lay->setContentsMargins(16, 14, 16, 14);

    QWidget *card = new QWidget(page);
    card->setObjectName(QStringLiteral("aboutCard"));
    card->setFixedSize(720, 680);
    QVBoxLayout *cl = new QVBoxLayout(card);
    cl->setContentsMargins(36, 24, 36, 20);
    cl->setSpacing(6);

    QLabel *logo = new QLabel(card);
    const QPixmap pm(QStringLiteral(":/image/logo.png"));
    if (!pm.isNull())
        logo->setPixmap(pm.scaled(60, 60, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    logo->setAlignment(Qt::AlignCenter);

    QLabel *title = new QLabel(QStringLiteral("南都图纸管理"), card);
    title->setObjectName(QStringLiteral("aboutTitle"));
    title->setAlignment(Qt::AlignCenter);

    // 版本号即构建日期：安装包内 version.txt 优先，保证安装后不会变成安装时间
    const QString buildDate = UpdateManager::instance().localVersion();

    QScrollArea *scroll = new QScrollArea(card);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setStyleSheet(QStringLiteral("background:transparent;border:none;"));
    QWidget *infoWrap = new QWidget(scroll);
    infoWrap->setStyleSheet(QStringLiteral("background:transparent;"));
    QVBoxLayout *il = new QVBoxLayout(infoWrap);
    il->setContentsMargins(4, 0, 4, 0);
    il->setSpacing(0);

    QLabel *info = new QLabel(
        QStringLiteral("河南南都汽车电子有限公司\n"
                       "河南省南阳市卧龙区天佑产业园24号楼2楼/3楼/4楼\n"
                       "开发：侯亚威　18337356560\n"
                       "版本号：%1（构建日期）").arg(buildDate), infoWrap);
    info->setObjectName(QStringLiteral("aboutInfo"));
    info->setAlignment(Qt::AlignCenter);
    il->addWidget(info);
    il->addSpacing(14);

    QFrame *line = new QFrame(infoWrap);
    line->setObjectName(QStringLiteral("aboutLine"));
    line->setFixedHeight(1);
    il->addWidget(line);
    il->addSpacing(14);

    QLabel *techTitle = new QLabel(QStringLiteral("技术"), infoWrap);
    techTitle->setObjectName(QStringLiteral("aboutTechTitle"));
    il->addWidget(techTitle);
    il->addSpacing(8);

    auto addTechGroup = [&](const QString &group, const QString &text) {
        QLabel *g = new QLabel(group, infoWrap);
        g->setObjectName(QStringLiteral("aboutGroup"));
        QLabel *t = new QLabel(text, infoWrap);
        t->setObjectName(QStringLiteral("aboutTech"));
        t->setWordWrap(true);
        il->addWidget(g);
        il->addWidget(t);
        il->addSpacing(10);
    };
    addTechGroup(QStringLiteral("开发与运行环境"),
                 QStringLiteral("Qt 6.8.3 / C++17（MSVC 2022 · MinGW-w64 13.1）\n"
                                "Qt 组件：Core · Gui · Widgets · Sql · Network · HttpServer · Pdf · PdfWidgets · Svg"));
    addTechGroup(QStringLiteral("功能组件"),
                 QStringLiteral("数据与服务：SQLite · Qt HttpServer（内嵌网页 + REST API）\n"
                                "离线 OCR：PaddleOCR-json（PP-OCRv3 中文模型）\n"
                                "HEIC 解码：libheif 1.23.7 + libde265 1.1.3（QLibrary 动态加载）\n"
                                "图纸浏览：Qt Pdf / Qt PdfWidgets / Qt Svg\n"
                                "安装包：Qt Installer Framework 4.11（binarycreator / windeployqt）"));
    il->addStretch(1);
    scroll->setWidget(infoWrap);
    scroll->viewport()->setStyleSheet(QStringLiteral("background:transparent;"));

    cl->addWidget(logo);
    cl->addSpacing(10);
    cl->addWidget(title);
    cl->addSpacing(16);

    // 关于卡片下方：检查更新（工具页免登录，任何用户都能查服务器上的新版）
    QHBoxLayout *checkRow = new QHBoxLayout();
    QLabel *checkTip = new QLabel(QStringLiteral(
        "版本号即程序构建日期，点击右侧按钮检查服务器上的新版本"), card);
    checkTip->setObjectName(QStringLiteral("previewInfo"));
    checkRow->addWidget(checkTip, 1);
    QPushButton *btnCheck = new QPushButton(QStringLiteral("检查更新"), card);
    btnCheck->setObjectName(QStringLiteral("btnEntry"));
    btnCheck->setCursor(Qt::PointingHandCursor);
    connect(btnCheck, &QPushButton::clicked, this, [this] {
        UpdateDialog::checkForUpdate(this);
    });
    checkRow->addWidget(btnCheck);
    cl->addLayout(checkRow);
    cl->addSpacing(8);

    // 手机端：显示服务器上的手机版本号，并提供直接下载 APK 的按钮
    QHBoxLayout *apkRow = new QHBoxLayout();
    m_lblMobileVersion = new QLabel(QStringLiteral("手机版本号：正在查询…"), card);
    m_lblMobileVersion->setObjectName(QStringLiteral("previewInfo"));
    apkRow->addWidget(m_lblMobileVersion, 1);
    QPushButton *btnApk = new QPushButton(QStringLiteral("下载 APK 文件"), card);
    btnApk->setObjectName(QStringLiteral("btnEntry"));
    btnApk->setCursor(Qt::PointingHandCursor);
    connect(btnApk, &QPushButton::clicked, this, &ToolsPage::onDownloadApk);
    apkRow->addWidget(btnApk);
    cl->addLayout(apkRow);

    QLabel *mobileTip = new QLabel(QStringLiteral(
        "手机与电脑处于同一局域网时，可在手机 APP「关于」页面点击「检查更新」直接下载安装；"
        "也可点上方按钮把 APK 下载到本机的下载目录。"), card);
    mobileTip->setObjectName(QStringLiteral("previewInfo"));
    mobileTip->setWordWrap(true);
    cl->addWidget(mobileTip);
    cl->addSpacing(6);
    cl->addWidget(scroll, 1);
    lay->addWidget(card, 0, Qt::AlignHCenter);
    lay->addStretch(1);
    return page;
}

void ToolsPage::refreshMobileVersion()
{
    if (!m_lblMobileVersion)
        return;
    const bool online = DatabaseManager::isServer() || RemoteApiClient::instance().reachable();
    if (!online) {
        m_lblMobileVersion->setText(QStringLiteral("手机版本号：未连接服务器"));
        return;
    }
    QString err;
    QApplication::setOverrideCursor(Qt::WaitCursor);
    const UpdateInfo info = UpdateManager::instance().checkUpdate(&err, PackageKind::Android);
    QApplication::restoreOverrideCursor();
    if (info.available)
        m_lblMobileVersion->setText(QStringLiteral("手机版本号：%1（服务器 APK %2）")
                                        .arg(info.version,
                                             UpdateManager::readableSize(info.size)));
    else
        m_lblMobileVersion->setText(QStringLiteral("手机版本号：%1")
                                        .arg(err.isEmpty()
                                                 ? QStringLiteral("服务器暂无 APK") : err));
}

void ToolsPage::onDownloadApk()
{
    const bool online = DatabaseManager::isServer() || RemoteApiClient::instance().reachable();
    if (!online) {
        NdMessageBox::warning(this, QStringLiteral("下载 APK"),
                              QStringLiteral("未连接服务器，请确认与服务器主机在同一局域网。"));
        return;
    }
    QString err;
    QApplication::setOverrideCursor(Qt::WaitCursor);
    const UpdateInfo info = UpdateManager::instance().checkUpdate(&err, PackageKind::Android);
    QApplication::restoreOverrideCursor();
    if (!info.available) {
        NdMessageBox::warning(this, QStringLiteral("下载 APK"),
                              QStringLiteral("服务器上还没有手机安装包，请先在「设置 → 版本管理」上传 APK。"));
        return;
    }
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
    if (dir.isEmpty() || !QDir().mkpath(dir)) {
        NdMessageBox::warning(this, QStringLiteral("下载 APK"),
                              QStringLiteral("找不到可写的下载目录。"));
        return;
    }
    const QString name = info.fileName.isEmpty()
                             ? QStringLiteral("ND_DrawingManagement.apk") : info.fileName;
    const QString path = QDir(dir).filePath(name);
    if (QFile::exists(path) && !QFile::remove(path)) {
        NdMessageBox::warning(this, QStringLiteral("下载 APK"),
                              QStringLiteral("同名文件被占用，请先关闭或删除：%1").arg(path));
        return;
    }
    m_lblMobileVersion->setText(QStringLiteral("正在下载 APK：0%"));
    QApplication::setOverrideCursor(Qt::WaitCursor);
    const bool ok = UpdateManager::instance().downloadPackage(path, &err,
        [this](int percent, qint64, qint64) {
            m_lblMobileVersion->setText(QStringLiteral("正在下载 APK：%1%").arg(percent));
            QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
        }, PackageKind::Android);
    QApplication::restoreOverrideCursor();
    if (!ok) {
        m_lblMobileVersion->setText(QStringLiteral("手机版本号：%1（APK 下载失败）").arg(info.version));
        NdMessageBox::warning(this, QStringLiteral("下载失败"), err);
        return;
    }
    m_lblMobileVersion->setText(QStringLiteral("手机版本号：%1（APK 已下载）").arg(info.version));
    NdMessageBox::information(this, QStringLiteral("下载完成"),
        QStringLiteral("APK 已保存到：\n%1\n\n把该文件传到手机安装，或让手机与电脑处于同一局域网，"
                       "在手机 APP「关于」页面点击「检查更新」直接下载安装。").arg(path));
}
