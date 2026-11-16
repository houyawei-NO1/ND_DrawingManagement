#ifndef DATAENTRYNOTIFY_H
#define DATAENTRYNOTIFY_H

#include <QWidget>
#include <QString>

class QKeyEvent;
class QPushButton;

// 屏幕右下角通知卡片：深色圆角 + 红取消/绿确认（与程序统一弹窗同款配色），
// 不使用 OverlayMask 的全屏黑色遮罩。数据录入提醒、自动备份完成提示共用。
class DataEntryNotify : public QWidget
{
    Q_OBJECT
public:
    struct Options {
        QString title;
        QString text;
        QString tip;
        bool showCancel = true; // 是否显示红色「取 消」按钮
        int autoCloseMs = 0;    // >0：无人操作时自动关闭（毫秒）
    };

    explicit DataEntryNotify(const Options &opt, QWidget *parent = nullptr);
    ~DataEntryNotify() override;

    // 独立顶层通知：主窗口隐藏到后台时仍能弹出，弹窗自行管理生命周期
    static void showMessage(const Options &opt);

    void showAtBottomRight();

signals:
    void accepted();
    void rejected();

protected:
    void keyPressEvent(QKeyEvent *event) override;

private:
    void finish(bool ok);

    QPushButton *m_ok = nullptr;
    bool m_finished = false;
};

#endif // DATAENTRYNOTIFY_H
