#include "chipquerypage.h"
#include "db/databasemanager.h"
#include "ui/ndmessagebox.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QLabel>
#include <QLineEdit>
#include <QComboBox>
#include <QPushButton>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QHeaderView>
#include <QFrame>
#include <QResizeEvent>
#include <QMap>
#include <QStringList>
#include <QColor>
#include <algorithm>

namespace {
const QString kHeaders[] = {
    QStringLiteral("硬件版本"), QStringLiteral("类型"), QStringLiteral("型号"),
    QStringLiteral("数量"), QStringLiteral("功能"), QStringLiteral("国家"),
    QStringLiteral("品牌"), QStringLiteral("备注")
};

const QStringList &typeOrder()
{
    static const QStringList order = {
        QStringLiteral("主控芯片"), QStringLiteral("驱动芯片"),
        QStringLiteral("电源芯片"), QStringLiteral("功率元件"),
        QStringLiteral("信号链芯片")
    };
    return order;
}

// 中文按 Unicode 排会把「信号链」排到最前，这里按业务固定顺序
int typeRank(const QString &t)
{
    const int i = typeOrder().indexOf(t);
    return i < 0 ? 9 : i;
}

// 可编辑下拉里显示一个不一定已存在的值（并顺手记住，下次可直接选）
void ensureComboItem(QComboBox *cb, const QString &text)
{
    if (!cb || text.isEmpty())
        return;
    if (cb->findText(text) < 0)
        cb->addItem(text);
    cb->setCurrentText(text);
}
}

ChipQueryPage::ChipQueryPage(QWidget *parent) : QWidget(parent)
{
    setObjectName(QStringLiteral("chipPage"));
    buildUi();
    m_ready = true;
    m_lblResult->setText(QStringLiteral("  切到本页后自动加载芯片资料…"));
}

void ChipQueryPage::refresh()
{
    // 字典不常变，只在首次（或上次离线没取到）时拉一次，减少客户端请求
    if (m_dict.isEmpty())
        refreshDict();
    onSearch();
}

