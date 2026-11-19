#include "moldquerypage.h"
#include "db/databasemanager.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QSplitter>
#include <QLineEdit>
#include <QPushButton>
#include <QComboBox>
#include <QTableWidget>
#include <QHeaderView>
#include <QLabel>
#include <QTextBrowser>

MoldQueryPage::MoldQueryPage(QWidget *parent) : QWidget(parent)
{
    QVBoxLayout *root = new QVBoxLayout(this);
    root->setContentsMargins(16, 12, 16, 12);
    root->setSpacing(10);

    root->addWidget(buildSearchBar());

    // 左右分栏：左表格（模具列表），右详情
    QSplitter *split = new QSplitter(Qt::Horizontal, this);
    split->setChildrenCollapsible(false);

    QWidget *left = new QWidget(split);
    QVBoxLayout *ll = new QVBoxLayout(left);
    ll->setContentsMargins(0, 0, 0, 0);
    ll->setSpacing(6);
    m_table = new QTableWidget(left);
    m_table->setObjectName(QStringLiteral("productTable"));
    m_table->setColumnCount(9);
    m_table->setHorizontalHeaderLabels({ QStringLiteral("模具号"), QStringLiteral("类型"),
        QStringLiteral("机器类型"), QStringLiteral("机器编号"), QStringLiteral("适用端子"),
        QStringLiteral("压接线径"), QStringLiteral("密封堵/机台"), QStringLiteral("使用线束"),
        QStringLiteral("状态") });
    m_table->horizontalHeader()->setStretchLastSection(true);
    m_table->verticalHeader()->setVisible(false);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    ll->addWidget(m_table, 1);
    m_lblInfo = new QLabel(QStringLiteral("共 0 条"), left);
    ll->addWidget(m_lblInfo);

    QWidget *right = new QWidget(split);
    QVBoxLayout *rl = new QVBoxLayout(right);
    rl->setContentsMargins(0, 0, 0, 0);
    m_detail = new QTextBrowser(right);
    m_detail->setObjectName(QStringLiteral("detailBlk"));
    m_detail->setOpenExternalLinks(true);
    rl->addWidget(m_detail, 1);

    split->addWidget(left);
    split->addWidget(right);
    split->setStretchFactor(0, 3);
    split->setStretchFactor(1, 2);
    root->addWidget(split, 1);

    connect(m_table, &QTableWidget::cellClicked, this, &MoldQueryPage::onTableClicked);
}

QWidget *MoldQueryPage::buildSearchBar()
{
    QWidget *bar = new QWidget(this);
    QHBoxLayout *lay = new QHBoxLayout(bar);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(8);

    m_edtKeyword = new QLineEdit(bar);
    m_edtKeyword->setPlaceholderText(QStringLiteral("搜索：模具号 / 端子型号 / 线径 / 使用线束 / 保管人"));
    m_edtKeyword->setClearButtonEnabled(true);
    lay->addWidget(m_edtKeyword, 1);

    m_cmbMachineType = new QComboBox(bar);
    m_cmbMachineType->addItem(QStringLiteral("全部类型"), QStringLiteral("all"));
    m_cmbMachineType->addItem(QStringLiteral("日式"), QStringLiteral("日式"));
    m_cmbMachineType->addItem(QStringLiteral("欧式"), QStringLiteral("欧式"));
    lay->addWidget(m_cmbMachineType);

    QPushButton *btnSearch = new QPushButton(QStringLiteral("搜索"), bar);
    btnSearch->setObjectName(QStringLiteral("btnSearch"));
    lay->addWidget(btnSearch);

    connect(btnSearch, &QPushButton::clicked, this, &MoldQueryPage::onSearch);
    connect(m_edtKeyword, &QLineEdit::returnPressed, this, &MoldQueryPage::onSearch);
    connect(m_cmbMachineType, &QComboBox::currentIndexChanged, this, &MoldQueryPage::onSearch);
    return bar;
}

void MoldQueryPage::onPageShown()
{
    refreshTable();
}

void MoldQueryPage::onSearch()
{
    refreshTable();
}

void MoldQueryPage::refreshTable()
{
    const QString kw = m_edtKeyword->text().trimmed();
    const QString type = m_cmbMachineType->currentData().toString();
    const QList<MoldItem> all = DatabaseManager::instance().moldSearch(kw, QStringLiteral("all"), 5000);
    m_items.clear();
    for (const MoldItem &m : all) {
        if (type != QLatin1String("all") && m.machineType != type)
            continue;
        m_items.append(m);
    }

    m_table->setRowCount(m_items.size());
    for (int r = 0; r < m_items.size(); ++r) {
        const MoldItem &m = m_items.at(r);
        const QStringList row = { m.moldCode.isEmpty() ? QStringLiteral("（无编号）") : m.moldCode,
            m.type, m.machineType, m.machineNo, m.terminals, m.pressWire, m.sealInfo,
            m.harness, m.statusText() };
        for (int c = 0; c < row.size(); ++c)
            m_table->setItem(r, c, new QTableWidgetItem(row.at(c)));
    }
    m_table->resizeColumnsToContents();
    m_table->setColumnWidth(0, qMax(m_table->columnWidth(0), 150)); // 模具号
    m_table->setColumnWidth(4, qMax(m_table->columnWidth(4), 200)); // 适用端子
    m_table->setColumnWidth(6, qMax(m_table->columnWidth(6), 160)); // 密封堵
    m_table->setColumnWidth(7, qMax(m_table->columnWidth(7), 160)); // 使用线束
    m_lblInfo->setText(QStringLiteral("共 %1 条").arg(m_items.size()));
    m_detail->clear();
}

void MoldQueryPage::onTableClicked(int row, int column)
{
    Q_UNUSED(column);
    if (row < 0 || row >= m_items.size())
        return;
    showDetail(m_items.at(row));
}

void MoldQueryPage::showDetail(const MoldItem &m)
{
    QString html;
    html += QStringLiteral("<div style='color:#43CF7C;font-size:16px;font-weight:bold;margin-bottom:8px;'>%1</div>")
            .arg(m.moldCode.isEmpty() ? QStringLiteral("（无编号模具）") : m.moldCode);
    auto line = [&html](const QString &label, const QString &value) {
        html += QStringLiteral("<div style='margin:3px 0;color:#9FB0C9;'>%1：<span style='color:#ffffff;'>%2</span></div>")
                .arg(label, value.isEmpty() ? QStringLiteral("—") : value.toHtmlEscaped());
    };
    line(QStringLiteral("类型"), m.type);
    line(QStringLiteral("机器类型"), m.machineType);
    line(QStringLiteral("适用端子"), m.terminals);
    line(QStringLiteral("可压接线径"), m.pressWire);
    line(QStringLiteral("端子信息"), m.terminalInfo);
    line(QStringLiteral("机器编号"), m.machineNo);
    line(QStringLiteral("密封堵/机台"), m.sealInfo);
    line(QStringLiteral("使用线束"), m.harness);
    line(QStringLiteral("排列"), m.orientation);
    line(QStringLiteral("责任部门"), m.department);
    line(QStringLiteral("放置区域"), m.area);
    line(QStringLiteral("保管人"), m.keeper);
    line(QStringLiteral("厂家/购买日期"), m.vendor);
    line(QStringLiteral("状态"), m.statusText());
    line(QStringLiteral("备注"), m.remark);
    m_detail->setHtml(html);
}
