#ifndef OVERLAYMASK_H
#define OVERLAYMASK_H

#include <QWidget>
#include <QApplication>
#include <QScreen>
#include <QPainter>
#include <QColor>
#include <QPaintEvent>

// 程序内所有半透明遮罩的统一实现：与「选择本机角色」一致，
// 作为顶层无边框窗口铺满整块屏幕，背景统一 70% 黑，卡片由子类自己居中放置。
// 内联嵌入（embedded）的面板不调用 enableOverlay()，行为与普通 QWidget 完全相同。
class OverlayMask : public QWidget
{
public:
    explicit OverlayMask(QWidget *parent = nullptr) : QWidget(parent) {}

    void enableOverlay()
    {
        if (m_overlayEnabled) return;
        m_overlayEnabled = true;
        setWindowFlags(Qt::FramelessWindowHint | Qt::Window | Qt::WindowStaysOnTopHint);
        setAttribute(Qt::WA_TranslucentBackground, true);
        setAttribute(Qt::WA_StyledBackground, true);
    }
    bool overlayEnabled() const { return m_overlayEnabled; }

    // 屏幕分辨率变化 / 主窗口缩放时重新贴合屏幕
    void syncOverlay()
    {
        if (QScreen *s = QApplication::primaryScreen())
            setGeometry(s->geometry());
    }

    // 铺满主屏幕并显示到最上层
    void showOverlay()
    {
        enableOverlay();
        syncOverlay();
        show();
        raise();
        activateWindow();
    }

protected:
    void paintEvent(QPaintEvent *event) override
    {
        if (!m_overlayEnabled) {
            QWidget::paintEvent(event);
            return;
        }
        QPainter p(this);
        p.fillRect(rect(), QColor(0, 0, 0, 179)); // 与角色选择遮罩一致，≈ 0.70
    }

private:
    bool m_overlayEnabled = false;
};

#endif // OVERLAYMASK_H
