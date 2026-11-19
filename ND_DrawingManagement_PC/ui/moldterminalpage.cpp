#include "moldterminalpage.h"
#include "ndmessagebox.h"
#include "db/databasemanager.h"

#include <QDialog>
#include <QFormLayout>
#include <QSpinBox>
#include <QTextEdit>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QLineEdit>
#include <QPushButton>
#include <QComboBox>
#include <QTableWidget>
#include <QHeaderView>
#include <QLabel>
#include <QFrame>

MoldTerminalPage::MoldTerminalPage(QWidget *parent) : QWidget(parent)
{
    QVBoxLayout *root = new QVBoxLayout(this);
    root->setContentsMargins(16, 12, 16, 12);
    root->setSpacing(10);

    QLabel *hint = new QLabel(
        QStringLiteral("模具台账与端子适配关系。老模具/刀片可无模具号，标记为「待完善」，后续再补录。"), this);
    hint->setObjectName(QStringLiteral("pageHint"));
    hint->setWordWrap(true);
    root->addWidget(hint);

    root->addWidget(buildSearchBar());
    root->addWidget(buildTableArea(), 1);
}

QWidget *MoldTerminalPage::buildSearchBar()
{
    QWidget *bar = new QWidget(this);
    QHBoxLayout *lay = new QHBoxLayout(bar);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(8);

    m_edtKeyword = new QLineEdit(bar);
    m_edtKeyword->setPlaceholderText(QStringLiteral("搜索：模具号 / 端子型号 / 线径 / 使用线束"));
    m_edtKeyword->setClearButtonEnabled(true);
    lay->addWidget(m_edtKeyword, 1);

    m_cmbStatus = new QComboBox(bar);
    m_cmbStatus->addItem(QStringLiteral("全部状态"), QStringLiteral("all"));
    m_cmbStatus->addItem(QStringLiteral("待完善"), QStringLiteral("incomplete"));
    m_cmbStatus->addItem(QStringLiteral("完整"), QStringLiteral("complete"));
    lay->addWidget(m_cmbStatus);

    m_btnSearch = new QPushButton(QStringLiteral("查询"), bar);
    m_btnSearch->setObjectName(QStringLiteral("btnSearch"));
    lay->addWidget(m_btnSearch);

    m_btnAdd = new QPushButton(QStringLiteral("新增"), bar);
    m_btnAdd->setObjectName(QStringLiteral("btnEntry"));
    lay->addWidget(m_btnAdd);
    m_btnEdit = new QPushButton(QStringLiteral("修改"), bar);
    m_btnEdit->setObjectName(QStringLiteral("btnEntry"));
    lay->addWidget(m_btnEdit);
    m_btnDelete = new QPushButton(QStringLiteral("删除"), bar);
    m_btnDelete->setObjectName(QStringLiteral("btnDanger"));
    lay->addWidget(m_btnDelete);

    connect(m_btnSearch, &QPushButton::clicked, this, &MoldTerminalPage::onSearch);
    connect(m_edtKeyword, &QLineEdit::returnPressed, this, &MoldTerminalPage::onSearch);
    connect(m_btnAdd, &QPushButton::clicked, this, &MoldTerminalPage::onAdd);
    connect(m_btnEdit, &QPushButton::clicked, this, &MoldTerminalPage::onEdit);
    connect(m_btnDelete, &QPushButton::clicked, this, &MoldTerminalPage::onDelete);

    return bar;
}

QWidget *MoldTerminalPage::buildTableArea()
{
    QWidget *area = new QWidget(this);
    QVBoxLayout *lay = new QVBoxLayout(area);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(6);

    m_table = new QTableWidget(area);
    m_table->setObjectName(QStringLiteral("productTable"));
    m_table->setColumnCount(9);
    QStringList headers;
    headers << QStringLiteral("ID") << QStringLiteral("模具号") << QStringLiteral("类型")
            << QStringLiteral("适用端子") << QStringLiteral("压接线径") << QStringLiteral("机器编号")
            << QStringLiteral("密封堵/机台") << QStringLiteral("使用线束") << QStringLiteral("状态");
    m_table->setHorizontalHeaderLabels(headers);
    m_table->horizontalHeader()->setStretchLastSection(true);
    m_table->verticalHeader()->setVisible(false);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    lay->addWidget(m_table, 1);

    m_lblInfo = new QLabel(QStringLiteral("共 0 条"), area);
    lay->addWidget(m_lblInfo);

    connect(m_table, &QTableWidget::cellDoubleClicked, this, &MoldTerminalPage::onTableDoubleClicked);
    return area;
}

void MoldTerminalPage::onPageShown()
{
    refreshTable();
}

void MoldTerminalPage::setLoggedIn(const QString &user)
{
    m_currentUser = user;
    const bool editable = !user.isEmpty();
    m_btnAdd->setEnabled(editable);
    m_btnEdit->setEnabled(editable);
    m_btnDelete->setEnabled(editable);
}

void MoldTerminalPage::onSearch()
{
    refreshTable();
}