void ChipQueryPage::buildUi()
{
    setStyleSheet(QStringLiteral(
        "#chipPage{background:#2B3046;color:#fff;}"
        "#chipPage QWidget{background:transparent;}"
        "#chipPage QLabel{color:rgba(255,255,255,0.72);font-size:12px;}"
        "#chipPage QLineEdit,#chipPage QComboBox{background:#22293C;"
        "    border:1px solid #4A5468;border-radius:8px;padding:5px 9px;"
        "    color:#fff;font-size:12px;min-height:22px;}"
        "#chipPage QComboBox::drop-down{border:none;width:18px;}"
        "#chipPage QPushButton{background:transparent;border:1px solid #4A5468;"
        "    border-radius:8px;color:rgba(255,255,255,0.85);"
        "    font-size:12px;padding:0 14px;min-height:32px;}"
        "#chipPage QPushButton:hover:!disabled{border-color:#43CF7C;color:#fff;}"
        "#chipPage QPushButton:disabled{color:#6D7890;border-color:#3A465F;}"
        "#chipPage QPushButton#chipGreen{background:#43CF7C;color:#0B1F14;"
        "    border:1px solid #43CF7C;font-weight:bold;}"
        "#chipPage QPushButton#chipDanger{background:#E06C75;color:#fff;"
        "    border:1px solid #E06C75;}"
        // #chipGreen / #chipDanger 带两个 ID 选择器，普通 :disabled 规则压不住，
        // 这里用同样高的特异性把未登录时的三个管理按钮统一成禁用色。
        "#chipPage QPushButton#chipGreen:disabled,"
        "#chipPage QPushButton#chipDanger:disabled{background:transparent;"
        "    border:1px solid #3A465F;color:#6D7890;font-weight:normal;}"
        "#chipPage QTableWidget{background:#2B3046;border:1px solid rgba(255,255,255,0.08);"
        "    border-radius:4px;gridline-color:rgba(255,255,255,0.06);color:#fff;}"
        "#chipPage QHeaderView::section{background:#22293C;color:rgba(255,255,255,0.55);"
        "    border:none;border-bottom:1px solid rgba(255,255,255,0.10);"
        "    font-size:11px;font-weight:normal;height:28px;}"
        "#chipPage #chipResult{background:rgba(67,207,124,0.08);"
        "    border:1px solid rgba(67,207,124,0.28);border-radius:4px;"
        "    color:#8CE0B7;padding:0 10px;}"
        "#chipPage #chipSummary{background:rgba(0,0,0,0.16);"
        "    border:1px solid #4A5468;border-radius:8px;"
        "    color:rgba(255,255,255,0.82);padding:0 12px;}"
        "#chipPage #chipEditor QWidget{background:transparent;}"
        "#chipPage #chipEditor{background:rgba(8,12,20,0.95);"
        "    border:1px solid rgba(160,180,220,0.22);border-radius:9px;}"
        "#chipPage #chipEditor QLabel{color:#9AA6BD;font-size:11px;}"
        "#chipPage #chipEditor QLineEdit,#chipPage #chipEditor QComboBox{"
        "    background:#111722;border:1px solid #3A465F;border-radius:5px;"
        "    color:#E7ECF8;font-size:11px;min-height:20px;padding:3px 7px;}"
        "#chipPage #chipEditor QPushButton{min-height:26px;padding:0 12px;font-size:11px;}"
        "#chipPage #chipEditor QPushButton#chipSave{background:#43CF7C;color:#0B1F14;"
        "    border:1px solid #43CF7C;font-weight:bold;min-height:28px;padding:0 20px;}"
        "#chipPage #chipEditor QPushButton#chipSave:hover{background:#5BE090;"
        "    border-color:#5BE090;}"
        "#chipPage #chipEditor #chipQuick{background:rgba(67,207,124,0.07);"
        "    border:1px solid rgba(67,207,124,0.30);border-radius:6px;}"
        ));

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(16, 12, 16, 10);
    root->setSpacing(9);

    // 说明 + 登录态
    auto *tipRow = new QHBoxLayout();
    auto *tip = new QLabel(QStringLiteral(
        "芯片资料已导入系统，表格仅作初始数据来源；之后直接在本页维护。"), this);
    m_lblLogin = new QLabel(this);
    m_lblLogin->setObjectName(QStringLiteral("chipLogin"));
    tipRow->addWidget(tip);
    tipRow->addStretch();
    tipRow->addWidget(m_lblLogin);
    root->addLayout(tipRow);

    // 查询行
    auto *queryRow = new QHBoxLayout();
    queryRow->setSpacing(8);
    m_btnModeHw = new QPushButton(QStringLiteral("按硬件查询"), this);
    m_btnModeModel = new QPushButton(QStringLiteral("按芯片反查"), this);
    m_btnModeHw->setCheckable(true);
    m_btnModeModel->setCheckable(true);
    m_edtKeyword = new QLineEdit(this);
    m_edtKeyword->setPlaceholderText(QStringLiteral("输入硬件版本，如 VTA2C701"));
    m_edtKeyword->setClearButtonEnabled(true);
    auto *btnSearch = new QPushButton(QStringLiteral("搜 索"), this);
    btnSearch->setObjectName(QStringLiteral("chipGreen"));
    m_btnAdd = new QPushButton(QStringLiteral("＋ 新增"), this);
    m_btnAdd->setObjectName(QStringLiteral("chipGreen"));
    m_btnEdit = new QPushButton(QStringLiteral("修改"), this);
    m_btnDelete = new QPushButton(QStringLiteral("删除"), this);
    m_btnDelete->setObjectName(QStringLiteral("chipDanger"));
    queryRow->addWidget(m_btnModeHw);
    queryRow->addWidget(m_btnModeModel);
    queryRow->addWidget(m_edtKeyword, 1);
    queryRow->addWidget(btnSearch);
    queryRow->addSpacing(6);
    queryRow->addWidget(m_btnAdd);
    queryRow->addWidget(m_btnEdit);
    queryRow->addWidget(m_btnDelete);
    root->addLayout(queryRow);

    m_lblResult = new QLabel(this);
    m_lblResult->setObjectName(QStringLiteral("chipResult"));
    m_lblResult->setFixedHeight(27);
    root->addWidget(m_lblResult);

    m_table = new QTableWidget(this);
    m_table->setObjectName(QStringLiteral("chipTable"));
    m_table->setColumnCount(8);
    for (int i = 0; i < 8; ++i)
        m_table->setHorizontalHeaderItem(i, new QTableWidgetItem(kHeaders[i]));
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->verticalHeader()->setVisible(false);
    m_table->horizontalHeader()->setStretchLastSection(true);
    m_table->setColumnWidth(0, 140);
    m_table->setColumnWidth(1, 90);
    m_table->setColumnWidth(2, 190);
    m_table->setColumnWidth(3, 60);
    m_table->setColumnWidth(4, 200);
    m_table->setColumnWidth(5, 70);
    m_table->setColumnWidth(6, 150);
    root->addWidget(m_table, 1);

    m_lblSummary = new QLabel(this);
    m_lblSummary->setObjectName(QStringLiteral("chipSummary"));
    m_lblSummary->setFixedHeight(30);
    m_lblSummary->setWordWrap(false);
    root->addWidget(m_lblSummary);

    // 右侧新增 / 编辑浮层（不加入布局，绝对定位）
    m_editor = new QFrame(this);
    m_editor->setObjectName(QStringLiteral("chipEditor"));
    m_editor->setFixedWidth(360);
    auto *eLay = new QVBoxLayout(m_editor);
    eLay->setContentsMargins(12, 10, 12, 10);
    eLay->setSpacing(9);

    auto *head = new QHBoxLayout();
    m_lblEditorTitle = new QLabel(QStringLiteral("新增芯片记录"), m_editor);
    m_lblEditorTitle->setStyleSheet(QStringLiteral(
        "color:#E6EBF7;font-size:13px;font-weight:bold;"));
    auto *btnClose = new QPushButton(QStringLiteral("×"), m_editor);
    btnClose->setFixedSize(26, 24);
    btnClose->setStyleSheet(QStringLiteral("padding:0;font-size:16px;"));
    head->addWidget(m_lblEditorTitle);
    head->addStretch();
    head->addWidget(btnClose);
    eLay->addLayout(head);

    auto *quick = new QFrame(m_editor);
    quick->setObjectName(QStringLiteral("chipQuick"));
    auto *qLay = new QVBoxLayout(quick);
    qLay->setContentsMargins(9, 8, 9, 8);
    qLay->setSpacing(6);
    auto *quickTip = new QLabel(QStringLiteral(
        "快速添加：新硬件含有已有芯片时，选型号自动带出资料"), quick);
    quickTip->setStyleSheet(QStringLiteral("color:#8CE0B7;font-size:11px;"));
    m_cmbQuick = new QComboBox(quick);
    m_cmbQuick->setEditable(true);
    m_cmbQuick->setInsertPolicy(QComboBox::NoInsert);
    m_cmbQuick->lineEdit()->setPlaceholderText(QStringLiteral("搜索已有型号，如 TLE4275"));
    auto *quickNote = new QLabel(QStringLiteral(
        "选中后自动填充 类型 / 功能 / 国家 / 品牌，只需填硬件版本和数量"), quick);
    quickNote->setStyleSheet(QStringLiteral("color:#8CE0B7;font-size:10px;"));
    qLay->addWidget(quickTip);
    qLay->addWidget(m_cmbQuick);
    qLay->addWidget(quickNote);
    eLay->addWidget(quick);

    auto *form = new QGridLayout();
    form->setHorizontalSpacing(9);
    form->setVerticalSpacing(8);
    m_cmbHardware = new QComboBox(m_editor);
    m_cmbHardware->setEditable(true);
    m_edtModel = new QLineEdit(m_editor);
    m_cmbCategory = new QComboBox(m_editor);   // 预设 5 类，也能直接输入新类型
    m_cmbCategory->setEditable(true);
    m_cmbCategory->addItem(QString());
    m_cmbCategory->addItems(typeOrder());
    m_edtQty = new QLineEdit(m_editor);
    m_edtFunction = new QLineEdit(m_editor);
    m_edtCountry = new QLineEdit(m_editor);
    m_cmbBrand = new QComboBox(m_editor);      // 已有品牌，也能输入新品牌
    m_cmbBrand->setEditable(true);
    m_cmbBrand->addItem(QString());
    m_edtRemark = new QLineEdit(m_editor);
    auto addField = [&](int row, int col, const QString &label, QWidget *w,
                        bool span = false) {
        auto *l = new QLabel(label, m_editor);
        form->addWidget(l, row, col * 2);
        form->addWidget(w, row, col * 2 + 1, 1, span ? 3 : 1);
    };
    addField(0, 0, QStringLiteral("硬件版本 *"), m_cmbHardware);
    addField(0, 1, QStringLiteral("型号 *"), m_edtModel);
    addField(1, 0, QStringLiteral("类型"), m_cmbCategory);
    addField(1, 1, QStringLiteral("数量 *"), m_edtQty);
    addField(2, 0, QStringLiteral("功能"), m_edtFunction, true);
    addField(3, 0, QStringLiteral("国家"), m_edtCountry);
    addField(3, 1, QStringLiteral("品牌"), m_cmbBrand);
    addField(4, 0, QStringLiteral("备注"), m_edtRemark, true);
    eLay->addLayout(form);

    auto *permTip = new QLabel(QStringLiteral(
        "* 为必填项。未登录只能查询；新增 / 修改 / 删除需先登录账号。"), m_editor);
    permTip->setWordWrap(true);
    permTip->setStyleSheet(QStringLiteral("color:#6D7890;font-size:10px;"));
    eLay->addWidget(permTip);
    eLay->addStretch();

    auto *foot = new QHBoxLayout();
    foot->addStretch();
    auto *btnCancel = new QPushButton(QStringLiteral("取 消"), m_editor);
    auto *btnSave = new QPushButton(QStringLiteral("保 存"), m_editor);
    btnSave->setObjectName(QStringLiteral("chipSave"));
    foot->addWidget(btnCancel);
    foot->addWidget(btnSave);
    eLay->addLayout(foot);
    m_editor->hide();

    connect(btnSearch, &QPushButton::clicked, this, &ChipQueryPage::onSearch);
    connect(m_edtKeyword, &QLineEdit::returnPressed, this, &ChipQueryPage::onSearch);
    connect(m_btnModeHw, &QPushButton::clicked, this, [this] { onSetMode(true); });
    connect(m_btnModeModel, &QPushButton::clicked, this, [this] { onSetMode(false); });
    connect(m_btnAdd, &QPushButton::clicked, this, &ChipQueryPage::onAdd);
    connect(m_btnEdit, &QPushButton::clicked, this, &ChipQueryPage::onEdit);
    connect(m_btnDelete, &QPushButton::clicked, this, &ChipQueryPage::onDelete);
    connect(btnClose, &QPushButton::clicked, this, &ChipQueryPage::onCancel);
    connect(btnCancel, &QPushButton::clicked, this, &ChipQueryPage::onCancel);
    connect(btnSave, &QPushButton::clicked, this, &ChipQueryPage::onSave);
    connect(m_cmbQuick, QOverload<int>::of(&QComboBox::activated),
            this, &ChipQueryPage::onQuickPicked);
    connect(m_cmbBrand, QOverload<int>::of(&QComboBox::activated),
            this, &ChipQueryPage::onBrandPicked);
    connect(m_table, &QTableWidget::cellDoubleClicked,
            this, &ChipQueryPage::onCellDoubleClicked);

    onSetMode(true);
    setLoggedIn(QString());
}

