#include "qtymanage.h"
#include "db/databasemanager.h"
#include "ndmessagebox.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QLabel>
#include <QLineEdit>
#include <QTableWidget>
#include <QHeaderView>
#include <QPushButton>
#include <QInputDialog>
#include <QEvent>

QtyManageDialog::QtyManageDialog(QWidget *parent) : QWidget(parent)
{
    // 复用「关于窗口」黑色半透明遮罩样式，点遮罩空白处关闭
    setObjectName(QStringLiteral("aboutOverlay"));
    setAttribute(Qt::WA_StyledBackground, true);
    hide();
    installEventFilter(this);

    QGridLayout *lay = new QGridLayout(this);
    lay->setContentsMargins(0, 0, 0, 0);

    QWidget *card = new QWidget(this);
    card->setObjectName(QStringLiteral("aboutCard"));
    card->setFixedSize(760, 560);
    QVBoxLayout *cl = new QVBoxLayout(card);
    cl->setContentsMargins(22, 18, 22, 18);
    cl->setSpacing(12);

    m_title = new QLabel(this);
    m_title->setObjectName(QStringLiteral("aboutTitle"));
    cl->addWidget(m_title);

    // 安全值：当前数量（只读） + 最小/最大数量（可改）
    QHBoxLayout *safe = new QHBoxLayout();
    safe->setSpacing(10);
    safe->addWidget(new QLabel(QStringLiteral("当前数量："), card));
    m_curQtyLabel = new QLabel(QStringLiteral("0"), card);
    m_curQtyLabel->setObjectName(QStringLiteral("aboutInfo"));
    safe->addWidget(m_curQtyLabel);
    safe->addSpacing(16);
    safe->addWidget(new QLabel(QStringLiteral("最小数量："), card));
    m_minEdit = new QLineEdit(card);
    m_minEdit->setFixedWidth(90);
    safe->addWidget(m_minEdit);
    safe->addWidget(new QLabel(QStringLiteral("最大数量："), card));
    m_maxEdit = new QLineEdit(card);
    m_maxEdit->setFixedWidth(90);
    safe->addWidget(m_maxEdit);
    safe->addStretch();
    QPushButton *btnSafe = new QPushButton(QStringLiteral("保存安全值"), card);
    btnSafe->setObjectName(QStringLiteral("btnEntry"));
    btnSafe->setCursor(Qt::PointingHandCursor);
    safe->addWidget(btnSafe);
    cl->addLayout(safe);
    connect(btnSafe, &QPushButton::clicked, this, &QtyManageDialog::onSaveSafe);

    QLabel *tip = new QLabel(QStringLiteral("每日录入数量（按日期倒序，可修改/删除某一天的数量）："), card);
    tip->setObjectName(QStringLiteral("aboutInfo"));
    cl->addWidget(tip);

    m_logTable = new QTableWidget(0, 3, card);
    m_logTable->setObjectName(QStringLiteral("productTable"));
    m_logTable->setHorizontalHeaderLabels({QStringLiteral("录入日期"),
                                          QStringLiteral("数量"),
                                          QStringLiteral("操作")});
    m_logTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_logTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_logTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    m_logTable->verticalHeader()->setVisible(false);
    m_logTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_logTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    cl->addWidget(m_logTable, 1);

    QHBoxLayout *foot = new QHBoxLayout();
    foot->addStretch();
    QPushButton *btnClose = new QPushButton(QStringLiteral("关 闭"), card);
    btnClose->setObjectName(QStringLiteral("btnSearch"));
    btnClose->setCursor(Qt::PointingHandCursor);
    connect(btnClose, &QPushButton::clicked, this, &QtyManageDialog::closePanel);
    foot->addWidget(btnClose);
    cl->addLayout(foot);

    lay->addWidget(card, 0, 0, Qt::AlignCenter);
}

void QtyManageDialog::loadProduct(int productId)
{
    m_pid = productId;
    const Product p = DatabaseManager::instance().productById(productId);
    m_title->setText(QStringLiteral("数量管理 · %1 / %2  %3")
                         .arg(p.category, p.model, p.name));
    m_curQtyLabel->setText(QString::number(p.qty));
    m_minEdit->setText(QString::number(p.minQty));
    m_maxEdit->setText(QString::number(p.maxQty));
    reloadLogs();
}

