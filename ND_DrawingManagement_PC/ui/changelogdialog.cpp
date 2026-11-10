#include "changelogdialog.h"
#include "db/databasemanager.h"
#include "ndmessagebox.h"
#include "model/product.h"

#include <QAbstractItemView>
#include <QEvent>
#include <QEventLoop>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QKeyEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>
#include <QWidget>

namespace {

// 统一风格的多行输入卡片，替代原生 QInputDialog::getMultiLineText，
// 避免在半透明遮罩卡片上再弹出一个系统风格对话框
class TextEditCard : public OverlayMask
{
public:
    TextEditCard(QWidget *parent, const QString &title, const QString &text)
        : OverlayMask(parent ? parent->window() : nullptr)
    {
        enableOverlay();
        setObjectName(QStringLiteral("ndInputMask"));
        setStyleSheet(QStringLiteral(
            "QWidget#ndInputCard{background:#22293C;border:1px solid rgba(255,255,255,0.12);"
            "border-radius:12px;}"
            "QWidget#ndInputCard QLabel{background:transparent;}"
            "QPushButton#ndInputOk{background:#43CF7C;color:#0B1F14;border:none;"
            "border-radius:6px;padding:6px 26px;font-weight:bold;}"
            "QPushButton#ndInputOk:hover{background:#5CE0A0;}"
            "QPushButton#ndInputCancel{background:rgba(224,108,117,0.85);color:#fff;"
            "border:none;border-radius:6px;padding:6px 22px;}"
            "QPushButton#ndInputCancel:hover{background:rgba(232,130,138,0.95);}"));

        QGridLayout *outer = new QGridLayout(this);
        outer->setContentsMargins(0, 0, 0, 0);
        card = new QWidget(this);
        card->setObjectName(QStringLiteral("ndInputCard"));
        card->setFixedWidth(480);
        QVBoxLayout *cl = new QVBoxLayout(card);
        cl->setContentsMargins(26, 20, 26, 18);
        cl->setSpacing(12);

        QLabel *titleLb = new QLabel(title, card);
        titleLb->setStyleSheet(QStringLiteral(
            "font-size:17px;font-weight:bold;color:#FFFFFF;"));
        m_edit = new QPlainTextEdit(text, card);
        m_edit->installEventFilter(this); // 输入框获得焦点时也要能按 Esc 取消
        m_edit->setFixedHeight(170);
        m_edit->setStyleSheet(QStringLiteral(
            "QPlainTextEdit{background:#1A2032;border:1px solid #4A5468;"
            "border-radius:6px;color:rgba(255,255,255,0.9);padding:6px;}"));

        QHBoxLayout *btns = new QHBoxLayout();
        btns->addStretch();
        QPushButton *cancel = new QPushButton(QStringLiteral("取 消"), card);
        cancel->setObjectName(QStringLiteral("ndInputCancel"));
        cancel->setCursor(Qt::PointingHandCursor);
        connect(cancel, &QPushButton::clicked, this, [this] { finish(false); });
        btns->addWidget(cancel);
        QPushButton *ok = new QPushButton(QStringLiteral("确 定"), card);
        ok->setObjectName(QStringLiteral("ndInputOk"));
        ok->setCursor(Qt::PointingHandCursor);
        connect(ok, &QPushButton::clicked, this, [this] { finish(true); });
        btns->addWidget(ok);

        cl->addWidget(titleLb);
        cl->addWidget(m_edit, 1);
        cl->addLayout(btns);
        outer->addWidget(card, 0, 0, Qt::AlignCenter);
        installEventFilter(this);
    }

    bool run(QString *out)
    {
        showOverlay();
        m_edit->setFocus();
        QEventLoop loop;
        m_loop = &loop;
        loop.exec();
        m_loop = nullptr;
        if (m_ok) *out = m_edit->toPlainText();
        return m_ok;
    }

protected:
    void keyPressEvent(QKeyEvent *event) override
    {
        if (event->key() == Qt::Key_Escape) {
            finish(false);
            return;
        }
        OverlayMask::keyPressEvent(event);
    }

    bool eventFilter(QObject *watched, QEvent *event) override
    {
        if (m_loop && watched == m_edit && event->type() == QEvent::KeyPress
            && static_cast<QKeyEvent *>(event)->key() == Qt::Key_Escape) {
            finish(false);
            return true;
        }
        if (m_loop && watched == this && event->type() == QEvent::MouseButtonRelease) {
            auto *me = static_cast<QMouseEvent *>(event);
            if (card && !card->geometry().contains(me->pos())) {
                finish(false);
                return true;
            }
        }
        return QWidget::eventFilter(watched, event);
    }

private:
    void finish(bool ok)
    {
        m_ok = ok;
        hide();
        if (m_loop) m_loop->quit();
    }

