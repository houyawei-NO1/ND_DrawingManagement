#ifndef SINGLEINSTANCE_H
#define SINGLEINSTANCE_H

#include <QObject>

class QLocalServer;

// 单实例守卫：第一个进程监听本地命名管道；后续进程连上它发一条唤醒消息后自行退出，
// 由已有进程把隐藏到后台的主窗口恢复显示。
// 这样点击桌面快捷方式不会重复启动数据库、网页服务、自动备份与每日提醒。
class SingleInstanceGuard : public QObject
{
    Q_OBJECT
public:
    static SingleInstanceGuard &instance();

    // true = 本进程是主实例并开始监听；false = 已通知已有实例，调用方应立即 return 0
    bool acquire();
    // 切换角色重启前释放监听，避免新进程把尚未退出的自己误判为「已有实例」
    void release();

signals:
    void activated(); // 收到后续实例的唤醒请求

private:
    SingleInstanceGuard();

    QLocalServer *m_server = nullptr;
};

#endif // SINGLEINSTANCE_H