void ChipQueryPage::setLoggedIn(const QString &user)
{
    m_user = user;
    if (m_lblLogin) {
        m_lblLogin->setText(user.isEmpty()
            ? QStringLiteral("未登录 · 只读查询")
            : QStringLiteral("已登录 %1 · 可新增 / 修改 / 删除").arg(user));
        m_lblLogin->setStyleSheet(user.isEmpty()
            ? QStringLiteral("color:#8A95AC;font-size:11px;")
            : QStringLiteral("color:#8CE0B7;font-size:11px;"));
    }
    updateManageButtons();
}

void ChipQueryPage::updateManageButtons()
{
    const bool on = loggedIn();
    if (m_btnAdd) m_btnAdd->setEnabled(on);
    if (m_btnEdit) m_btnEdit->setEnabled(on);
    if (m_btnDelete) m_btnDelete->setEnabled(on);
    if (!on && m_editorOpen)
        onCancel();
}

void ChipQueryPage::onSetMode(bool byHardware)
{
    m_byHardware = byHardware;
    m_btnModeHw->setChecked(byHardware);
    m_btnModeModel->setChecked(!byHardware);
    const auto style = [](QPushButton *b, bool on) {
        b->setStyleSheet(on
            ? QStringLiteral("background:#1E3B2C;border:1px solid #1E3B2C;"
                             "color:#43CF7C;font-weight:bold;border-radius:8px;")
            : QStringLiteral("background:transparent;border:1px solid #4A5468;"
                             "color:rgba(255,255,255,0.6);border-radius:8px;"));
    };
    style(m_btnModeHw, byHardware);
    style(m_btnModeModel, !byHardware);
    m_edtKeyword->setPlaceholderText(byHardware
        ? QStringLiteral("输入硬件版本，如 VTA2C701")
        : QStringLiteral("输入芯片型号，如 TLE4275"));
    if (m_ready)
        onSearch();
}

