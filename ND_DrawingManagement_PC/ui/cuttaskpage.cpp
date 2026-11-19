#include "cuttaskpage.h"
#include "ndmessagebox.h"
#include "ndinputdialog.h"
#include "db/databasemanager.h"
#include "util/xlsreader.h"

#include <QInputDialog>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QTabWidget>
#include <QLineEdit>
#include <QPushButton>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QHeaderView>
#include <QScrollBar>
#include <QStyledItemDelegate>
#include <QTimer>
#include <QLabel>
#include <QFileDialog>
#include <QFileInfo>
#include <QHash>
#include <QDateTime>
#include <QColor>
#include <QEvent>

namespace {
// 与「图纸查询」表格一致：表头与单元格文字居中、浅灰
void setCenteredHeaders(QTableWidget *table, const QStringList &headers)
{
    table->setColumnCount(headers.size());
    for (int i = 0; i < headers.size(); ++i) {
        QTableWidgetItem *h = new QTableWidgetItem(headers.at(i));
        h->setTextAlignment(Qt::AlignCenter);
        table->setHorizontalHeaderItem(i, h);
    }
}
QTableWidgetItem *centeredItem(const QString &text)
{
    QTableWidgetItem *it = new QTableWidgetItem(text);
    it->setTextAlignment(Qt::AlignCenter);
    it->setForeground(QColor(QStringLiteral("#FFFFFF")));
    return it;
}

// 预览行机台列的编辑器：双击直接弹出一号机～五号机选择。
// 用委托临时创建 QComboBox，不用 setCellWidget，避免反复刷新累积控件导致卡死。
class MachineComboDelegate : public QStyledItemDelegate
{
public:
    explicit MachineComboDelegate(QObject *parent = nullptr) : QStyledItemDelegate(parent) {}
    QWidget *createEditor(QWidget *parent, const QStyleOptionViewItem &,
                          const QModelIndex &index) const override
    {
        if (!index.isValid()) return nullptr;
        const QModelIndex idIndex = index.model()->index(index.row(), 0);
        const bool preview = idIndex.data(Qt::UserRole).toInt() > 0;
        const int taskId = idIndex.data(Qt::UserRole + 1).toInt();
        const QString state = index.model()->index(index.row(), 14).data(Qt::DisplayRole).toString();
        if (!preview && !(taskId > 0 && state == QStringLiteral("待生产")))
            return nullptr;
        QComboBox *cmb = new QComboBox(parent);
        for (const CutMachine &m : DatabaseManager::instance().cutMachines())
            cmb->addItem(m.name, m.id);
        QTimer::singleShot(0, cmb, [cmb]() { cmb->showPopup(); });
        return cmb;
    }
    void setEditorData(QWidget *editor, const QModelIndex &index) const override
    {
        if (QComboBox *cmb = qobject_cast<QComboBox *>(editor))
            cmb->setCurrentText(index.data(Qt::DisplayRole).toString());
    }
    void setModelData(QWidget *editor, QAbstractItemModel *model,
                      const QModelIndex &index) const override
    {
        if (QComboBox *cmb = qobject_cast<QComboBox *>(editor))
            model->setData(index, cmb->currentText(), Qt::EditRole);
    }
};

// 正式任务状态列的编辑器：只允许把「待生产 / 进行中」标记为已完成
class StatusComboDelegate : public QStyledItemDelegate
{
public:
    explicit StatusComboDelegate(QObject *parent = nullptr) : QStyledItemDelegate(parent) {}
    QWidget *createEditor(QWidget *parent, const QStyleOptionViewItem &,
                          const QModelIndex &index) const override
    {
        const QModelIndex idIndex = index.model()->index(index.row(), 0);
        if (idIndex.data(Qt::UserRole + 1).toInt() <= 0) return nullptr;  // 只针对正式任务
        const QString state = index.data(Qt::DisplayRole).toString();
        if (state == QStringLiteral("已完成") || state == QStringLiteral("已作废"))
            return nullptr;
        QComboBox *cmb = new QComboBox(parent);
        cmb->addItem(QStringLiteral("待生产"));
        cmb->addItem(QStringLiteral("已完成"));
        QTimer::singleShot(0, cmb, [cmb]() { cmb->showPopup(); });
        return cmb;
    }
    void setEditorData(QWidget *editor, const QModelIndex &index) const override
    {
        if (QComboBox *cmb = qobject_cast<QComboBox *>(editor))
            cmb->setCurrentText(index.data(Qt::DisplayRole).toString());
    }
    void setModelData(QWidget *editor, QAbstractItemModel *model,
                      const QModelIndex &index) const override
    {
        if (QComboBox *cmb = qobject_cast<QComboBox *>(editor))
            model->setData(index, cmb->currentText(), Qt::EditRole);
    }
};
} // namespace

CutTaskPage::CutTaskPage(QWidget *parent) : QWidget(parent)
{
    // 线束页所有表格统一为「图纸查询」风格：细边框、圆角、白色文字
    setStyleSheet(QStringLiteral(
        "#productTable{background:#2B3046;border:1px solid rgba(255,255,255,0.08);"
        "border-radius:4px;gridline-color:rgba(255,255,255,0.06);"
        "color:#FFFFFF;font-size:12px;}"
        "#productTable::item{color:#FFFFFF;}"
        "#productTable::item:selected{background:rgba(67,207,124,0.18);color:#fff;}"
        "QHeaderView::section{background:#22293C;color:#FFFFFF;"
        "border:none;border-bottom:1px solid rgba(255,255,255,0.10);"
        "font-size:11px;font-weight:normal;height:28px;}"));

    QVBoxLayout *root = new QVBoxLayout(this);
    root->setContentsMargins(16, 12, 16, 12);
    root->setSpacing(10);

    m_tabs = new QTabWidget(this);
    m_tabs->addTab(buildImportTab(), QStringLiteral("下线表导入"));
    m_tabs->addTab(buildTaskTab(), QStringLiteral("任务与派工"));
    m_tabs->addTab(buildRuleTab(), QStringLiteral("派单规则"));
    root->addWidget(m_tabs, 1);
}

bool CutTaskPage::eventFilter(QObject *obj, QEvent *event)
{
    if (event->type() == QEvent::MouseButtonPress
        && obj->property("clearRuleSelectionOnClick").toBool()) {
        onRuleReset();
    }
    return QWidget::eventFilter(obj, event);
}

QWidget *CutTaskPage::buildImportTab()
{
    QWidget *page = new QWidget(this);
    QVBoxLayout *lay = new QVBoxLayout(page);
    lay->setContentsMargins(10, 12, 10, 10);
    lay->setSpacing(10);

    QGridLayout *grid = new QGridLayout();
    grid->addWidget(new QLabel(QStringLiteral("下线表文件："), page), 0, 0);
    m_edtExcelPath = new QLineEdit(page);
    m_edtExcelPath->setReadOnly(true);
    m_edtExcelPath->setPlaceholderText(QStringLiteral("选择导出的下线表 .xls / .xlsx"));
    grid->addWidget(m_edtExcelPath, 0, 1);
    m_btnPick = new QPushButton(QStringLiteral("选择文件"), page);
    m_btnPick->setObjectName(QStringLiteral("btnEntry"));
    grid->addWidget(m_btnPick, 0, 2);
    m_btnImport = new QPushButton(QStringLiteral("导入"), page);
    m_btnImport->setObjectName(QStringLiteral("btnEntry"));
    grid->addWidget(m_btnImport, 0, 3);
    lay->addLayout(grid);

    m_lblImportInfo = new QLabel(QStringLiteral(""), page);
    m_lblImportInfo->setWordWrap(true);
    lay->addWidget(m_lblImportInfo);

    // ---- 已导入批次管理：紧跟导入区上移，避免中间大片空白 ----
    QHBoxLayout *batchBar = new QHBoxLayout();
    batchBar->setSpacing(8);
    QLabel *batchTitle = new QLabel(QStringLiteral("已导入线束："), page);
    batchBar->addWidget(batchTitle);
    m_lblBatchInfo = new QLabel(QStringLiteral("共 0 套线束"), page);
    batchBar->addWidget(m_lblBatchInfo);
    batchBar->addStretch(1);
    m_btnBatchRefresh = new QPushButton(QStringLiteral("刷新列表"), page);
    m_btnBatchRefresh->setObjectName(QStringLiteral("btnEntry"));
    batchBar->addWidget(m_btnBatchRefresh);
    m_btnBatchDelete = new QPushButton(QStringLiteral("删除线束"), page);
    m_btnBatchDelete->setObjectName(QStringLiteral("btnDanger"));  // 红色：不可恢复的删除
    m_btnBatchDelete->setCursor(Qt::PointingHandCursor);
    m_btnBatchDelete->setToolTip(QStringLiteral("删除该线束及其全部明细、已生成的派工任务"));
    batchBar->addWidget(m_btnBatchDelete);
    lay->addLayout(batchBar);

    m_batchTable = new QTableWidget(page);
    m_batchTable->setObjectName(QStringLiteral("productTable"));
    m_batchTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_batchTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_batchTable->setSelectionMode(QAbstractItemView::SingleSelection);
    m_batchTable->verticalHeader()->hide();   // 不显示白色行号列，与其它表格统一
    m_batchTable->setMaximumHeight(180);
    lay->addWidget(m_batchTable, 1);

    m_previewTable = new QTableWidget(page);
    m_previewTable->setObjectName(QStringLiteral("productTable"));
    m_previewTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_previewTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_previewTable->verticalHeader()->hide();
    m_previewTable->setWordWrap(true);                                  // 单元格/表头文字自动换行
    m_previewTable->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff); // 彻底禁止横向滚动，所有列一页显示
    m_previewTable->verticalHeader()->setDefaultSectionSize(76);
    m_previewTable->verticalHeader()->setSectionResizeMode(QHeaderView::Fixed);
    m_previewTable->setMinimumHeight(320);
    lay->addWidget(m_previewTable, 3);

    connect(m_btnPick, &QPushButton::clicked, this, &CutTaskPage::onPickExcel);
    connect(m_btnImport, &QPushButton::clicked, this, &CutTaskPage::onImport);
    connect(m_btnBatchDelete, &QPushButton::clicked, this, &CutTaskPage::onDeleteBatch);
    connect(m_btnBatchRefresh, &QPushButton::clicked, this, &CutTaskPage::refreshBatchTable);
    return page;
}