void QtyManageDialog::reloadLogs()
{
    QString err;
    const QList<QtyLog> logs = DatabaseManager::instance().qtyLogsOf(m_pid, &err);
    m_logTable->setRowCount(logs.size());
    if (!err.isEmpty())
        NdMessageBox::warning(this, QStringLiteral("读取失败"),
                              QStringLiteral("无法读取每日录入数量：%1").arg(err));
    for (int i = 0; i < logs.size(); ++i) {
        const QtyLog &l = logs.at(i);
        m_logTable->setItem(i, 0, new QTableWidgetItem(
            l.recordDate.isValid() ? l.recordDate.toString(QStringLiteral("yyyy-MM-dd"))
                                   : QStringLiteral("-")));
        m_logTable->setItem(i, 1, new QTableWidgetItem(QString::number(l.qty)));

        QWidget *ops = new QWidget(m_logTable);
        QHBoxLayout *ol = new QHBoxLayout(ops);
        ol->setContentsMargins(2, 0, 2, 0);
        ol->setSpacing(4);
        QPushButton *btnE = new QPushButton(QStringLiteral("修改"), ops);
        btnE->setObjectName(QStringLiteral("btnSearch"));
        btnE->setCursor(Qt::PointingHandCursor);
        QPushButton *btnD = new QPushButton(QStringLiteral("删除"), ops);
        btnD->setObjectName(QStringLiteral("btnDanger"));
        btnD->setCursor(Qt::PointingHandCursor);
        ol->addWidget(btnE);
        ol->addWidget(btnD);
        m_logTable->setCellWidget(i, 2, ops);
        m_logTable->setRowHeight(i, 30);

        connect(btnE, &QPushButton::clicked, this, [this, l] {
            bool ok = false;
            const int v = QInputDialog::getInt(this, QStringLiteral("修改每日数量"),
                                              QStringLiteral("数量（%1）：")
                                                  .arg(l.recordDate.toString(QStringLiteral("yyyy-MM-dd"))),
                                              l.qty, 0, 100000000, 1, &ok);
            if (!ok) return;
            QString err;
            if (!DatabaseManager::instance().updateQtyLog(l.id, v, &err))
                NdMessageBox::warning(this, QStringLiteral("失败"), err);
            reloadLogs();
        });
        connect(btnD, &QPushButton::clicked, this, [this, l] {
            if (!NdMessageBox::question(this, QStringLiteral("确认"),
                                       QStringLiteral("删除 %1 的这条数量记录？删除后该日用量统计将不再计入。")
                                           .arg(l.recordDate.toString(QStringLiteral("yyyy-MM-dd")))))
                return;
            QString err;
            if (!DatabaseManager::instance().deleteQtyLog(l.id, &err))
                NdMessageBox::warning(this, QStringLiteral("失败"), err);
            reloadLogs();
        });
    }
}

void QtyManageDialog::onSaveSafe()
{
    const int minV = m_minEdit->text().trimmed().toInt();
    const int maxV = m_maxEdit->text().trimmed().toInt();
    const Product p = DatabaseManager::instance().productById(m_pid);
    QString err;
    if (DatabaseManager::instance().updateQuantities(m_pid, p.qty, minV, maxV, &err))
        NdMessageBox::information(this, QStringLiteral("已保存"),
                                  QStringLiteral("最小/最大数量已更新"));
    else
        NdMessageBox::warning(this, QStringLiteral("保存失败"), err);
}

void QtyManageDialog::closePanel()
{
    hide();
    emit closed();
}

void QtyManageDialog::showPanel()
{
    if (parentWidget())
        resize(parentWidget()->size());
    raise();
    show();
}

bool QtyManageDialog::eventFilter(QObject *watched, QEvent *event)
{
    // 点到卡片之外的半透明遮罩空白处：关闭弹窗
    if (watched == this && event->type() == QEvent::MouseButtonRelease) {
        hide();
        emit closed();
        return true;
    }
    return QWidget::eventFilter(watched, event);
}
