#ifndef SAVINGOVERLAY_H
#define SAVINGOVERLAY_H

#include "overlaymask.h"

#include <QApplication>
#include <QCoreApplication>
#include <QEventLoop>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QProgressBar>
#include <QPushButton>
#include <QVBoxLayout>

// 「正在保存至服务器」→「操作完成」两段式全屏遮罩，风格与 NdMessageBox 一致。
// 阶段一不阻塞、无按钮，由调用方接着执行同步写库；阶段二在同一张卡片上变成
// 「操作完成」并带「确 定」，局部阻塞等待用户确认。
// 期间遮罩铺满屏幕并吸收鼠标输入，配合调用方的 busy 标志杜绝重复提交。
class SavingOverlay : public OverlayMask
{
public:
    explicit SavingOverlay(QWidget *parent = nullptr)
        : OverlayMask(parent ? parent->window() : nullptr)
    {
        enableOverlay();
        setObjectName(QStringLiteral("ndSavingMask"));
        setFocusPolicy(Qt::StrongFocus);
        setStyleSheet(QStringLiteral(
            "QWidget#ndSavingCard{background:#22293C;border:1px solid rgba(255,255,255,0.12);"
            "border-radius:12px;}"
            "QWidget#ndSavingCard QLabel{background:transparent;}"
            "QPushButton#ndSavingOk{background:#43CF7C;color:#0B1F14;border:none;"
            "border-radius:6px;padding:6px 26px;font-weight:bold;}"
            "QPushButton#ndSavingOk:hover{background:#5CE0A0;}"));

        QGridLayout *outer = new QGridLayout(this);
        outer->setContentsMargins(0, 0, 0, 0);

        m_card = new QWidget(this);
        m_card->setObjectName(QStringLiteral("ndSavingCard"));
        m_card->setFixedWidth(420);
        QVBoxLayout *cl = new QVBoxLayout(m_card);
        cl->setContentsMargins(26, 20, 26, 18);
        cl->setSpacing(12);

        m_title = new QLabel(QStringLiteral("正在保存至服务器"), m_card);
        m_title->setStyleSheet(QStringLiteral(
            "font-size:17px;font-weight:bold;color:#FFFFFF;"));
        m_text = new QLabel(QStringLiteral("请勿操作"), m_card);
        m_text->setWordWrap(true);
        m_text->setStyleSheet(QStringLiteral(
            "color:rgba(255,255,255,0.85);font-size:13px;"));

        m_progress = new QProgressBar(m_card);
        m_progress->setRange(0, 0); // 不确定进度（逐条写库，条数不代表耗时）
        m_progress->setTextVisible(false);
        m_progress->setFixedHeight(6);

        m_ok = new QPushButton(QStringLiteral("确 定"), m_card);
        m_ok->setObjectName(QStringLiteral("ndSavingOk"));
        m_ok->setCursor(Qt::PointingHandCursor);
        m_ok->hide();
        connect(m_ok, &QPushButton::clicked, this, [this] { finish(); });

        QHBoxLayout *btns = new QHBoxLayout();
        btns->addStretch();
        btns->addWidget(m_ok);

        cl->addWidget(m_title);
        cl->addWidget(m_text, 1);
        cl->addWidget(m_progress);
        cl->addLayout(btns);
        outer->addWidget(m_card, 0, 0, Qt::AlignCenter);
    }

    // 显示后立刻重绘一次（排除用户输入，避免保存还没开始就重入点击）
    void showBusy()
    {
        showOverlay();
        setFocus();
        QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
    }

    // 同一张卡片切换为「操作完成」，等待用户点确定后返回
    void showDone(const QString &result)
    {
        m_title->setText(QStringLiteral("操作完成"));
        m_text->setText(result);
        m_progress->hide();
        m_ok->show();
        showOverlay();
        m_ok->setFocus();
        QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
        QEventLoop loop;
        m_loop = &loop;
        loop.exec();
        m_loop = nullptr;
    }

protected:
    // 阶段一不接受任何关闭；阶段二回车等同点「确 定」
    void keyPressEvent(QKeyEvent *event) override
    {
        if (m_loop && (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter)) {
            finish();
            return;
        }
        OverlayMask::keyPressEvent(event);
    }

    // 吃掉落在遮罩上的点击，防止穿透到下面的「应用到数据库」按钮；
    // 阶段二沿用 NdMessageBox 的习惯，点卡片外也可以关闭
    void mousePressEvent(QMouseEvent *event) override
    {
        if (m_loop && m_card && !m_card->geometry().contains(event->pos())) {
            finish();
            return;
        }
        OverlayMask::mousePressEvent(event);
    }

private:
    void finish()
    {
        hide();
        if (m_loop) m_loop->quit();
    }

    QWidget *m_card = nullptr;
    QLabel *m_title = nullptr;
    QLabel *m_text = nullptr;
    QProgressBar *m_progress = nullptr;
    QPushButton *m_ok = nullptr;
    QEventLoop *m_loop = nullptr;
};

#endif // SAVINGOVERLAY_H