QWidget *CutTaskPage::buildTaskTab()
{
    QWidget *page = new QWidget(this);
    QVBoxLayout *lay = new QVBoxLayout(page);
    lay->setContentsMargins(10, 12, 10, 10);
    lay->setSpacing(10);

    QHBoxLayout *bar = new QHBoxLayout();
    bar->setSpacing(8);
    bar->addWidget(new QLabel(QStringLiteral("型号："), page));
    m_cmbBatch = new QComboBox(page);
    bar->addWidget(m_cmbBatch, 2);
    bar->addWidget(new QLabel(QStringLiteral("机台："), page));
    m_cmbMachine = new QComboBox(page);
    m_cmbMachine->addItem(QStringLiteral("全部机台"), QStringLiteral("all"));
    bar->addWidget(m_cmbMachine, 1);
    bar->addWidget(new QLabel(QStringLiteral("状态："), page));
    m_cmbStatus = new QComboBox(page);
    m_cmbStatus->addItem(QStringLiteral("全部"), QStringLiteral("all"));
    m_cmbStatus->addItem(QStringLiteral("待生产"), QStringLiteral("assigned"));
    m_cmbStatus->addItem(QStringLiteral("进行中"), QStringLiteral("doing"));
    m_cmbStatus->addItem(QStringLiteral("已完成"), QStringLiteral("done"));
    bar->addWidget(m_cmbStatus, 1);
    m_btnRefresh = new QPushButton(QStringLiteral("刷新"), page);
    m_btnRefresh->setObjectName(QStringLiteral("btnEntry"));
    bar->addWidget(m_btnRefresh);
    m_btnAutoDispatch = new QPushButton(QStringLiteral("智能分配"), page);
    m_btnAutoDispatch->setObjectName(QStringLiteral("btnEntry"));
    m_btnAutoDispatch->setToolTip(QStringLiteral(
        "① 防水栓/端子等规则锁定机台；② 线径规则限定各机台适合的材料；"
        "③ 剩余任务按线径+线色归组，在适合的机台间挑最闲的，尽量均衡。\n"
        "下线表 Excel 自带的「机台」列不参与派工。\n"
        "只生成预览，可改少量任务的机台，确认后再派工。"));
    bar->addWidget(m_btnAutoDispatch);
    m_btnCommit = new QPushButton(QStringLiteral("确认派工"), page);
    m_btnCommit->setObjectName(QStringLiteral("btnEntry"));
    m_btnCommit->setToolTip(QStringLiteral("把表格中待确认的预览任务正式派到各机台"));
    m_btnCommit->setEnabled(false);
    bar->addWidget(m_btnCommit);
    lay->addLayout(bar);

    m_taskTable = new QTableWidget(page);
    m_taskTable->setObjectName(QStringLiteral("productTable"));
    // 仅「待确认」预览行的机台格可编辑，其余行通过 item flags 禁止编辑
    m_taskTable->setEditTriggers(QAbstractItemView::DoubleClicked
                                 | QAbstractItemView::EditKeyPressed);
    m_taskTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_taskTable->verticalHeader()->hide();   // 去掉白色行号列，与其它表格统一
    m_taskTable->setWordWrap(true);          // 单元格/表头文字自动换行，长内容折行显示
    m_taskTable->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff); // 彻底禁止横向滚动
    m_taskTable->verticalHeader()->setDefaultSectionSize(84);
    m_taskTable->verticalHeader()->setSectionResizeMode(QHeaderView::Fixed);
    m_taskTable->setItemDelegateForColumn(11, new MachineComboDelegate(m_taskTable));
    m_taskTable->setItemDelegateForColumn(14, new StatusComboDelegate(m_taskTable));
    m_taskTable->setToolTip(QStringLiteral(
        "双击「机台」可改派；双击已派工任务的「状态」可标记为已完成"));
    lay->addWidget(m_taskTable, 1);

    m_lblTaskInfo = new QLabel(QStringLiteral("共 0 条任务"), page);
    m_lblTaskInfo->setWordWrap(true);
    lay->addWidget(m_lblTaskInfo);

    connect(m_btnRefresh, &QPushButton::clicked, this, &CutTaskPage::onRefresh);
    connect(m_btnAutoDispatch, &QPushButton::clicked, this, &CutTaskPage::onAutoDispatch);
    connect(m_btnCommit, &QPushButton::clicked, this, &CutTaskPage::onCommitDispatch);
    connect(m_taskTable, &QTableWidget::cellChanged, this, &CutTaskPage::onTaskCellChanged);
    connect(m_taskTable, &QTableWidget::cellChanged, this, &CutTaskPage::onTaskStatusChanged);
    // 筛选条件变化立即刷新表格
    connect(m_cmbBatch, &QComboBox::currentIndexChanged, this, &CutTaskPage::refreshTasks);
    connect(m_cmbMachine, &QComboBox::currentIndexChanged, this, &CutTaskPage::refreshTasks);
    connect(m_cmbStatus, &QComboBox::currentIndexChanged, this, &CutTaskPage::refreshTasks);
    return page;
}

