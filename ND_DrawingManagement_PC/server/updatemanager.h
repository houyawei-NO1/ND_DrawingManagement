#ifndef UPDATEMANAGER_H
#define UPDATEMANAGER_H

#include <QString>
#include <QStringList>
#include <QJsonObject>
#include <functional>

// 安装包类型：PC 安装包 / 手机 APK / 下线机 AutoIt exe 各自在服务器「安装包」目录里只保留最新一份
enum class PackageKind { Pc, Android, Device };

// 下线机 exe 有 32/64 位两套，分别保存、分别上传、分别检查更新
enum class PackageArch { Unknown, X86, X64 };

// 服务器「安装包」目录里 update.json / update_android.json / update_device_x86.json 等描述的那一份安装包
struct UpdateInfo
{
    bool available = false;      // 服务器上是否有可用安装包
    QString version;             // 版本号（构建日期 yyyy-MM-dd）；Device 为 yyyyMMdd-bXX-x86/x64
    QString buildStamp;          // 精确构建戳 yyyyMMddHHmmss，用来区分同一天多次编译
    QString arch;                // x86 / x64，PC 与 APK 为空
    QString fileName;
    qint64 size = 0;
    QString uploadedAt;
};

// 版本管理与安装包分发。
//   服务器本机：安装包保存在「工作路径/安装包」，PC 与手机各始终只保留最新一份 + 元数据
//   客户端：通过 /api/update/* 查询版本、按 1MB 分块上传、带进度流式下载
class UpdateManager
{
public:
    static UpdateManager &instance();

    // 本机版本号：优先读程序目录下 version.txt（安装包打包时写入），否则取 exe 最后修改时间
    QString localVersion() const;
    QString localStamp() const;
    bool hasUpdate(const UpdateInfo &info) const;
    static QString readableSize(qint64 bytes);

    // 位数标签：x86 / x64；Unknown 表示不区分（PC / APK）
    static QString archTag(PackageArch arch);
    static PackageArch archFromTag(const QString &tag);

    // 服务器工作路径下的「安装包」目录（客户端模式返回空）
    static QString packageDirectory();
    static QString metaFilePath(PackageKind kind = PackageKind::Pc,
                                PackageArch arch = PackageArch::Unknown);
    static QString partFilePath(PackageKind kind = PackageKind::Pc,
                                PackageArch arch = PackageArch::Unknown);
    static QString localPackagePath(PackageKind kind = PackageKind::Pc,
                                    PackageArch arch = PackageArch::Unknown);
    static QString packageGlob(PackageKind kind = PackageKind::Pc,
                               PackageArch arch = PackageArch::Unknown);
    // 同类型 + 同位数下要清理的旧包（含新中文名与旧 CutDevice_* 名）
    static QStringList packageGlobs(PackageKind kind = PackageKind::Pc,
                                    PackageArch arch = PackageArch::Unknown);
    // 下线机包名校验：本地选择、客户端上传、服务端接收共用这一条规则
    static bool isDevicePackageName(const QString &fileName, PackageArch arch);
    static UpdateInfo readLocalMeta(PackageKind kind = PackageKind::Pc,
                                    PackageArch arch = PackageArch::Unknown);
    static bool writeLocalMeta(const QString &fileName, const QString &version,
                               const QString &stamp, qint64 size, QString *err = nullptr,
                               PackageKind kind = PackageKind::Pc,
                               PackageArch arch = PackageArch::Unknown);
    // 服务器本机：把选中的安装包复制进来，清理同类型旧包并写元数据
    static bool installLocalPackage(const QString &srcPath, const QString &version,
                                    const QString &stamp, QString *err,
                                    const std::function<void(int)> &progress = {},
                                    PackageKind kind = PackageKind::Pc,
                                    PackageArch arch = PackageArch::Unknown);
    // 上传时用的版本号/构建戳：PC 用本机版本，Device 从 yyyyMMdd-bXX-x86/x64 文件名解析
    static QString packageVersion(const QString &srcPath, PackageKind kind,
                                  PackageArch arch = PackageArch::Unknown);
    static QString packageStamp(const QString &srcPath, PackageKind kind);

    // 查询服务器最新安装包信息（服务器本机读本地元数据；客户端走 HTTP）
    UpdateInfo checkUpdate(QString *err = nullptr,
                           PackageKind kind = PackageKind::Pc,
                           PackageArch arch = PackageArch::Unknown) const;
    // 上传安装包（服务器本机直接复制；客户端按 1MB 分块 POST）
    bool uploadPackage(const QString &srcPath, QString *err,
                       const std::function<void(int)> &progress = {},
                       PackageKind kind = PackageKind::Pc,
                       PackageArch arch = PackageArch::Unknown) const;
    // 下载安装包到 destPath（服务器本机复制；客户端流式 HTTP 下载）
    bool downloadPackage(const QString &destPath, QString *err,
                         const std::function<void(int, qint64, qint64)> &progress = {},
                         PackageKind kind = PackageKind::Pc,
                         PackageArch arch = PackageArch::Unknown) const;

private:
    UpdateManager() = default;
    static QString serverBaseUrl();
    QJsonObject postJson(const QString &path, const QJsonObject &payload,
                         QString *err, int timeoutMs) const;
};

#endif // UPDATEMANAGER_H