    QWidget *card = nullptr;
    QPlainTextEdit *m_edit = nullptr;
    QEventLoop *m_loop = nullptr;
    bool m_ok = false;
};

bool inputMultiLine(QWidget *parent, const QString &title, const QString &text, QString *out)
{
    TextEditCard box(parent, title, text);
    return box.run(out);
}

} // namespace

ChangeLogDialog::ChangeLogDialog(QWidget *parent)
    : OverlayMask(parent ? parent->window() : nullptr)
{
    enableOverlay();
    hide();
    setFocusPolicy(Qt::StrongFocus); // 让 Esc 关闭生效
    installEventFilter(this);

    QGridLayout *outer = new QGridLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);

    m_card = new QWidget(this);
    m_card->setObjectName(QStringLiteral("aboutCard"));
    m_card->setFixedSize(760, 560);
    QVBoxLayout *cl = new QVBoxLayout(m_card);
    cl->setContentsMargins(22, 18, 22, 18);
    cl->setSpacing(12);

    m_title = new QLabel(QStringLiteral("更改记录"), m_card);
    m_title->setObjectName(QStringLiteral("aboutTitle"));
    cl->addWidget(m_title);

    m_table = new QTableWidget(0, 4, m_card);
    m_table->setObjectName(QStringLiteral("productTable"));
    m_table->setHorizontalHeaderLabels({QStringLiteral("时间"), QStringLiteral("操作人"),
                                         QStringLiteral("内容"), QStringLiteral("操作")});
    m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    m_table->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Fixed);
    m_table->setColumnWidth(3, 140);
    m_table->verticalHeader()->setVisible(false);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    cl->addWidget(m_table, 1);

    QHBoxLayout *foot = new QHBoxLayout();
    foot->addStretch();
    QPushButton *btnClose = new QPushButton(QStringLiteral("关 闭"), m_card);
    btnClose->setObjectName(QStringLiteral("btnSearch"));
    btnClose->setCursor(Qt::PointingHandCursor);
    connect(btnClose, &QPushButton::clicked, this, [this] { closePanel(); });
    foot->addWidget(btnClose);
    cl->addLayout(foot);

    outer->addWidget(m_card, 0, 0, Qt::AlignCenter);
}

void ChangeLogDialog::loadProduct(int productId)
{
    m_pid = productId;
    const Product p = DatabaseManager::instance().productById(productId);
    m_title->setText(QStringLiteral("更改记录 · %1 / %2  %3")
                         .arg(p.category, p.model, p.name));
    reloadLogs();
}

void ChangeLogDialog::reloadLogs()
{
    const QList<ChangeLog> logs = DatabaseManager::instance().changeLogsOf(m_pid);
    m_table->setRowCount(logs.size());
    for (int i = 0; i < logs.size(); ++i) {
        const ChangeLog log = logs.at(i); // 按值拷贝，避免 lambda 引用悬空
        m_table->setItem(i, 0, new QTableWidgetItem(
            log.createdAt.toString(QStringLiteral("yyyy-MM-dd HH:mm"))));
        m_table->setItem(i, 1, new QTableWidgetItem(log.oper));
        m_table->setItem(i, 2, new QTableWidgetItem(log.content));

        QWidget *ops = new QWidget(m_table);
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
        m_table->setCellWidget(i, 3, ops);
        m_table->setRowHeight(i, 30);

        connect(btnE, &QPushButton::clicked, this, [this, log] {
            QString text;
            if (!inputMultiLine(this, QStringLiteral("修改更改记录"), log.content, &text))
                return;
            QString err;
            if (!DatabaseManager::instance().updateChangeLog(log.id, text, &err))
                NdMessageBox::warning(this, QStringLiteral("失败"), err);
            reloadLogs();
        });
        connect(btnD, &QPushButton::clicked, this, [this, log] {
            if (!NdMessageBox::question(this, QStringLiteral("确认"),
                                        QStringLiteral("删除这条更改记录？")))
                return;
            DatabaseManager::instance().deleteChangeLog(log.id);
            reloadLogs();
        });
    }
}

void ChangeLogDialog::showPanel()
{
    showOverlay(); // 全屏半透明黑遮罩
    setFocus();
}

void ChangeLogDialog::closePanel()
{
    hide();
}

bool ChangeLogDialog::eventFilter(QObject *watched, QEvent *event)
{
    // 点卡片之外的遮罩空白处关闭（卡片内的点击交给按钮/表格处理）
    if (watched == this && event->type() == QEvent::MouseButtonRelease) {
        auto *me = static_cast<QMouseEvent *>(event);
        if (m_card && !m_card->geometry().contains(me->pos())) {
            closePanel();
            return true;
        }
    }
    return QWidget::eventFilter(watched, event);
}

void ChangeLogDialog::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Escape) {
        closePanel();
        return;
    }
    OverlayMask::keyPressEvent(event);
}
