#ifndef UPDATEMANAGER_H
#define UPDATEMANAGER_H

#include <QString>
#include <QJsonObject>
#include <functional>

// 安装包类型：PC 安装包与手机 APK 各自在服务器「安装包」目录里只保留最新一份
enum class PackageKind { Pc, Android };

// 服务器「安装包」目录里 update.json / update_android.json 描述的那一份安装包
struct UpdateInfo
{
    bool available = false;      // 服务器上是否有可用安装包
    QString version;             // 版本号（构建日期 yyyy-MM-dd）
    QString buildStamp;          // 精确构建戳 yyyyMMddHHmmss，用来区分同一天多次编译
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

    // 服务器工作路径下的「安装包」目录（客户端模式返回空）
    static QString packageDirectory();
    static QString metaFilePath(PackageKind kind = PackageKind::Pc);
    static QString partFilePath(PackageKind kind = PackageKind::Pc);
    static QString localPackagePath(PackageKind kind = PackageKind::Pc);
    static UpdateInfo readLocalMeta(PackageKind kind = PackageKind::Pc);
    static bool writeLocalMeta(const QString &fileName, const QString &version,
                               const QString &stamp, qint64 size, QString *err = nullptr,
                               PackageKind kind = PackageKind::Pc);
    // 服务器本机：把选中的安装包复制进来，清理同类型旧包并写元数据
    static bool installLocalPackage(const QString &srcPath, const QString &version,
                                    const QString &stamp, QString *err,
                                    const std::function<void(int)> &progress = {},
                                    PackageKind kind = PackageKind::Pc);
    // 上传时用的版本号/构建戳：PC 用本机版本，APK 从文件名或文件时间取构建日期
    static QString packageVersion(const QString &srcPath, PackageKind kind);
    static QString packageStamp(const QString &srcPath, PackageKind kind);

    // 查询服务器最新安装包信息（服务器本机读本地元数据；客户端走 HTTP）
    UpdateInfo checkUpdate(QString *err = nullptr,
                           PackageKind kind = PackageKind::Pc) const;
    // 上传安装包（服务器本机直接复制；客户端按 1MB 分块 POST）
    bool uploadPackage(const QString &srcPath, QString *err,
                       const std::function<void(int)> &progress = {},
                       PackageKind kind = PackageKind::Pc) const;
    // 下载安装包到 destPath（服务器本机复制；客户端流式 HTTP 下载）
    bool downloadPackage(const QString &destPath, QString *err,
                         const std::function<void(int, qint64, qint64)> &progress = {},
                         PackageKind kind = PackageKind::Pc) const;

private:
    UpdateManager() = default;
    static QString serverBaseUrl();
    QJsonObject postJson(const QString &path, const QJsonObject &payload,
                         QString *err, int timeoutMs) const;
};

#endif // UPDATEMANAGER_H