// 派单规则：约束某类线径 / 端子 / 防水栓 只能发到指定机台。
// 命中规则的明细在「智能分配」时优先按规则锁定机台，其余再走 Excel 指定与均衡。
QWidget *CutTaskPage::buildRuleTab()
{
    QWidget *page = new QWidget(this);
    QVBoxLayout *rootLay = new QVBoxLayout(page);
    rootLay->setContentsMargins(10, 12, 10, 10);
    rootLay->setSpacing(10);

    QLabel *hint = new QLabel(QStringLiteral(
        "规则用于限制某类任务只能派给指定机台：线径按范围设置（如一号机 0.5-1.5），"
        "端子/防水栓按编号设置（如端子 PP2011101 只发一号机）。目标机台可选「其他」，"
        "表示人工/手动下线；端子/防水栓/材质含「屏蔽」的明细会自动归「其他」，无需单独建规则。"
        "智能分配时防水栓/端子规则锁定机台，线径规则只限定机台适合的材料，"
        "其余按机台当前任务量均衡分配。"), page);
    hint->setWordWrap(true);
    rootLay->addWidget(hint);

    // 左：规则设置表单；右：规则表格
    QHBoxLayout *panes = new QHBoxLayout();
    panes->setSpacing(14);
    QWidget *leftPane = new QWidget(page);
    leftPane->setObjectName(QStringLiteral("ruleFormPane"));
    leftPane->setProperty("clearRuleSelectionOnClick", true);
    leftPane->installEventFilter(this);
    leftPane->setMinimumWidth(420);
    QVBoxLayout *lay = new QVBoxLayout(leftPane);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(10);
    panes->addWidget(leftPane, 0);
    rootLay->addLayout(panes, 1);

    QGridLayout *grid = new QGridLayout();
    grid->setHorizontalSpacing(8);
    grid->setVerticalSpacing(8);
    grid->addWidget(new QLabel(QStringLiteral("匹配字段："), page), 0, 0);
    m_cmbRuleField = new QComboBox(page);
    m_cmbRuleField->addItem(QStringLiteral("线径(截面)"), QStringLiteral("section"));
    m_cmbRuleField->addItem(QStringLiteral("端子"), QStringLiteral("terminal"));
    m_cmbRuleField->addItem(QStringLiteral("防水栓(压前作业)"), QStringLiteral("seal"));
    m_cmbRuleField->addItem(QStringLiteral("颜色"), QStringLiteral("color"));
    m_cmbRuleField->addItem(QStringLiteral("材质"), QStringLiteral("material"));
    grid->addWidget(m_cmbRuleField, 0, 1);

    grid->addWidget(new QLabel(QStringLiteral("发给机台："), page), 0, 2);
    m_cmbRuleMachine = new QComboBox(page);
    grid->addWidget(m_cmbRuleMachine, 0, 3);

    // 第二行按字段类型二选一显示：线径 → 范围输入；其它字段 → 匹配方式+匹配值
    m_widgetRuleRange = new QWidget(page);
    QHBoxLayout *rangeLay = new QHBoxLayout(m_widgetRuleRange);
    rangeLay->setContentsMargins(0, 0, 0, 0);
    rangeLay->setSpacing(8);
    rangeLay->addWidget(new QLabel(QStringLiteral("线径范围："), m_widgetRuleRange));
    m_spinRuleMin = new QDoubleSpinBox(m_widgetRuleRange);
    m_spinRuleMin->setDecimals(2);
    m_spinRuleMin->setRange(0.05, 100.0);
    m_spinRuleMin->setSingleStep(0.25);
    m_spinRuleMin->setValue(0.5);
    m_spinRuleMax = new QDoubleSpinBox(m_widgetRuleRange);
    m_spinRuleMax->setDecimals(2);
    m_spinRuleMax->setRange(0.05, 100.0);
    m_spinRuleMax->setSingleStep(0.25);
    m_spinRuleMax->setValue(1.5);
    rangeLay->addWidget(m_spinRuleMin);
    rangeLay->addWidget(new QLabel(QStringLiteral("～"), m_widgetRuleRange));
    rangeLay->addWidget(m_spinRuleMax);
    rangeLay->addStretch(1);
    grid->addWidget(m_widgetRuleRange, 1, 0, 1, 4);

    m_widgetRuleMatch = new QWidget(page);
    QHBoxLayout *matchLay = new QHBoxLayout(m_widgetRuleMatch);
    matchLay->setContentsMargins(0, 0, 0, 0);
    matchLay->setSpacing(8);
    matchLay->addWidget(new QLabel(QStringLiteral("匹配方式："), m_widgetRuleMatch));
    m_cmbRuleMatch = new QComboBox(m_widgetRuleMatch);
    m_cmbRuleMatch->addItem(QStringLiteral("精确等于"), QStringLiteral("exact"));
    m_cmbRuleMatch->addItem(QStringLiteral("包含"), QStringLiteral("contains"));
    m_cmbRuleMatch->addItem(QStringLiteral("开头是"), QStringLiteral("prefix"));
    matchLay->addWidget(m_cmbRuleMatch);
    matchLay->addSpacing(18);
    matchLay->addWidget(new QLabel(QStringLiteral("匹配值："), m_widgetRuleMatch));
    m_edtRuleValue = new QLineEdit(m_widgetRuleMatch);
    m_edtRuleValue->setPlaceholderText(QStringLiteral("如 PP2011101"));
    matchLay->addWidget(m_edtRuleValue, 1);
    grid->addWidget(m_widgetRuleMatch, 1, 0, 1, 4);

    lay->addLayout(grid);

    // 规则操作按钮：新增 / 保存修改 分开，另加启用-禁用
    QHBoxLayout *btns = new QHBoxLayout();
    btns->setSpacing(8);
    btns->addWidget(new QLabel(QStringLiteral("表单操作："), page));
    m_btnRuleAdd = new QPushButton(QStringLiteral("新增规则"), page);
    m_btnRuleAdd->setObjectName(QStringLiteral("btnEntry"));
    m_btnRuleAdd->setToolTip(QStringLiteral("把表单内容添加为一条新规则"));
    btns->addWidget(m_btnRuleAdd);
    m_btnRuleSave = new QPushButton(QStringLiteral("保存修改"), page);
    m_btnRuleSave->setObjectName(QStringLiteral("btnEntry"));
    m_btnRuleSave->setToolTip(QStringLiteral("把表单内容保存到表格中选中的规则上"));
    btns->addWidget(m_btnRuleSave);
    btns->addSpacing(18);
    btns->addWidget(new QLabel(QStringLiteral("选中规则："), page));
    m_btnRuleToggle = new QPushButton(QStringLiteral("禁用规则"), page);
    m_btnRuleToggle->setObjectName(QStringLiteral("btnDanger"));  // 红色：停用规则
    m_btnRuleToggle->setCursor(Qt::PointingHandCursor);
    m_btnRuleToggle->setToolTip(QStringLiteral("切换选中规则的启用状态，停用后不参与智能分配"));
    btns->addWidget(m_btnRuleToggle);
    m_btnRuleDelete = new QPushButton(QStringLiteral("删除规则"), page);
    m_btnRuleDelete->setObjectName(QStringLiteral("btnDanger"));  // 红色：不可恢复的删除
    m_btnRuleDelete->setCursor(Qt::PointingHandCursor);
    btns->addWidget(m_btnRuleDelete);
    btns->addStretch(1);
    lay->addLayout(btns);
    lay->addStretch(1);

    QWidget *rightPane = new QWidget(page);
    rightPane->setProperty("clearRuleSelectionOnClick", true);
    rightPane->installEventFilter(this);
    QVBoxLayout *rightLay = new QVBoxLayout(rightPane);
    rightLay->setContentsMargins(0, 0, 0, 0);
    rightLay->setSpacing(8);
    m_ruleTable = new QTableWidget(rightPane);
    m_ruleTable->setObjectName(QStringLiteral("productTable"));
    m_ruleTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_ruleTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_ruleTable->setSelectionMode(QAbstractItemView::SingleSelection);
    m_ruleTable->verticalHeader()->hide();
    rightLay->addWidget(m_ruleTable, 1);

    m_lblRuleInfo = new QLabel(QStringLiteral("共 0 条规则（点击表格行可载入编辑）"), rightPane);
    rightLay->addWidget(m_lblRuleInfo);
    panes->addWidget(rightPane, 1);

    connect(m_btnRuleAdd, &QPushButton::clicked, this, &CutTaskPage::onRuleAdd);
    connect(m_btnRuleSave, &QPushButton::clicked, this, &CutTaskPage::onRuleSave);
    connect(m_btnRuleToggle, &QPushButton::clicked, this, &CutTaskPage::onRuleToggle);
    connect(m_btnRuleDelete, &QPushButton::clicked, this, &CutTaskPage::onRuleDelete);
    connect(m_edtRuleValue, &QLineEdit::returnPressed, this, &CutTaskPage::onRuleAdd);
    connect(m_ruleTable, &QTableWidget::cellClicked, this, &CutTaskPage::onRuleRowClicked);
    connect(m_ruleTable, &QTableWidget::clicked, this, [this](const QModelIndex &idx) {
        if (!idx.isValid() || !m_ruleTable->item(idx.row(), 0))
            onRuleReset();   // 点击表格空白处清空左侧表单
    });
    connect(m_cmbRuleField, &QComboBox::currentIndexChanged, this, &CutTaskPage::updateRuleInputs);
    updateRuleInputs();
    updateRuleButtons();
    return page;
}

// 线径规则只显示范围输入，其余字段只显示匹配方式 + 匹配值（互斥隐藏，避免干扰）
void CutTaskPage::updateRuleInputs()
{
    const bool range = (m_cmbRuleField->currentData().toString() == QLatin1String("section"));
    m_widgetRuleRange->setVisible(range);
    m_widgetRuleMatch->setVisible(!range);
}

// 按钮可用状态：新增只看是否登录；保存/启用/删除必须先在表格里选中规则
void CutTaskPage::updateRuleButtons()
{
    const bool editing = (m_editingRuleId > 0);
    m_btnRuleAdd->setEnabled(m_ruleEditable);
    m_btnRuleSave->setEnabled(m_ruleEditable && editing);
    m_btnRuleToggle->setEnabled(m_ruleEditable && editing);
    m_btnRuleDelete->setEnabled(m_ruleEditable && editing);
    m_btnRuleToggle->setText(m_editingRuleEnabled ? QStringLiteral("禁用规则")
                                                  : QStringLiteral("启用规则"));
}

// 读取表单内容（不含 id / enabled），校验失败时弹提示
bool CutTaskPage::collectRuleForm(CutRule *rule)
{
    if (!rule) return false;
    rule->field = m_cmbRuleField->currentData().toString();
    rule->machineId = m_cmbRuleMachine->currentData().toInt();
    if (rule->machineId <= 0) {
        NdMessageBox::information(this, QStringLiteral("派单规则"),
            QStringLiteral("请选择目标机台。"));
        return false;
    }
    if (rule->isRange()) {
        double lo = m_spinRuleMin->value();
        double hi = m_spinRuleMax->value();
        if (lo > hi) qSwap(lo, hi);
        rule->matchType = QStringLiteral("range");
        rule->matchValue = QStringLiteral("%1-%2")
            .arg(QString::number(lo, 'g', 10), QString::number(hi, 'g', 10));
    } else {
        rule->matchType = m_cmbRuleMatch->currentData().toString();
        rule->matchValue = m_edtRuleValue->text().trimmed();
        if (rule->matchValue.isEmpty()) {
            NdMessageBox::information(this, QStringLiteral("派单规则"),
                QStringLiteral("请填写匹配值，如 PP2011101。"));
            return false;
        }
    }
    return true;
}

void CutTaskPage::onPageShown()
{
    refreshBatches();
    refreshBatchTable();
    refreshTasks();
    refreshRules();
}

void CutTaskPage::setLoggedIn(const QString &user)
{
    m_currentUser = user;
    const bool editable = !user.isEmpty();
    m_btnImport->setEnabled(editable);
    m_btnAutoDispatch->setEnabled(editable);
    m_btnCommit->setEnabled(editable && !m_previewTasks.isEmpty());
    m_btnBatchDelete->setEnabled(editable);
    m_ruleEditable = editable;
    updateRuleButtons();
}

void CutTaskPage::onPickExcel()
{
    const QString path = QFileDialog::getOpenFileName(this,
        QStringLiteral("选择下线表"), QString(),
        QStringLiteral("Excel 文件 (*.xls);;所有文件 (*.*)"));
    if (path.isEmpty())
        return;
    m_excelPath = path;
    m_edtExcelPath->setText(path);

    // 立即解析预览
    QString model, batchNo, err;
    QList<CutItem> items;
    QStringList warnings;
    if (!parseExcel(path, &model, &batchNo, &items, &err, &warnings)) {
        m_lblImportInfo->setText(QStringLiteral("解析失败：%1").arg(err));
        m_previewTable->setRowCount(0);
        m_parsedItems.clear();
        return;
    }
    m_parsedItems = items;
    m_parsedModel = model;
    m_parsedBatchNo = batchNo;
    QString info = QStringLiteral("线束型号：%1　批次号：%2　共解析 %3 条明细，确认无误后点「导入」。")
                       .arg(model).arg(batchNo).arg(items.size());
    if (!warnings.isEmpty())
        info += QStringLiteral("　⚠ 原表有 %1 处数据异常，已按提示替代，序号未错位。")
                    .arg(warnings.size());
    m_lblImportInfo->setText(info);
    showPreview(items);
    if (!warnings.isEmpty()) {
        NdMessageBox::warning(this, QStringLiteral("原表数据异常"),
            QStringLiteral("检测到原表有数据被改动，为避免后面的行整体错位，导入时按以下方式替代：\n\n%1\n\n"
                           "这些行已保留并参与导入，请在预览表格里核对。")
                .arg(warnings.join(QLatin1Char('\n'))));
    }
}

