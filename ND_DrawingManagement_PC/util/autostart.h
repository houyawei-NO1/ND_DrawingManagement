#ifndef AUTOSTART_H
#define AUTOSTART_H

#include <QCoreApplication>
#include <QDir>
#include <QSettings>
#include <QString>
#include <QTime>

// 开机启动与每日数量录入通知的公共配置键。
// Windows 开机启动写 HKCU\...\Run，不需要管理员权限，也不会影响其它用户。
namespace AutoStart {

inline QString autoRunKey()        { return QStringLiteral("startup/autoRun"); }
inline QString notifyEnabledKey()  { return QStringLiteral("notify/dailyQtyEnabled"); }
inline QString notifyTimeKey()     { return QStringLiteral("notify/dailyQtyTime"); }
// 已提醒标记：记录「日期 + 设定时刻」（yyyy-MM-dd HH:mm），而不是只有日期。
// 这样修改提醒时间后，当天仍能按新时刻再提醒一次，不会出现「改了时间当天不生效」。
inline QString notifyStampKey() { return QStringLiteral("notify/dailyQtyLastStamp"); }
inline QString defaultNotifyTime() { return QStringLiteral("16:00"); }

// 每日更新通知：到点检查服务器是否有新版本，有则右下角提示是否更新
inline QString updateEnabledKey() { return QStringLiteral("notify/updateEnabled"); }
inline QString updateTimeKey()    { return QStringLiteral("notify/updateTime"); }
inline QString updateStampKey()   { return QStringLiteral("notify/updateLastStamp"); }
inline QString defaultUpdateTime() { return QStringLiteral("09:00"); }

inline QTime configuredUpdateTime()
{
    const QTime t = QTime::fromString(
        QSettings().value(updateTimeKey(), defaultUpdateTime()).toString(),
        QStringLiteral("HH:mm"));
    return t.isValid() ? t : QTime(9, 0);
}

inline QTime configuredNotifyTime()
{
    const QTime t = QTime::fromString(
        QSettings().value(notifyTimeKey(), defaultNotifyTime()).toString(),
        QStringLiteral("HH:mm"));
    return t.isValid() ? t : QTime(16, 0);
}

inline bool isEnabled()
{
#ifdef Q_OS_WIN
    QSettings run(QStringLiteral(
        "HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Run"),
        QSettings::NativeFormat);
    return run.contains(QStringLiteral("ND_DrawingManagement"))
        && !run.value(QStringLiteral("ND_DrawingManagement")).toString().isEmpty();
#else
    return false;
#endif
}

inline bool setEnabled(bool on)
{
#ifdef Q_OS_WIN
    QSettings run(QStringLiteral(
        "HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Run"),
        QSettings::NativeFormat);
    if (on) {
        // 路径可能含空格/中文，必须用引号包住；开机自启时工作目录不是 exe 目录，
        // 程序内部一律用 applicationDirPath 或配置中的绝对路径取数据。
        const QString exe = QDir::toNativeSeparators(QCoreApplication::applicationFilePath());
        run.setValue(QStringLiteral("ND_DrawingManagement"),
                     QStringLiteral("\"%1\"").arg(exe));
    } else {
        run.remove(QStringLiteral("ND_DrawingManagement"));
    }
    run.sync();
    return run.status() == QSettings::NoError;
#else
    Q_UNUSED(on);
    return false;
#endif
}

// 服务器主机必须开机自启，否则客户端连不上数据服务；客户端按用户勾选决定。
// 程序启动时调用一次，保证 QSettings、关于页勾选框、注册表三者一致。
inline void enforceServerIfNeeded(bool isServer)
{
    QSettings s;
    if (isServer)
        s.setValue(autoRunKey(), true);
    setEnabled(s.value(autoRunKey(), false).toBool());
}

} // namespace AutoStart

#endif // AUTOSTART_H