void ChipQueryPage::onSearch()
{
    QString err;
    DatabaseManager &db = DatabaseManager::instance();
    m_items = db.chipSearch(m_byHardware ? QStringLiteral("hardware")
                                         : QStringLiteral("model"),
                            m_edtKeyword->text(), 1000, &m_total, &err);
    if (!err.isEmpty()) {
        m_lblResult->setText(QStringLiteral("查询失败：%1").arg(err));
        m_table->setRowCount(0);
        m_lblSummary->clear();
        return;
    }
    sortItems();
    fillTable();
    updateSummary();
}

// 按硬件查询时：同一硬件内按 主控→驱动→电源→功率→信号链 排列，
// 反查时按型号聚拢再按硬件排，避免中文 Unicode 顺序把「信号链」顶到最前。
void ChipQueryPage::sortItems()
{
    std::stable_sort(m_items.begin(), m_items.end(),
        [this](const ChipItem &a, const ChipItem &b) {
            if (m_byHardware) {
                if (a.hardware != b.hardware)
                    return a.hardware < b.hardware;
                const int ra = typeRank(a.category), rb = typeRank(b.category);
                if (ra != rb)
                    return ra < rb;
                return a.model < b.model;
            }
            if (a.model != b.model)
                return a.model < b.model;
            if (a.hardware != b.hardware)
                return a.hardware < b.hardware;
            return typeRank(a.category) < typeRank(b.category);
        });
}