bool CutTaskPage::parseExcel(const QString &path, QString *modelName, QString *batchNo,
                             QList<CutItem> *items, QString *err, QStringList *warnings) const
{
    QList<QStringList> rows;
    if (!XlsReader::read(path, rows, err))
        return false;

    // 标题区按「标签 + 同行右侧最近的非空单元格」提取。
    // 生产表有合并单元格，XlsReader 可能丢掉前置空格，不能假定标签固定在 B 列、值固定在 C 列。
    auto labeledValue = [&rows](const QString &label, const QStringList &stopLabels,
                                int maxRows) -> QString {
        for (int r = 0; r < qMin(maxRows, rows.size()); ++r) {
            const QStringList &row = rows.at(r);
            for (int c = 0; c < row.size(); ++c) {
                const QString v = row.at(c).trimmed();
                const int pos = v.indexOf(label);
                if (pos < 0) continue;
                int at = pos + label.size();
                while (at < v.size() &&
                       (v.at(at) == QLatin1Char(':') || v.mid(at, 1) == QStringLiteral("：")))
                    ++at;
                const QString sameCell = v.mid(at).trimmed();
                if (!sameCell.isEmpty())
                    return sameCell;
                for (int nc = c + 1; nc < row.size(); ++nc) {
                    const QString val = row.at(nc).trimmed();
                    if (val.isEmpty())
                        continue;
                    bool isNextLabel = false;
                    for (const QString &stop : stopLabels) {
                        if (val.contains(stop)) { isNextLabel = true; break; }
                    }
                    if (isNextLabel)
                        break;
                    return val;
                }
            }
        }
        return QString();
    };
    const QStringList titleStops = {
        QStringLiteral("型号"), QStringLiteral("批次号"), QStringLiteral("数量"),
        QStringLiteral("打印份数"), QStringLiteral("打印到"), QStringLiteral("只打印"),
        QStringLiteral("从")
    };

    // 线束型号：优先取「型号：」右侧单元格，值形如 ND-HY-FT-F008 （D000）
    if (modelName) {
        *modelName = labeledValue(QStringLiteral("型号"), titleStops, 3);
        if (!modelName->contains(QStringLiteral("ND-"))) {
            modelName->clear();
            for (int r = 0; r < qMin(3, rows.size()) && modelName->isEmpty(); ++r) {
                for (int c = 0; c < rows.at(r).size(); ++c) {
                    const QString v = rows.at(r).at(c).trimmed();
                    if (v.contains(QStringLiteral("ND-")) &&
                        (v.contains(QStringLiteral("(")) || v.contains(QStringLiteral("（")))) {
                        *modelName = v;
                        break;
                    }
                }
            }
        }
        if (modelName->isEmpty())
            *modelName = QFileInfo(path).baseName();
    }

    // 批次号：优先取「批次号：」右侧单元格，例如「样品」；不允许被「从/打印到」等标签带偏
    if (batchNo) {
        *batchNo = labeledValue(QStringLiteral("批次号"), titleStops, 30);
        if (batchNo->isEmpty()) {
            for (int r = 0; r < qMin(30, rows.size()) && batchNo->isEmpty(); ++r) {
                for (int c = 0; c < rows.at(r).size(); ++c) {
                    const QString v = rows.at(r).at(c).trimmed();
                    if (!v.contains(QStringLiteral("批次号"))) continue;
                    const int pos = v.indexOf(QStringLiteral("批次号")) + 4;
                    int at = pos;
                    while (at < v.size() &&
                           (v.at(at) == QLatin1Char(':') || v.mid(at, 1) == QStringLiteral("：")))
                        ++at;
                    const QString same = v.mid(at).trimmed();
                    if (!same.isEmpty()) { *batchNo = same; break; }
                    for (int nc = c + 1; nc < rows.at(r).size(); ++nc) {
                        const QString val = rows.at(r).at(nc).trimmed();
                        if (val.isEmpty()) continue;
                        bool isNextLabel = false;
                        for (const QString &stop : titleStops) {
                            if (val.contains(stop)) { isNextLabel = true; break; }
                        }
                        if (isNextLabel) break;
                        *batchNo = val;
                        break;
                    }
                }
            }
        }
        if (batchNo->isEmpty()) *batchNo = modelName ? *modelName : QString();
    }

    // 实际生产 Excel 有合并单元格，XlsReader 读出来列会错位：
    //   C0 是标签模板残留垃圾列；真正的回路编号在 C1，材质 C2，截面 C3，
    //   颜色 C4，长度 C5，左/右剥皮 C6/C7，左/右端子 C8/C9，压前作业 C10/C11。
    // 用「长度列(C5)是数字 且 左端子列(C8)非空 且 回路列(C1)非空」识别数据行，
    // 跳过标题/参数/表头/「列N」说明行。
    auto cell = [&rows](int r, int c) -> QString {
        if (r < 0 || r >= rows.size() || c < 0 || c >= rows.at(r).size()) return QString();
        return rows.at(r).at(c).trimmed();
    };
    const int C_CIRCUIT = 1, C_MAT = 2, C_SEC = 3, C_COLOR = 4, C_LEN = 5;
    const int C_SL = 6, C_SR = 7, C_TL = 8, C_TR = 9, C_WL = 10, C_WR = 11;
    auto usableMaterial = [](const QString &v) {
        return !v.isEmpty() && !v.contains(QStringLiteral("剥皮"))
            && !v.contains(QStringLiteral("端子")) && !v.contains(QStringLiteral("材质"))
            && !v.contains(QStringLiteral("作业"));
    };

    // 序号列：本表 A 列（C0）就是「序号」，若表头出现在别的列则以表头为准。
    // 数据行的序号直接取原值，保证「原表序号」与 Excel 完全一致；此前按检测到的
    // 数据行自动计数，中途漏掉/多算一行就会整体错位（如 Excel 184 行是 K06，导入后显示 K07）。
    int seqCol = 0;
    for (int r = 0; r < qMin(40, rows.size()); ++r) {
        bool found = false;
        for (int c = 0; c < qMin(4, rows.at(r).size()); ++c) {
            if (rows.at(r).at(c).trimmed().contains(QStringLiteral("序号"))) {
                seqCol = c;
                found = true;
                break;
            }
        }
        if (found) break;
    }

    int autoSeq = 0;
    for (int r = 0; r < rows.size(); ++r) {
        const QString circuit = cell(r, C_CIRCUIT);
        const QString terminalLeft = cell(r, C_TL);
        if (circuit.isEmpty() || terminalLeft.isEmpty()) continue;

        bool seqOk = false;
        const int excelSeq = cell(r, seqCol).toInt(&seqOk);
        const bool hasSeq = seqOk && excelSeq > 0;

        const QString len = cell(r, C_LEN);
        bool lenOk = false;
        len.toDouble(&lenOk);
        bool secOk = false;
        cell(r, C_SEC).toDouble(&secOk);

        // 数据行判定：回路 + 左端子非空，且长度或截面积至少有一个是数字。
        // 表尾「不下料 / 卡给压接」的汇总行这些列全空，仍会被跳过。
        if (!lenOk && !secOk) continue;

        // 原表被手工改过（长度写成「取消」或留空）时不能直接丢掉这一行：
        // 少一行会让后面的「原表序号 → 明细」整体错位。保留该行，长度用 0 占位并给出提示。
        const bool firstDataRow = (autoSeq == 0);
        CutItem it;
        ++autoSeq;
        it.seq = hasSeq ? excelSeq : autoSeq;
        if (!lenOk && warnings) {
            const QString where = hasSeq
                ? QStringLiteral("原表序号 %1").arg(excelSeq)
                : QStringLiteral("原表第 %1 行").arg(r + 1);
            warnings->append(QStringLiteral("%1（回路 %2）：长度「%3」不是数字，已用 0 替代")
                                 .arg(where, circuit,
                                      len.isEmpty() ? QStringLiteral("空") : len));
        }
        it.circuit = circuit;
        QString mat = cell(r, C_MAT);
        // 合并单元格导致个别行材质列读到表头残留（如「右剥皮」），这些值不是材质，置空待补
        if (!usableMaterial(mat)) mat.clear();
        // 首条明细的材质单元格偶尔被 Excel 合并单元格读空：用下一条明细的材质补齐
        if (mat.isEmpty() && firstDataRow) {
            for (int nr = r + 1; nr < rows.size(); ++nr) {
                const QString next = cell(nr, C_MAT);
                if (!usableMaterial(next)) continue;
                mat = next;
                break;
            }
        }
        it.material = mat;
        it.section = cell(r, C_SEC);
        it.color = cell(r, C_COLOR);
        it.wireLength = lenOk ? len : QStringLiteral("0");
        it.stripLeft = cell(r, C_SL);
        it.stripRight = cell(r, C_SR);
        it.terminalLeft = cell(r, C_TL);
        it.terminalRight = cell(r, C_TR);
        it.workLeft = cell(r, C_WL);
        it.workRight = cell(r, C_WR);
        // 原表末尾的「机台」列不再导入：机台一律由派单规则 + 线径能力 + 均衡决定
        items->append(it);
    }
    if (items->isEmpty()) {
        if (err) *err = QStringLiteral("未解析到数据行（需包含回路编号、长度、左端子）");
        return false;
    }
    return true;
}

