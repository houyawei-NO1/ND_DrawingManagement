#include "singleinstance.h"

#include <QLocalServer>
#include <QLocalSocket>
#include <QString>
#include <QDebug>

namespace {
// 按 Windows 登录用户区分，避免快速用户切换时两个账户互相顶掉；
// 管道名只用 ASCII 常量 + 用户名，不含反斜杠等特殊字符
QString localServerName()
{
    return QStringLiteral("ND_DrawingManagement_SingleInstance_")
           + QString::fromLocal8Bit(qgetenv("USERNAME"));
}
} // namespace

SingleInstanceGuard &SingleInstanceGuard::instance()
{
    // 懒加载：必须在 QApplication 构造之后才首次调用
    static SingleInstanceGuard *guard = new SingleInstanceGuard();
    return *guard;
}

SingleInstanceGuard::SingleInstanceGuard() : QObject(nullptr)
{
}

bool SingleInstanceGuard::acquire()
{
    const QString name = localServerName();

    // 已有实例在监听：发唤醒消息后直接让调用方退出，不初始化数据库、不建托盘、不启定时器
    QLocalSocket probe;
    probe.connectToServer(name);
    if (probe.waitForConnected(300)) {
        probe.write("show");
        probe.flush();
        probe.waitForBytesWritten(300);
        probe.disconnectFromServer();
        return false;
    }

    // 走到这里说明没有活实例；上次异常退出可能残留同名管道，可安全清理
    QLocalServer::removeServer(name);

    if (!m_server) {
        m_server = new QLocalServer(this);
        connect(m_server, &QLocalServer::newConnection, this, [this]() {
            while (QLocalSocket *c = m_server->nextPendingConnection()) {
                c->waitForReadyRead(100);
                c->readAll();
                c->disconnectFromServer();
                c->deleteLater();
            }
            emit activated();
        });
    }
    // 极端情况（管道名冲突、权限）监听失败也不阻止程序启动，只是本次没有唤醒能力
    if (!m_server->isListening() && !m_server->listen(name))
        qWarning("SingleInstanceGuard: listen failed: %s",
                 qPrintable(m_server->errorString()));
    return true;
}

void SingleInstanceGuard::release()
{
    // 只关闭监听、保留对象，重启失败时 acquire() 可以直接复用
    if (m_server)
        m_server->close();
}
