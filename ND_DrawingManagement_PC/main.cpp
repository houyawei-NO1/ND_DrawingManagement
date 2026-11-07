#include "ui/mainwindow.h"
#include "ui/roledialog.h"
#include "db/databasemanager.h"
#include <QApplication>
#include <QCoreApplication>
#include <QMessageBox>
#include <QSettings>

int main(int argc, char *argv[])
{
    // Windows 下显示缩放按 100% 生效
    qputenv("QT_FONT_DPI", "96");
    qputenv("QT_SCALE_FACTOR", "1.0");

    QApplication a(argc, argv);
    // 工作路径、本机角色、自动备份开关等配置统一用 QSettings 保存，需在数据库初始化前设置好作用域
    QCoreApplication::setOrganizationName(QStringLiteral("HouYawei"));
    QCoreApplication::setApplicationName(QStringLiteral("ND_DrawingManagement"));

    // 首次启动选择本机角色：整个系统只有一台服务器主机保存图纸与数据库，其余电脑都是客户端。
    // 使用与「关于 / 登录」一致的全屏半透明遮罩弹窗，而不是系统默认对话框。
    QSettings settings;
    if (!settings.contains(QStringLiteral("system/role"))) {
        RoleChoice cur;
        cur.host = DatabaseManager::serverHost();
        cur.port = DatabaseManager::serverPort();
        const RoleChoice r = RoleSelectDialog::choose(cur);
        if (!r.ok)
            return -1;
        if (r.role == QLatin1String("client")) {
            DatabaseManager::setServerHost(r.host);
            DatabaseManager::setServerPort(r.port);
        }
        DatabaseManager::setSystemRole(r.role);
    }

    if (!DatabaseManager::instance().init()) {
        QMessageBox::critical(nullptr, QStringLiteral("错误"),
                              QStringLiteral("数据库初始化失败，程序退出"));
        return -1;
    }

    MainWindow w;
    w.show();
    return a.exec();
}