void CutTaskPage::showPreview(const QList<CutItem> &items)
{
    const QStringList headers = { QStringLiteral("序号"), QStringLiteral("回路编号"),
        QStringLiteral("材质"), QStringLiteral("截面"), QStringLiteral("颜色"),
        QStringLiteral("长度"), QStringLiteral("左剥"), QStringLiteral("右剥"),
        QStringLiteral("左端子"), QStringLiteral("右端子"),
        QStringLiteral("左压前作业"), QStringLiteral("右压前作业") };
    m_previewTable->clear();
    setCenteredHeaders(m_previewTable, headers);
    m_previewTable->setRowCount(items.size());
    for (int r = 0; r < items.size(); ++r) {
        const CutItem &it = items.at(r);
        const QStringList row = { QString::number(it.seq), it.circuit, it.material,
            it.section, it.color, it.wireLength, it.stripLeft, it.stripRight,
            it.terminalLeft, it.terminalRight, it.workLeft, it.workRight };
        for (int c = 0; c < row.size(); ++c)
            m_previewTable->setItem(r, c, centeredItem(row.at(c)));
    }
    // 列宽填满表格可视区：所有列一屏显示；文字过长自动换行；彻底禁止横向滚动
    QHeaderView *hh = m_previewTable->horizontalHeader();
    hh->setMinimumSectionSize(100);                     // 全局最小列宽，避免被挤坏
    // 先设固定宽度（确保表头文字完整），再把几列改 Stretch 吸收剩余空间
    const int fixedWidths[] = { 50, 115, 80, 60, 55, 80, 55, 55, 120, 120, 130, 130 };
    for (int c = 0; c < headers.size(); ++c) {
        m_previewTable->setColumnWidth(c, fixedWidths[c]);
        if (c == 10 || c == 11)
            hh->setSectionResizeMode(c, QHeaderView::Stretch);  // 左/右压前作业列吸收剩余
        else
            hh->setSectionResizeMode(c, QHeaderView::Fixed);
    }
    for (int r = 0; r < m_previewTable->rowCount(); ++r)
        m_previewTable->setRowHeight(r, 76);
    m_previewTable->horizontalScrollBar()->setValue(0);
}

void CutTaskPage::onImport()
{
    if (m_parsedItems.isEmpty()) {
        NdMessageBox::information(this, QStringLiteral("下线表导入"),
            QStringLiteral("请先选择并成功解析下线表文件。"));
        return;
    }
    // 批次名称统一取表格中的型号，不再弹窗允许改写，避免和任务表「型号」列不一致
    const QString model = m_parsedModel.trimmed();
    if (model.isEmpty()) {
        NdMessageBox::information(this, QStringLiteral("下线表导入"),
            QStringLiteral("未解析到表格中的型号，请确认线下表标题区「型号：」有值。"));
        return;
    }

    CutBatch batch;
    batch.name = model;
    batch.batchNo = m_parsedBatchNo.trimmed().isEmpty() ? batch.name : m_parsedBatchNo.trimmed();
    batch.fileName = QFileInfo(m_excelPath).fileName();
    batch.creator = m_currentUser;
    QString err;
    const int id = DatabaseManager::instance().cutImportBatch(batch, m_parsedItems, &err);
    if (id < 0) {
        NdMessageBox::information(this, QStringLiteral("导入失败"),
            err.isEmpty() ? QStringLiteral("导入失败。") : err);
        return;
    }
    refreshBatches();
    refreshBatchTable();
    const int batchIdx = m_cmbBatch->findData(id);
    if (batchIdx >= 0) m_cmbBatch->setCurrentIndex(batchIdx);
    refreshTasks();
    NdMessageBox::information(this, QStringLiteral("导入完成"),
        QStringLiteral("已建立线束「%1」，共 %2 条明细。请到「任务与派工」页派工。")
        .arg(batch.name).arg(m_parsedItems.size()));
}

void CutTaskPage::onRefresh()
{
    refreshBatches();
    refreshTasks();
}

// 智能分配：只生成建议（预览），不写任务表；确认后才正式派工
void CutTaskPage::onAutoDispatch()
{
    const int batchId = m_cmbBatch->currentData().toInt();
    if (batchId <= 0) {
        NdMessageBox::information(this, QStringLiteral("智能分配"),
            QStringLiteral("请先选择一个具体线束（型号），再生成分配建议。"));
        return;
    }
    if (!m_previewTasks.isEmpty()
        && !NdMessageBox::question(this, QStringLiteral("智能分配"),
               QStringLiteral("已有未确认的预览分配，重新生成会丢弃上次调整。继续吗？"))) {
        return;
    }
    int qty = 1;
    if (!NdInputDialog::getInt(this, QStringLiteral("智能分配"),
            QStringLiteral("每条任务数量："), 1, 1, 100000, &qty)) {
        return;
    }

    QList<CutTask> preview;
    int fixed = 0, balanced = 0;
    QString err;
    if (!DatabaseManager::instance().cutAutoDispatchPreview(batchId, &preview,
                                                            &fixed, &balanced, &err)) {
        NdMessageBox::information(this, QStringLiteral("智能分配失败"),
            err.isEmpty() ? QStringLiteral("智能分配失败。") : err);
        return;
    }
    if (preview.isEmpty()) {
        NdMessageBox::information(this, QStringLiteral("智能分配"),
            QStringLiteral("该线束没有需要分配的明细（可能已全部派工）。"));
        return;
    }
    m_previewTasks = preview;
    m_previewBatchId = batchId;
    m_previewQty = qty;
    m_btnCommit->setEnabled(true);
    refreshTasks();
    int unassigned = 0;
    for (const CutTask &t : preview) if (t.machineId <= 0) ++unassigned;
    QString msg = QStringLiteral("已生成 %1 条建议：防水栓/端子等规则锁定 %2 条，"
                                 "按线径能力+线色归组并均衡分配 %3 条。\n"
                                 "表格顶部「待确认」行的机台格双击可修改；核对后点「确认派工」"
                                 "才会正式写入任务。")
        .arg(preview.size()).arg(fixed).arg(balanced);
    if (unassigned > 0)
        msg += QStringLiteral("\n其中 %1 条没有匹配的线径规则，已分给「其他」（人工/手动下线）。")
            .arg(unassigned);
    int otherId = 0, otherCount = 0;
    for (const CutMachine &m : DatabaseManager::instance().cutMachines())
        if (m.code == QLatin1String("OTHER")) { otherId = m.id; break; }
    for (const CutTask &t : preview)
        if (otherId > 0 && t.machineId == otherId) ++otherCount;
    if (otherCount > 0)
        msg += QStringLiteral("\n共 %1 条归「其他」（人工/手动，含屏蔽件及无线径匹配的明细）。")
            .arg(otherCount);
    NdMessageBox::information(this, QStringLiteral("智能分配预览"), msg);
}

// 确认派工：把预览中（可能已调整机台）的建议一次性写成正式任务
void CutTaskPage::onCommitDispatch()
{
    if (m_previewTasks.isEmpty()) {
        NdMessageBox::information(this, QStringLiteral("确认派工"),
            QStringLiteral("请先点「智能分配」生成预览，再确认派工。"));
        return;
    }
    int unassigned = 0;
    for (const CutTask &t : m_previewTasks) if (t.machineId <= 0) ++unassigned;
    if (unassigned > 0) {
        NdMessageBox::information(this, QStringLiteral("确认派工"),
            QStringLiteral("还有 %1 条任务未指定机台（状态为「未分配」，通常是没有匹配的线径规则）。\n"
                           "请双击这些行的机台格指定机台后，再确认派工。").arg(unassigned));
        return;
    }
    if (!NdMessageBox::question(this, QStringLiteral("确认派工"),
            QStringLiteral("确定把 %1 条待确认任务正式派到各机台吗？\n派工后将出现在对应机台的任务列表中。")
            .arg(m_previewTasks.size()))) {
        return;
    }
    QList<CutAssign> assigns;
    for (const CutTask &t : m_previewTasks) assigns.append({t.itemId, t.machineId});
    QString err;
    if (!DatabaseManager::instance().cutCommitDispatch(m_previewBatchId, assigns,
                                                       m_previewQty, &err)) {
        NdMessageBox::information(this, QStringLiteral("派工失败"),
            err.isEmpty() ? QStringLiteral("确认派工失败。") : err);
        return;
    }
    m_previewTasks.clear();
    m_previewBatchId = 0;
    m_btnCommit->setEnabled(false);
    refreshTasks();
    NdMessageBox::information(this, QStringLiteral("派工完成"),
        QStringLiteral("已确认派工 %1 条任务。").arg(assigns.size()));
}

void CutTaskPage::refreshBatches()
{
    const int prev = m_cmbBatch->currentData().toInt();
    m_cmbBatch->clear();
    m_cmbBatch->addItem(QStringLiteral("全部型号"), 0);
    for (const CutBatch &b : DatabaseManager::instance().cutBatches())
        m_cmbBatch->addItem(b.name, b.id);
    if (prev > 0) {
        const int idx = m_cmbBatch->findData(prev);
        if (idx >= 0) m_cmbBatch->setCurrentIndex(idx);
    }
    // 机台
    const int prevM = m_cmbMachine->currentData().toInt();
    m_cmbMachine->clear();
    m_cmbMachine->addItem(QStringLiteral("全部机台"), 0);
    for (const CutMachine &m : DatabaseManager::instance().cutMachines())
        m_cmbMachine->addItem(m.name, m.id);
    if (prevM > 0) {
        const int idx = m_cmbMachine->findData(prevM);
        if (idx >= 0) m_cmbMachine->setCurrentIndex(idx);
    }
}