int MoldTerminalPage::currentRowId() const
{
    const int row = m_table->currentRow();
    if (row < 0 || row >= m_table->rowCount())
        return -1;
    QTableWidgetItem *it = m_table->item(row, 0); // ID 列
    return it ? it->text().toInt() : -1;
}

void MoldTerminalPage::onAdd()
{
    MoldItem m;
    if (openEditor(&m))
        refreshTable();
}

void MoldTerminalPage::onEdit()
{
    const int id = currentRowId();
    if (id <= 0) {
        NdMessageBox::information(this, QStringLiteral("修改模具"), QStringLiteral("请先选中一条记录。"));
        return;
    }
    QString err;
    const QList<MoldItem> list = DatabaseManager::instance().moldSearch(
        QString::number(id), QStringLiteral("all"), 1, nullptr, &err);
    if (list.isEmpty()) {
        NdMessageBox::information(this, QStringLiteral("修改模具"),
            err.isEmpty() ? QStringLiteral("未找到该记录。") : err);
        return;
    }
    MoldItem m = list.first();
    if (openEditor(&m))
        refreshTable();
}

void MoldTerminalPage::onDelete()
{
    const int id = currentRowId();
    if (id <= 0) {
        NdMessageBox::information(this, QStringLiteral("删除模具"), QStringLiteral("请先选中一条记录。"));
        return;
    }
    if (!NdMessageBox::question(this, QStringLiteral("删除模具"),
            QStringLiteral("确定删除这条模具记录吗？此操作不可恢复。")))
        return;
    QString err;
    if (!DatabaseManager::instance().moldDelete(id, &err)) {
        NdMessageBox::information(this, QStringLiteral("删除失败"),
            err.isEmpty() ? QStringLiteral("删除失败。") : err);
        return;
    }
    refreshTable();
}

void MoldTerminalPage::onTableDoubleClicked(int row, int column)
{
    Q_UNUSED(row);
    Q_UNUSED(column);
    if (!m_currentUser.isEmpty())
        onEdit();
}

void MoldTerminalPage::refreshTable()
{
    QString err;
    const QString status = m_cmbStatus ? m_cmbStatus->currentData().toString() : QStringLiteral("all");
    const QList<MoldItem> list = DatabaseManager::instance().moldSearch(
        m_edtKeyword ? m_edtKeyword->text() : QString(), status, 2000, nullptr, &err);
    QList<QStringList> rows;
    for (const MoldItem &m : list)
        rows.append(m.toRow());
    fillTable(rows);
    if (!err.isEmpty())
        m_lblInfo->setText(QStringLiteral("加载失败：%1").arg(err));
}