void ChipQueryPage::fillTable()
{
    m_table->setRowCount(m_items.size());
    for (int i = 0; i < m_items.size(); ++i) {
        const ChipItem &c = m_items.at(i);
        const QStringList row = c.toRow();
        for (int j = 0; j < 8; ++j) {
            auto *it = new QTableWidgetItem(row.at(j));
            it->setTextAlignment(j == 7 ? (Qt::AlignLeft | Qt::AlignVCenter)
                                        : Qt::AlignCenter);
            if (j == 0)
                it->setForeground(QColor(QStringLiteral("#8CE0B7")));
            if (j == 2)
                it->setForeground(QColor(QStringLiteral("#63E6A8")));
            if (j == 3)
                it->setForeground(QColor(QStringLiteral("#E7ECF8")));
            m_table->setItem(i, j, it);
        }
    }

    int qtySum = 0;
    for (const ChipItem &c : m_items)
        qtySum += c.qty;
    if (m_byHardware) {
        const QString hw = m_edtKeyword->text().trimmed().isEmpty()
            ? QStringLiteral("全部") : m_edtKeyword->text().trimmed();
        m_lblResult->setText(QStringLiteral(
            "按硬件 %1 查询：共 %2 条芯片记录，合计 %3 颗　·　双击「型号」可反查使用该芯片的所有硬件")
            .arg(hw).arg(m_total).arg(qtySum));
    } else {
        QStringList hws;
        for (const ChipItem &c : m_items)
            if (!hws.contains(c.hardware))
                hws.append(c.hardware);
        const QString kw = m_edtKeyword->text().trimmed().isEmpty()
            ? QStringLiteral("全部") : m_edtKeyword->text().trimmed();
        m_lblResult->setText(QStringLiteral(
            "按芯片 %1 反查：%2 个硬件使用，共 %3 条记录，合计 %4 颗　·　双击「硬件版本」可按硬件查询")
            .arg(kw).arg(hws.size()).arg(m_total).arg(qtySum));
    }
}