void CutTaskPage::refreshTasks()
{
    const int batchId = m_cmbBatch->currentData().toInt();
    const int machineId = m_cmbMachine->currentData().toInt();
    const QString status = m_cmbStatus->currentData().toString();
    const QList<CutTask> tasks = DatabaseManager::instance().cutTasks(batchId, machineId, status);
    // 任务号按「整个批次的全量任务」编号，避免筛选后同一任务号变化
    QHash<int, int> taskNumbers;
    QHash<int, int> numberSeed;
    for (const CutTask &t : DatabaseManager::instance().cutTasks(batchId, 0,
                                                                  QStringLiteral("all"))) {
        ++numberSeed[t.batchId];
        taskNumbers.insert(t.id, numberSeed.value(t.batchId));
    }

    const QStringList headers = { QStringLiteral("序号"), QStringLiteral("任务号"),
        QStringLiteral("型号"), QStringLiteral("批次号"), QStringLiteral("原表序号"),
        QStringLiteral("回路"), QStringLiteral("截面"), QStringLiteral("颜色"),
        QStringLiteral("长度"), QStringLiteral("左压前"), QStringLiteral("右压前"),
        QStringLiteral("机台"), QStringLiteral("数量"), QStringLiteral("完成"),
        QStringLiteral("状态"), QStringLiteral("操作员") };
    m_taskTable->clear();
    // QTableWidget::clear() 只清内容不重置行数：必须显式归零，
    // 否则反复刷新会在旧空行后面继续追加，表现为「表格空白且越刷越卡」
    m_taskTable->setRowCount(0);
    setCenteredHeaders(m_taskTable, headers);

    // 型号 / 批次号 / 机台名映射
    QHash<int, QString> batchNames;
    QHash<int, QString> batchNos;
    QList<CutMachine> machines = DatabaseManager::instance().cutMachines();
    for (const CutBatch &b : DatabaseManager::instance().cutBatches()) {
        batchNames[b.id] = b.name;
        batchNos[b.id] = b.batchNo;
    }
    QHash<int, QString> machineNames;
    for (const CutMachine &m : machines) machineNames[m.id] = m.name;

    // 任务号 = 批次导入时间 + 该批次内任务序号，不再显示全局自增 ID
    int maxTaskNo = 0;
    for (int n : taskNumbers.values()) maxTaskNo = qMax(maxTaskNo, n);
    auto taskNo = [](const CutTask &t, int index) {
        const QDateTime dt = QDateTime::fromString(t.batchCreatedAt, Qt::ISODate);
        const QString stamp = dt.isValid() ? dt.toString(QStringLiteral("yyyyMMddHHmm"))
                                           : QStringLiteral("000000000000");
        return QStringLiteral("%1-%2").arg(stamp).arg(index);
    };
    // previewIndex=0 为正式任务，>0 为预览行在 m_previewTasks 中的序号（1 起）
    auto fillRow = [this](int row, const QStringList &rowData, int previewIndex) {
        for (int c = 0; c < rowData.size(); ++c) {
            QTableWidgetItem *cell = centeredItem(rowData.at(c));
            cell->setFlags(cell->flags() & ~Qt::ItemIsEditable);
            // 左压前 / 右压前：长文本手动折行（Qt 6 的 Alignment 已不支持 TextWordWrap）
            if (c == 9 || c == 10) {
                const QString txt = rowData.at(c);
                QString wrapped;
                for (int i = 0; i < txt.size(); i += 9) {
                    if (!wrapped.isEmpty()) wrapped += QLatin1Char('\n');
                    wrapped += txt.mid(i, 9);
                }
                cell->setText(wrapped);
                cell->setTextAlignment(Qt::AlignCenter);
            }
            if (previewIndex > 0) {
                if (c == 0) cell->setData(Qt::UserRole, previewIndex);
                if (c == 11) cell->setFlags(cell->flags() | Qt::ItemIsEditable);
            } else if (rowData.size() > 14) {
                const QString state = rowData.at(14);   // 状态列（15=操作员）
                if (c == 11 && state == QStringLiteral("待生产")) {
                    // 已派工但待生产的任务也允许改派机台
                    cell->setFlags(cell->flags() | Qt::ItemIsEditable);
                } else if (c == 14 && (state == QStringLiteral("待生产")
                                       || state == QStringLiteral("进行中"))) {
                    cell->setFlags(cell->flags() | Qt::ItemIsEditable);
                }
            }
            m_taskTable->setItem(row, c, cell);
        }
    };

    // 待确认的预览行放在最前面；机台列双击可输入机台名/编号。
    // 注意：QTableWidget::clear() 不会删除 setCellWidget 的控件，
    // 244 条预览反复刷新会累积大量控件导致界面卡死，因此这里只用单元格文本。
    int previewCount = 0;
    const bool showPreview = !m_previewTasks.isEmpty() && status == QLatin1String("all")
                             && (batchId == 0 || batchId == m_previewBatchId);
    if (showPreview) {
        for (int i = 0; i < m_previewTasks.size(); ++i) {
            const CutTask &t = m_previewTasks.at(i);
            if (machineId > 0 && t.machineId != machineId) continue;
            const int r = m_taskTable->rowCount();
            m_taskTable->insertRow(r);
            QString stampNo = taskNo(t, maxTaskNo + i + 1);
            const bool unassigned = (t.machineId <= 0);
            fillRow(r, { QString::number(r + 1), QStringLiteral("待确认 %1").arg(stampNo),
                batchNames.value(t.batchId), batchNos.value(t.batchId),
                QString::number(t.seq), t.circuit,
                t.section, t.color, t.wireLength, t.workLeft, t.workRight,
                unassigned ? QString() : machineNames.value(t.machineId),
                QString::number(t.quantity), QStringLiteral("0"),
                unassigned ? QStringLiteral("未分配") : QStringLiteral("待确认"),
                QString() }, i + 1);
            ++previewCount;
        }
    }

    int done = 0;
    for (const CutTask &t : tasks) {
        if (t.status == 3) ++done;
        const int r = m_taskTable->rowCount();
        m_taskTable->insertRow(r);
        fillRow(r, { QString::number(r + 1), taskNo(t, taskNumbers.value(t.id)),
            batchNames.value(t.batchId), batchNos.value(t.batchId),
            QString::number(t.seq), t.circuit,
            t.section, t.color, t.wireLength, t.workLeft, t.workRight,
            machineNames.value(t.machineId), QString::number(t.quantity),
            QString::number(t.doneQty), CutTask::statusText(t.status), t.operName }, 0);
        m_taskTable->item(r, 0)->setData(Qt::UserRole + 1, t.id);
    }
    // 列宽填满表格可视区：所有列一屏显示；文字过长自动换行；彻底禁止横向滚动
    QHeaderView *hh = m_taskTable->horizontalHeader();
    hh->setMinimumSectionSize(60);                     // 全局最小列宽，允许更窄
    // 先设固定宽度（缩小总和让小窗口也能容下）
    const int fixedWidths[] = { 48, 130, 100, 70, 60, 64, 56, 54, 64, 110, 110, 72, 54, 54, 72, 72 };
    for (int c = 0; c < headers.size(); ++c) {
        m_taskTable->setColumnWidth(c, fixedWidths[c]);
        // Stretch 列：型号(2)、左压前作业(9)、右压前作业(10) 吸收剩余空间
        // 固定列：序号(0)、任务号(1)、批次号(3)、原表序号(4)、回路(5)、截面(6)、颜色(7)、长度(8)、机台(11)、状态(14)、操作员(15)
        if (c == 2 || c == 9 || c == 10)
            hh->setSectionResizeMode(c, QHeaderView::Stretch);
        else
            hh->setSectionResizeMode(c, QHeaderView::Fixed);
    }
    hh->setStretchLastSection(true);  // 最后一列也自动拉伸填充
    // 行高统一加高，配合 setWordWrap 让长内容折行显示
    for (int r = 0; r < m_taskTable->rowCount(); ++r)
        m_taskTable->setRowHeight(r, 84);
    m_taskTable->horizontalScrollBar()->setValue(0);

    QString info = QStringLiteral("共 %1 条任务，已完成 %2 条").arg(tasks.size()).arg(done);
    if (!m_previewTasks.isEmpty()) {
        if (previewCount > 0)
            info += QStringLiteral("；另有 %1 条待确认分配（共 %2 条）")
                .arg(previewCount).arg(m_previewTasks.size());
        else
            info += QStringLiteral("；当前筛选下无待确认分配（共 %1 条，可切到「全部机台」查看）")
                .arg(m_previewTasks.size());
        QHash<int, int> machineLoad;
        for (const CutTask &t : m_previewTasks) ++machineLoad[t.machineId];
        QHash<int, int> load = machineLoad;
        int otherId = 0;
        for (const CutMachine &m : machines) {
            if (m.code == QLatin1String("OTHER") || m.name == QStringLiteral("其他")) {
                otherId = m.id;
                break;
            }
        }
        // 未分配(0)统一并入「其他」，并放到最后显示
        if (otherId > 0 && load.contains(0)) {
            load[otherId] += load.value(0);
            load.remove(0);
        }
        QList<int> order;
        for (const CutMachine &m : machines) {
            if (m.id != otherId) order << m.id;   // cutMachines() 已按 id 排序：一号机~五号机
        }
        if (otherId > 0) order << otherId;
        else if (load.contains(0)) order << 0;
        for (int id : load.keys()) if (!order.contains(id)) order << id;

        QStringList dist;
        for (int id : order) {
            if (!load.contains(id)) continue;
            const QString name = (id == 0 || id == otherId)
                ? QStringLiteral("其他")
                : machineNames.value(id, QStringLiteral("机台%1").arg(id));
            dist << QStringLiteral("%1 %2条").arg(name).arg(load.value(id));
        }
        info += QStringLiteral("；预览机台分布：%1").arg(dist.join(QStringLiteral("、")));
    }
    m_lblTaskInfo->setText(info);
    m_btnCommit->setEnabled(m_ruleEditable && !m_previewTasks.isEmpty());
}