bool MoldTerminalPage::openEditor(MoldItem *item)
{
    QDialog dlg(this);
    dlg.setWindowTitle(item->id > 0 ? QStringLiteral("修改模具") : QStringLiteral("新增模具"));
    QFormLayout *form = new QFormLayout(&dlg);

    QLineEdit *edCode = new QLineEdit(item->moldCode, &dlg);
    edCode->setPlaceholderText(QStringLiteral("新模具才有编号；老模具/刀片可留空"));
    form->addRow(QStringLiteral("模具号"), edCode);

    QComboBox *cmbKind = new QComboBox(&dlg);
    cmbKind->addItems({ QStringLiteral("模具"), QStringLiteral("刀片"),
                        QStringLiteral("刀头"), QStringLiteral("其他") });
    cmbKind->setCurrentText(item->type.isEmpty() ? QStringLiteral("模具") : item->type);
    form->addRow(QStringLiteral("类型"), cmbKind);

    QComboBox *cmbType = new QComboBox(&dlg);
    cmbType->setEditable(true);
    cmbType->addItem(QStringLiteral(""), QStringLiteral(""));
    cmbType->addItem(QStringLiteral("日式"), QStringLiteral("日式"));
    cmbType->addItem(QStringLiteral("欧式"), QStringLiteral("欧式"));
    cmbType->setCurrentText(item->machineType);
    form->addRow(QStringLiteral("机器类型"), cmbType);

    QLineEdit *edPress = new QLineEdit(item->pressWire, &dlg);
    form->addRow(QStringLiteral("可压接线径"), edPress);

    QTextEdit *edTerms = new QTextEdit(item->terminals, &dlg);
    edTerms->setPlaceholderText(QStringLiteral("适用端子型号，多个用「、」分隔"));
    edTerms->setMaximumHeight(70);
    form->addRow(QStringLiteral("适用端子"), edTerms);

    QTextEdit *edTermInfo = new QTextEdit(item->terminalInfo, &dlg);
    edTermInfo->setPlaceholderText(QStringLiteral("端子信息（片形/公母端/线径范围等）"));
    edTermInfo->setMaximumHeight(70);
    form->addRow(QStringLiteral("端子信息"), edTermInfo);

    QLineEdit *edMachineNo = new QLineEdit(item->machineNo, &dlg);
    edMachineNo->setPlaceholderText(QStringLiteral("预留字段，如 03号机 / M3"));
    form->addRow(QStringLiteral("机器编号"), edMachineNo);

    QLineEdit *edSeal = new QLineEdit(item->sealInfo, &dlg);
    edSeal->setPlaceholderText(QStringLiteral("密封堵 / 在几号机使用"));
    form->addRow(QStringLiteral("密封堵/机台"), edSeal);

    QLineEdit *edHarness = new QLineEdit(item->harness, &dlg);
    form->addRow(QStringLiteral("使用线束"), edHarness);

    QComboBox *cmbOrient = new QComboBox(&dlg);
    cmbOrient->addItem(QStringLiteral(""), QStringLiteral(""));
    cmbOrient->addItem(QStringLiteral("A竖排"), QStringLiteral("A"));
    cmbOrient->addItem(QStringLiteral("B横排"), QStringLiteral("B"));
    cmbOrient->setCurrentIndex(cmbOrient->findData(item->orientation));
    form->addRow(QStringLiteral("排列"), cmbOrient);

    QLineEdit *edDept = new QLineEdit(item->department, &dlg);
    form->addRow(QStringLiteral("责任部门"), edDept);
    QLineEdit *edArea = new QLineEdit(item->area, &dlg);
    form->addRow(QStringLiteral("放置区域"), edArea);
    QLineEdit *edKeeper = new QLineEdit(item->keeper, &dlg);
    form->addRow(QStringLiteral("保管人"), edKeeper);
    QLineEdit *edVendor = new QLineEdit(item->vendor, &dlg);
    form->addRow(QStringLiteral("厂家/购买日期"), edVendor);

    QComboBox *cmbStatus = new QComboBox(&dlg);
    cmbStatus->addItem(QStringLiteral("待完善"), 0);
    cmbStatus->addItem(QStringLiteral("完整"), 1);
    cmbStatus->addItem(QStringLiteral("已核对"), 2);
    cmbStatus->setCurrentIndex(cmbStatus->findData(item->status));
    form->addRow(QStringLiteral("状态"), cmbStatus);

    QLineEdit *edRemark = new QLineEdit(item->remark, &dlg);
    form->addRow(QStringLiteral("备注"), edRemark);

    QPushButton *btnOk = new QPushButton(QStringLiteral("保存"), &dlg);
    QPushButton *btnCancel = new QPushButton(QStringLiteral("取消"), &dlg);
    btnOk->setObjectName(QStringLiteral("btnEntry"));
    btnCancel->setObjectName(QStringLiteral("btnGhost"));
    QHBoxLayout *btns = new QHBoxLayout();
    btns->addStretch();
    btns->addWidget(btnCancel);
    btns->addWidget(btnOk);
    form->addRow(btns);

    connect(btnCancel, &QPushButton::clicked, &dlg, &QDialog::reject);
    connect(btnOk, &QPushButton::clicked, &dlg, &QDialog::accept);
    if (dlg.exec() != QDialog::Accepted)
        return false;

    item->moldCode = edCode->text().trimmed();
    item->type = cmbKind->currentText().trimmed();
    item->machineType = cmbType->currentText().trimmed();
    item->pressWire = edPress->text().trimmed();
    item->terminals = edTerms->toPlainText().trimmed();
    item->terminalInfo = edTermInfo->toPlainText().trimmed();
    item->machineNo = edMachineNo->text().trimmed();
    item->sealInfo = edSeal->text().trimmed();
    item->harness = edHarness->text().trimmed();
    item->orientation = cmbOrient->currentData().toString();
    item->department = edDept->text().trimmed();
    item->area = edArea->text().trimmed();
    item->keeper = edKeeper->text().trimmed();
    item->vendor = edVendor->text().trimmed();
    item->status = cmbStatus->currentData().toInt();
    item->remark = edRemark->text().trimmed();

    // 模具号留空也允许保存（老模具），自动保持「待完善」提示
    QString err;
    if (!DatabaseManager::instance().moldSave(item, &err)) {
        NdMessageBox::information(this, QStringLiteral("保存失败"),
            err.isEmpty() ? QStringLiteral("保存失败。") : err);
        return false;
    }
    return true;
}

void MoldTerminalPage::fillTable(const QList<QStringList> &rows)
{
    m_table->setRowCount(rows.size());
    for (int r = 0; r < rows.size(); ++r) {
        const QStringList &cols = rows.at(r);
        for (int c = 0; c < cols.size() && c < m_table->columnCount(); ++c) {
            QTableWidgetItem *it = new QTableWidgetItem(cols.at(c));
            m_table->setItem(r, c, it);
        }
    }
    m_table->resizeColumnsToContents();
    // 内容较多的列给最小宽度，避免文字被截断
    m_table->setColumnWidth(1, qMax(m_table->columnWidth(1), 150)); // 模具号
    m_table->setColumnWidth(3, qMax(m_table->columnWidth(3), 200)); // 适用端子
    m_table->setColumnWidth(7, qMax(m_table->columnWidth(7), 160)); // 使用线束
    m_lblInfo->setText(QStringLiteral("共 %1 条").arg(rows.size()));
}
