#ifndef UPDATEDIALOG_H
#define UPDATEDIALOG_H

#include "overlaymask.h"
#include "server/updatemanager.h"

class QLabel;
class QProgressBar;
class QPushButton;
class QMouseEvent;
class QEvent;

// 检查更新弹窗：与「选择本机角色」一致的全屏半透明遮罩 + 居中深色卡片，
// 显示版本号、下载进度，下载完成后可直接启动安装包覆盖安装
class UpdateDialog : public OverlayMask
{
    Q_OBJECT
public:
    static void checkForUpdate(QWidget *parent);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;

private:
    explicit UpdateDialog(QWidget *parent = nullptr);
    void fillInfo(const UpdateInfo &info, const QString &err);
    void onDownload();
    void onInstall();
    void closeDialog();

    UpdateInfo m_info;
    QString m_downloadPath;
    QWidget *m_card = nullptr;
    QLabel *m_lblDetail = nullptr;
    QLabel *m_lblStatus = nullptr;
    QProgressBar *m_bar = nullptr;
    QPushButton *m_btnDownload = nullptr;
    QPushButton *m_btnInstall = nullptr;
};

#endif // UPDATEDIALOG_H