// 预览行的机台格被用户修改：校验机台并写回 m_previewTasks
void CutTaskPage::onTaskCellChanged(int row, int column)
{
    if (column != 11 || row < 0 || row >= m_taskTable->rowCount()) return;
    QTableWidgetItem *idCell = m_taskTable->item(row, 0);
    QTableWidgetItem *machineCell = m_taskTable->item(row, column);
    if (!idCell || !machineCell) return;
    const int idx = idCell->data(Qt::UserRole).toInt();       // >0 = 预览行序号
    const int taskId = idCell->data(Qt::UserRole + 1).toInt(); // >0 = 已派工任务 id
    if (idx <= 0 && taskId <= 0) return;
    const QString oldMachineText = machineCell->text().trimmed();

    const QList<CutMachine> machines = DatabaseManager::instance().cutMachines();
    QHash<int, QString> names;
    const QString text = machineCell->text().trimmed();
    int mid = 0;
    for (const CutMachine &m : machines) {
        names[m.id] = m.name;
        if (m.name == text || m.code == text) mid = m.id;
    }
    if (mid <= 0) {
        bool ok = false;
        const int n = text.toInt(&ok);
        if (ok && n >= 1 && n <= machines.size()) mid = machines.at(n - 1).id;
    }
    auto restore = [this, machineCell, &oldMachineText]() {
        m_taskTable->blockSignals(true);
        machineCell->setText(oldMachineText);
        m_taskTable->blockSignals(false);
    };
    if (mid <= 0) {
        NdMessageBox::information(this, QStringLiteral("机台无效"),
            QStringLiteral("请选择有效机台（一号机～五号机）。"));
        restore();
        return;
    }
    if (idx > 0 && idx <= m_previewTasks.size()) {
        m_previewTasks[idx - 1].machineId = mid;
        restore();   // 用机台标准名覆盖显示
        m_taskTable->blockSignals(true);
        machineCell->setText(names.value(mid));
        if (QTableWidgetItem *stateCell = m_taskTable->item(row, 14)) {
            if (stateCell->text() == QStringLiteral("未分配"))
                stateCell->setText(QStringLiteral("待确认"));
        }
        m_taskTable->blockSignals(false);
        return;
    }

    // 已派工任务：确认后直接改派数据库
    if (!m_ruleEditable) {
        NdMessageBox::information(this, QStringLiteral("任务改派"),
            QStringLiteral("请先登录，再改派已派工的任务。"));
        restore();
        return;
    }
    if (!NdMessageBox::question(this, QStringLiteral("任务改派"),
            QStringLiteral("确定把该任务从「%1」改派到「%2」吗？\n任务数量与状态保持不变。")
            .arg(oldMachineText, names.value(mid)))) {
        restore();
        return;
    }
    QString err;
    if (!DatabaseManager::instance().cutRetask(taskId, mid, &err)) {
        NdMessageBox::information(this, QStringLiteral("改派失败"),
            err.isEmpty() ? QStringLiteral("任务改派失败。") : err);
        restore();
        return;
    }
    refreshTasks();
    NdMessageBox::information(this, QStringLiteral("任务改派"),
        QStringLiteral("任务已改派到「%1」。").arg(names.value(mid)));
}

// 正式任务状态被手动改为「已完成」：走原有完成回传接口写库
void CutTaskPage::onTaskStatusChanged(int row, int column)
{
    if (column != 14 || row < 0 || row >= m_taskTable->rowCount()) return;
    QTableWidgetItem *idCell = m_taskTable->item(row, 0);
    QTableWidgetItem *stateCell = m_taskTable->item(row, column);
    if (!idCell || !stateCell) return;
    const int taskId = idCell->data(Qt::UserRole + 1).toInt();
    if (taskId <= 0) return;   // 预览行的状态不允许手动改

    if (stateCell->text().trimmed() != QStringLiteral("已完成")) {
        refreshTasks();   // 未选「已完成」，按数据库原值恢复
        return;
    }
    if (!m_ruleEditable) {
        NdMessageBox::information(this, QStringLiteral("任务完成"),
            QStringLiteral("请先登录，再标记任务已完成。"));
        refreshTasks();
        return;
    }
    if (!NdMessageBox::question(this, QStringLiteral("任务完成"),
            QStringLiteral("确定把该任务标记为「已完成」吗？\n完成后该任务不能再改派机台。"))) {
        refreshTasks();
        return;
    }
    QTableWidgetItem *qtyCell = m_taskTable->item(row, 12);
    int qty = qtyCell ? qtyCell->text().toInt() : 1;
    if (qty <= 0) qty = 1;
    QString err;
    if (!DatabaseManager::instance().cutFinishTask(taskId, qty, m_currentUser, &err)) {
        NdMessageBox::information(this, QStringLiteral("操作失败"),
            err.isEmpty() ? QStringLiteral("标记任务完成失败。") : err);
        refreshTasks();
        return;
    }
    refreshTasks();
    NdMessageBox::information(this, QStringLiteral("任务完成"),
        QStringLiteral("任务已标记为已完成。"));
}

// ==================== 线束管理 ====================
void CutTaskPage::refreshBatchTable()
{
    const QList<CutBatch> batches = DatabaseManager::instance().cutBatches();
    const QStringList headers = { QStringLiteral("线束ID"), QStringLiteral("型号"),
        QStringLiteral("批次号"), QStringLiteral("来源文件"), QStringLiteral("明细数"),
        QStringLiteral("导入人"), QStringLiteral("导入时间") };
    m_batchTable->clear();
    setCenteredHeaders(m_batchTable, headers);
    m_batchTable->setRowCount(batches.size());
    for (int r = 0; r < batches.size(); ++r) {
        const CutBatch &b = batches.at(r);
        QTableWidgetItem *idItem = centeredItem(QString::number(b.id));
        idItem->setData(Qt::UserRole, b.id);
        m_batchTable->setItem(r, 0, idItem);
        const QStringList row = { QString(), b.name, b.batchNo, b.fileName,
            QString::number(b.totalRows), b.creator, b.createdAt };
        for (int c = 1; c < row.size(); ++c)
            m_batchTable->setItem(r, c, centeredItem(row.at(c)));
    }
    m_batchTable->resizeColumnsToContents();
    m_lblBatchInfo->setText(QStringLiteral("共 %1 套线束").arg(batches.size()));
}

void CutTaskPage::onDeleteBatch()
{
    const int row = m_batchTable->currentRow();
    if (row < 0 || !m_batchTable->item(row, 0)) {
        NdMessageBox::information(this, QStringLiteral("线束管理"),
            QStringLiteral("请先在下方列表中选中要删除的线束。"));
        return;
    }
    // UserRole 是首选；旧列表/异常刷新丢失 UserRole 时退回第 0 列文本，避免把 0 当 ID 传下去
    int batchId = m_batchTable->item(row, 0)->data(Qt::UserRole).toInt();
    if (batchId <= 0) batchId = m_batchTable->item(row, 0)->text().trimmed().toInt();
    const QString name = m_batchTable->item(row, 1) ? m_batchTable->item(row, 1)->text() : QString();
    if (batchId <= 0) {
        refreshBatchTable();
        NdMessageBox::information(this, QStringLiteral("删除线束"),
            QStringLiteral("没有读到有效的线束 ID，列表已刷新，请重新选中后再删除。"));
        return;
    }
    if (!NdMessageBox::question(this, QStringLiteral("删除线束"),
            QStringLiteral("确定删除线束「%1」吗？\n该线束的全部明细和已生成的派工任务都会被删除，且不可恢复。")
            .arg(name))) {
        return;
    }
    QString err;
    if (!DatabaseManager::instance().cutDeleteBatch(batchId, &err)) {
        NdMessageBox::information(this, QStringLiteral("删除失败"),
            err.isEmpty() ? QStringLiteral("删除线束失败。") : err);
        return;
    }
    // 删除后回查确认：数据库里确实没有这个 ID 才算成功，否则提示而不是假装删掉了
    for (const CutBatch &b : DatabaseManager::instance().cutBatches()) {
        if (b.id == batchId) {
            refreshBatches();
            refreshBatchTable();
            refreshTasks();
            NdMessageBox::information(this, QStringLiteral("删除未生效"),
                QStringLiteral("线束「%1」仍在数据库中，可能是旧数据被自动回填，请重启软件后再删除一次。")
                .arg(name));
            return;
        }
    }
    if (m_previewBatchId == batchId) {
        m_previewTasks.clear();
        m_previewBatchId = 0;
    }
    refreshBatches();
    refreshBatchTable();
    refreshTasks();
    NdMessageBox::information(this, QStringLiteral("删除完成"),
        QStringLiteral("线束「%1」已删除。").arg(name));
}

