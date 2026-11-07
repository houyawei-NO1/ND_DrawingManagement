#pragma once

#include <QMenu>
#include <QPainterPath>
#include <QRegion>
#include <QResizeEvent>
#include <QShowEvent>

// 深色主题下的圆角右键菜单。
// QMenu 是矩形弹出窗口，样式表画的圆角之外那一圈仍会被窗口底色填成直角，
// 所以这里把窗口本身也裁成同样的圆角，四个角不会再出现直角边。
class RoundedMenu : public QMenu
{
public:
    explicit RoundedMenu(QWidget *parent = nullptr, int radius = 8)
        : QMenu(parent), m_radius(radius)
    {
        setAttribute(Qt::WA_TranslucentBackground, true);
        setWindowFlag(Qt::NoDropShadowWindowHint, true);
    }

protected:
    void showEvent(QShowEvent *event) override
    {
        QMenu::showEvent(event);
        updateMask();
    }

    void resizeEvent(QResizeEvent *event) override
    {
        QMenu::resizeEvent(event);
        updateMask();
    }

private:
    void updateMask()
    {
        if (width() <= 0 || height() <= 0)
            return;
        QPainterPath path;
        path.addRoundedRect(QRectF(rect()), m_radius, m_radius);
        setMask(QRegion(path.toFillPolygon().toPolygon()));
    }

    int m_radius;
};