void ChipQueryPage::updateSummary()
{
    QMap<QString, int> byType, byCountry;
    for (const ChipItem &c : m_items) {
        byType[c.category.isEmpty() ? QStringLiteral("未分类") : c.category] += c.qty;
        byCountry[c.country.isEmpty() ? QStringLiteral("未填") : c.country] += c.qty;
    }
    QStringList types, countries;
    for (auto it = byType.constBegin(); it != byType.constEnd(); ++it)
        types << QStringLiteral("%1 %2").arg(it.key()).arg(it.value());
    for (auto it = byCountry.constBegin(); it != byCountry.constEnd(); ++it)
        countries << QStringLiteral("%1 %2").arg(it.key()).arg(it.value());
    int sum = 0;
    for (const ChipItem &c : m_items)
        sum += c.qty;
    m_lblSummary->setText(QStringLiteral(
        "按类型：%1　　按国家：%2　　合计 %3 颗")
        .arg(types.join(QStringLiteral(" · ")), countries.join(QStringLiteral(" · ")))
        .arg(sum));
}

void ChipQueryPage::refreshDict()
{
    DatabaseManager &db = DatabaseManager::instance();
    m_hardwares = db.chipHardwares();
    m_dict = db.chipModelDict();

    if (m_cmbHardware) {
        const QString cur = m_cmbHardware->currentText();
        m_cmbHardware->blockSignals(true);
        m_cmbHardware->clear();
        m_cmbHardware->addItems(m_hardwares);
        m_cmbHardware->setCurrentText(cur);
        m_cmbHardware->blockSignals(false);
    }
    if (m_cmbQuick) {
        m_cmbQuick->blockSignals(true);
        m_cmbQuick->clear();
        m_cmbQuick->addItem(QString());
        for (const ChipItem &c : m_dict)
            m_cmbQuick->addItem(QStringLiteral("%1   %2").arg(c.model, c.brand), c.model);
        m_cmbQuick->setCurrentIndex(0);
        m_cmbQuick->blockSignals(false);
    }
    // 类型：固定 5 类优先，后面补上用户自己新增过的类型
    if (m_cmbCategory) {
        const QString cur = m_cmbCategory->currentText();
        QStringList cats = typeOrder();
        for (const ChipItem &c : m_dict)
            if (!c.category.isEmpty() && !cats.contains(c.category))
                cats.append(c.category);
        m_cmbCategory->blockSignals(true);
        m_cmbCategory->clear();
        m_cmbCategory->addItem(QString());
        m_cmbCategory->addItems(cats);
        m_cmbCategory->setCurrentText(cur);
        m_cmbCategory->blockSignals(false);
    }
    // 品牌：库里出现过的都列出来，选中已有品牌时联动带出国家
    if (m_cmbBrand) {
        const QString cur = m_cmbBrand->currentText();
        QStringList brands;
        for (const ChipItem &c : m_dict)
            if (!c.brand.isEmpty() && !brands.contains(c.brand))
                brands.append(c.brand);
        m_cmbBrand->blockSignals(true);
        m_cmbBrand->clear();
        m_cmbBrand->addItem(QString());
        m_cmbBrand->addItems(brands);
        m_cmbBrand->setCurrentText(cur);
        m_cmbBrand->blockSignals(false);
    }
}