// ==================== 派单规则 ====================
void CutTaskPage::refreshRules()
{
    // 机台下拉（保留当前选择）
    const int prevM = m_cmbRuleMachine->currentData().toInt();
    m_cmbRuleMachine->clear();
    // 「其他」=人工/手动下线，也允许作为规则目标；它按 id 排在五台机之后
    for (const CutMachine &m : DatabaseManager::instance().cutMachines())
        m_cmbRuleMachine->addItem(m.name, m.id);
    if (prevM > 0) {
        const int idx = m_cmbRuleMachine->findData(prevM);
        if (idx >= 0) m_cmbRuleMachine->setCurrentIndex(idx);
    }

    QHash<int, QString> machineNames;
    for (const CutMachine &m : DatabaseManager::instance().cutMachines())
        machineNames[m.id] = m.name;

    const QList<CutRule> rules = DatabaseManager::instance().cutRules();
    const QStringList headers = { QStringLiteral("状态"), QStringLiteral("匹配字段"),
        QStringLiteral("匹配方式"), QStringLiteral("匹配值"), QStringLiteral("目标机台") };
    m_ruleTable->clear();
    setCenteredHeaders(m_ruleTable, headers);
    m_ruleTable->setRowCount(rules.size());
    int enabledCount = 0;
    for (int r = 0; r < rules.size(); ++r) {
        const CutRule &rule = rules.at(r);
        if (rule.enabled) ++enabledCount;
        QTableWidgetItem *stateItem = centeredItem(rule.enabled ? QStringLiteral("启用")
                                                               : QStringLiteral("停用"));
        stateItem->setData(Qt::UserRole, rule.id);
        m_ruleTable->setItem(r, 0, stateItem);
        const QString mName = machineNames.value(rule.machineId,
            QStringLiteral("机台%1").arg(rule.machineId));
        const QStringList row = { CutRule::fieldText(rule.field),
            CutRule::matchText(rule.isRange() ? QStringLiteral("range") : rule.matchType),
            rule.valueText(), mName };
        for (int c = 0; c < row.size(); ++c)
            m_ruleTable->setItem(r, c + 1, centeredItem(row.at(c)));
    }
    m_ruleTable->resizeColumnsToContents();
    // 匹配值与目标机台两列吸收剩余宽度，让右侧表格列占满可视区
    QHeaderView *ruleHeader = m_ruleTable->horizontalHeader();
    ruleHeader->setSectionResizeMode(3, QHeaderView::Stretch);
    ruleHeader->setSectionResizeMode(4, QHeaderView::Stretch);
    m_lblRuleInfo->setText(QStringLiteral("共 %1 条规则（启用 %2 条），点击表格行可载入编辑")
                           .arg(rules.size()).arg(enabledCount));
}

void CutTaskPage::onRuleRowClicked(int row, int column)
{
    Q_UNUSED(column)
    if (row < 0 || !m_ruleTable->item(row, 0)) return;
    m_editingRuleId = m_ruleTable->item(row, 0)->data(Qt::UserRole).toInt();
    // 按显示文本反查字段/匹配方式 key
    const QString fieldText = m_ruleTable->item(row, 1) ? m_ruleTable->item(row, 1)->text() : QString();
    const QString matchText = m_ruleTable->item(row, 2) ? m_ruleTable->item(row, 2)->text() : QString();
    const QString valueText = m_ruleTable->item(row, 3) ? m_ruleTable->item(row, 3)->text() : QString();
    const QString mName = m_ruleTable->item(row, 4) ? m_ruleTable->item(row, 4)->text() : QString();
    for (int i = 0; i < m_cmbRuleField->count(); ++i)
        if (CutRule::fieldText(m_cmbRuleField->itemData(i).toString()) == fieldText)
            m_cmbRuleField->setCurrentIndex(i);
    updateRuleInputs();   // 索引没变化时信号不会触发，这里显式同步一次
    if (m_cmbRuleField->currentData().toString() == QLatin1String("section")) {
        double lo = 0.0, hi = 0.0;
        if (cutRuleParseRange(valueText, &lo, &hi)) {
            m_spinRuleMin->setValue(lo);
            m_spinRuleMax->setValue(hi);
        }
    } else {
        for (int i = 0; i < m_cmbRuleMatch->count(); ++i)
            if (CutRule::matchText(m_cmbRuleMatch->itemData(i).toString()) == matchText)
                m_cmbRuleMatch->setCurrentIndex(i);
        m_edtRuleValue->setText(valueText);
    }
    const int mi = m_cmbRuleMachine->findText(mName);
    if (mi >= 0) m_cmbRuleMachine->setCurrentIndex(mi);
    m_editingRuleEnabled = (m_ruleTable->item(row, 0)->text() == QStringLiteral("启用")) ? 1 : 0;
    updateRuleButtons();
}

void CutTaskPage::onRuleReset()
{
    m_editingRuleId = 0;
    m_editingRuleEnabled = 1;
    m_edtRuleValue->clear();
    m_spinRuleMin->setValue(0.50);
    m_spinRuleMax->setValue(1.50);
    m_ruleTable->clearSelection();
    updateRuleButtons();
}

// 新增：把表单内容作为一条新规则插入
void CutTaskPage::onRuleAdd()
{
    CutRule rule;            // id = 0 → 新增
    rule.enabled = 1;
    if (!collectRuleForm(&rule)) return;
    QString err;
    if (!DatabaseManager::instance().cutSaveRule(&rule, &err)) {
        NdMessageBox::information(this, QStringLiteral("保存失败"),
            err.isEmpty() ? QStringLiteral("新增派单规则失败。") : err);
        return;
    }
    const QString machineName = m_cmbRuleMachine->currentText();
    onRuleReset();
    refreshRules();
    NdMessageBox::information(this, QStringLiteral("派单规则"),
        QStringLiteral("规则已新增：%1 %2 → %3")
        .arg(CutRule::fieldText(rule.field), rule.valueText(), machineName));
}

// 保存修改：把表单内容写回表格中选中的那条规则
void CutTaskPage::onRuleSave()
{
    if (m_editingRuleId <= 0) {
        NdMessageBox::information(this, QStringLiteral("派单规则"),
            QStringLiteral("「保存修改」用于修改已有规则。\n"
                           "请先在下方表格中点击要修改的规则；新增规则请点「新增规则」。"));
        return;
    }
    CutRule rule;
    rule.id = m_editingRuleId;
    rule.enabled = m_editingRuleEnabled;   // 保持原有启用状态
    if (!collectRuleForm(&rule)) return;
    QString err;
    if (!DatabaseManager::instance().cutSaveRule(&rule, &err)) {
        NdMessageBox::information(this, QStringLiteral("保存失败"),
            err.isEmpty() ? QStringLiteral("保存派单规则失败。") : err);
        return;
    }
    const QString machineName = m_cmbRuleMachine->currentText();
    onRuleReset();
    refreshRules();
    NdMessageBox::information(this, QStringLiteral("派单规则"),
        QStringLiteral("规则已修改：%1 %2 → %3")
        .arg(CutRule::fieldText(rule.field), rule.valueText(), machineName));
}

// 启用 / 禁用选中的规则
void CutTaskPage::onRuleToggle()
{
    if (m_editingRuleId <= 0) {
        NdMessageBox::information(this, QStringLiteral("派单规则"),
            QStringLiteral("请先在表格中选中要启用或禁用的规则。"));
        return;
    }
    CutRule target;
    bool found = false;
    for (const CutRule &r : DatabaseManager::instance().cutRules()) {
        if (r.id == m_editingRuleId) { target = r; found = true; break; }
    }
    if (!found) {
        onRuleReset();
        refreshRules();
        NdMessageBox::information(this, QStringLiteral("派单规则"),
            QStringLiteral("这条规则已不存在，列表已刷新。"));
        return;
    }
    target.enabled = target.enabled ? 0 : 1;
    QString err;
    if (!DatabaseManager::instance().cutSaveRule(&target, &err)) {
        NdMessageBox::information(this, QStringLiteral("操作失败"),
            err.isEmpty() ? QStringLiteral("修改规则状态失败。") : err);
        return;
    }
    m_editingRuleEnabled = target.enabled;
    refreshRules();
    updateRuleButtons();
    const int mi = m_cmbRuleMachine->findData(target.machineId);
    const QString machineName = (mi >= 0) ? m_cmbRuleMachine->itemText(mi)
                                          : QStringLiteral("机台%1").arg(target.machineId);
    NdMessageBox::information(this, QStringLiteral("派单规则"),
        QStringLiteral("规则已%1：%2 %3 → %4")
        .arg(target.enabled ? QStringLiteral("启用") : QStringLiteral("禁用"),
             CutRule::fieldText(target.field), target.valueText(), machineName));
}

void CutTaskPage::onRuleDelete()
{
    if (m_editingRuleId <= 0) {
        NdMessageBox::information(this, QStringLiteral("派单规则"),
            QStringLiteral("请先在表格中选中要删除的规则。"));
        return;
    }
    if (!NdMessageBox::question(this, QStringLiteral("删除规则"),
            QStringLiteral("确定删除这条派单规则吗？"))) {
        return;
    }
    QString err;
    if (!DatabaseManager::instance().cutDeleteRule(m_editingRuleId, &err)) {
        NdMessageBox::information(this, QStringLiteral("删除失败"),
            err.isEmpty() ? QStringLiteral("删除规则失败。") : err);
        return;
    }
    onRuleReset();
    refreshRules();
}