void ChipQueryPage::onQuickPicked(int index)
{
    const QString model = m_cmbQuick->itemData(index).toString();
    if (model.isEmpty())
        return;
    for (const ChipItem &c : m_dict) {
        if (c.model != model)
            continue;
        m_edtModel->setText(c.model);
        ensureComboItem(m_cmbCategory, c.category);
        m_edtFunction->setText(c.function);
        m_edtCountry->setText(c.country);
        ensureComboItem(m_cmbBrand, c.brand);
        if (m_edtQty->text().trimmed().isEmpty())
            m_edtQty->setText(QStringLiteral("1"));
        break;
    }
}

// 选了已有品牌就带出该品牌对应的国家；输入的新品牌没有历史数据，留给用户手填。
// 恩智浦（NXP）原始表格误填「德国」，已在种子数据与库中统一订正为「荷兰」；
// 若后续仍有品牌对应多个产地，按出现次数取最常见的那个。
void ChipQueryPage::onBrandPicked(int index)
{
    Q_UNUSED(index);
    const QString brand = m_cmbBrand->currentText().trimmed();
    if (brand.isEmpty())
        return;
    QMap<QString, int> votes;
    for (const ChipItem &c : m_dict)
        if (c.brand == brand && !c.country.isEmpty())
            votes[c.country]++;
    QString best;
    int bestN = 0;
    for (auto it = votes.constBegin(); it != votes.constEnd(); ++it) {
        if (it.value() > bestN) {
            bestN = it.value();
            best = it.key();
        }
    }
    if (!best.isEmpty())
        m_edtCountry->setText(best);
}

void ChipQueryPage::onCellDoubleClicked(int row, int column)
{
    if (row < 0 || row >= m_items.size())
        return;
    if (m_byHardware && column == 2) {
        m_edtKeyword->setText(m_items.at(row).model);
        onSetMode(false);
    } else if (!m_byHardware && column == 0) {
        m_edtKeyword->setText(m_items.at(row).hardware);
        onSetMode(true);
    }
}

void ChipQueryPage::openEditor(const ChipItem &item, bool isNew)
{
    m_editId = isNew ? 0 : item.id;
    m_lblEditorTitle->setText(isNew ? QStringLiteral("新增芯片记录")
                                    : QStringLiteral("修改芯片记录"));
    m_cmbQuick->setCurrentIndex(0);
    m_cmbHardware->setCurrentText(item.hardware);
    m_edtModel->setText(item.model);
    m_cmbCategory->setCurrentIndex(0);   // 先清空，避免上次的值残留
    m_cmbBrand->setCurrentIndex(0);
    ensureComboItem(m_cmbCategory, item.category);
    m_edtQty->setText(isNew ? QStringLiteral("1") : QString::number(item.qty));
    m_edtFunction->setText(item.function);
    m_edtCountry->setText(item.country);
    ensureComboItem(m_cmbBrand, item.brand);
    m_edtRemark->setText(item.remark);
    m_editorOpen = true;
    m_editor->show();
    m_editor->raise();
    positionEditor();
    m_edtModel->setFocus();
}

void ChipQueryPage::positionEditor()
{
    if (!m_editor)
        return;
    const int h = qMin(height() - 24, 640);
    m_editor->setGeometry(width() - 360 - 16, 12, 360, h);
}

void ChipQueryPage::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    positionEditor();
}

ChipItem ChipQueryPage::collectForm() const
{
    ChipItem c;
    c.id = m_editId;
    c.hardware = m_cmbHardware->currentText().trimmed();
    c.category = m_cmbCategory->currentText().trimmed();
    c.model = m_edtModel->text().trimmed();
    c.qty = m_edtQty->text().trimmed().toInt();
    c.function = m_edtFunction->text().trimmed();
    c.country = m_edtCountry->text().trimmed();
    c.brand = m_cmbBrand->currentText().trimmed();
    c.remark = m_edtRemark->text().trimmed();
    return c;
}

void ChipQueryPage::onAdd()
{
    if (!loggedIn()) {
        NdMessageBox::warning(this, QStringLiteral("需要登录"),
                              QStringLiteral("请先登录账号，才能新增芯片信息。"));
        return;
    }
    ChipItem c;
    if (m_byHardware)
        c.hardware = m_edtKeyword->text().trimmed(); // 反查模式下关键字是型号，不能当硬件版本
    openEditor(c, true);
}

void ChipQueryPage::onEdit()
{
    if (!loggedIn())
        return;
    const int row = m_table->currentRow();
    if (row < 0 || row >= m_items.size()) {
        NdMessageBox::information(this, QStringLiteral("修改芯片"),
                                  QStringLiteral("请先在表格里选中一行。"));
        return;
    }
    openEditor(m_items.at(row), false);
}

void ChipQueryPage::onDelete()
{
    if (!loggedIn())
        return;
    const int row = m_table->currentRow();
    if (row < 0 || row >= m_items.size()) {
        NdMessageBox::information(this, QStringLiteral("删除芯片"),
                                  QStringLiteral("请先在表格里选中一行。"));
        return;
    }
    const ChipItem &c = m_items.at(row);
    if (!NdMessageBox::question(this, QStringLiteral("删除芯片"),
            QStringLiteral("确定删除「%1 / %2」这条芯片记录？").arg(c.hardware, c.model)))
        return;
    QString err;
    if (!DatabaseManager::instance().chipDelete(c.id, &err)) {
        NdMessageBox::warning(this, QStringLiteral("删除失败"),
                              err.isEmpty() ? QStringLiteral("删除芯片信息失败") : err);
        return;
    }
    refreshDict();
    onSearch();
}

void ChipQueryPage::onSave()
{
    if (!loggedIn())
        return;
    ChipItem c = collectForm();
    if (c.hardware.isEmpty() || c.model.isEmpty()) {
        NdMessageBox::warning(this, QStringLiteral("信息不完整"),
                              QStringLiteral("硬件版本和型号不能为空。"));
        return;
    }
    if (c.qty <= 0) {
        NdMessageBox::warning(this, QStringLiteral("信息不完整"),
                              QStringLiteral("数量必须是大于 0 的整数。"));
        return;
    }
    QString err;
    if (!DatabaseManager::instance().chipSave(&c, &err)) {
        NdMessageBox::warning(this, QStringLiteral("保存失败"),
                              err.isEmpty() ? QStringLiteral("芯片信息保存失败") : err);
        return;
    }
    onCancel();
    refreshDict();
    onSearch();
}

void ChipQueryPage::onCancel()
{
    m_editorOpen = false;
    m_editId = 0;
    if (m_editor)
        m_editor->hide();
}
