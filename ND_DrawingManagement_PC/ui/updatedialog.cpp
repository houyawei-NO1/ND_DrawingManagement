#include "updatedialog.h"

#include <QGridLayout>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QProgressBar>
#include <QApplication>
#include <QEventLoop>
#include <QProcess>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QTimer>
#include <QStandardPaths>
#include <QMouseEvent>
#include <QEvent>

void UpdateDialog::checkForUpdate(QWidget *parent)
{
    // 全屏遮罩覆盖整块屏幕，和「选择本机角色」保持同一层级与风格
    QWidget *top = parent ? parent->window() : QApplication::activeWindow();
    if (!top) return;

    QApplication::setOverrideCursor(Qt::WaitCursor);
    QString err;
    const UpdateInfo info = UpdateManager::instance().checkUpdate(&err);
    QApplication::restoreOverrideCursor();

    // 重复点击只刷新内容，不叠加遮罩
    UpdateDialog *dlg = top->findChild<UpdateDialog *>(QString(),
                                                       Qt::FindDirectChildrenOnly);
    if (!dlg)
        dlg = new UpdateDialog(top);
    dlg->fillInfo(info, err);
    dlg->showOverlay();
}

UpdateDialog::UpdateDialog(QWidget *parent) : OverlayMask(parent)
{
    enableOverlay(); // 全屏半透明黑遮罩，由 OverlayMask 统一绘制
    hide();

    QGridLayout *lay = new QGridLayout(this);
    lay->setContentsMargins(0, 0, 0, 0);

    m_card = new QWidget(this);
    m_card->setObjectName(QStringLiteral("aboutCard"));
    m_card->setFixedSize(480, 360);
    QVBoxLayout *cl = new QVBoxLayout(m_card);
    cl->setContentsMargins(26, 22, 26, 20);
    cl->setSpacing(8);

    QLabel *title = new QLabel(QStringLiteral("检查更新"), m_card);
    title->setObjectName(QStringLiteral("aboutTitle"));
    title->setAlignment(Qt::AlignCenter);
    cl->addWidget(title);
    cl->addSpacing(4);

    m_lblDetail = new QLabel(m_card);
    m_lblDetail->setObjectName(QStringLiteral("aboutInfo"));
    m_lblDetail->setWordWrap(true);
    cl->addWidget(m_lblDetail);

    m_lblStatus = new QLabel(m_card);
    m_lblStatus->setObjectName(QStringLiteral("updateStatus"));
    m_lblStatus->setWordWrap(true);
    cl->addWidget(m_lblStatus);

    m_bar = new QProgressBar(m_card);
    m_bar->setRange(0, 100);
    m_bar->setValue(0);
    m_bar->setVisible(false);
    cl->addWidget(m_bar);

    QHBoxLayout *btns = new QHBoxLayout();
    btns->setSpacing(10);
    btns->addStretch(1);
    QPushButton *btnClose = new QPushButton(QStringLiteral("关 闭"), m_card);
    btnClose->setObjectName(QStringLiteral("btnDanger")); // 红色，与登录验证「取消」一致
    btnClose->setCursor(Qt::PointingHandCursor);
    connect(btnClose, &QPushButton::clicked, this, &UpdateDialog::closeDialog);
    btns->addWidget(btnClose);

    m_btnInstall = new QPushButton(QStringLiteral("立即覆盖安装"), m_card);
    m_btnInstall->setObjectName(QStringLiteral("btnEntry"));
    m_btnInstall->setCursor(Qt::PointingHandCursor);
    m_btnInstall->setVisible(false);
    connect(m_btnInstall, &QPushButton::clicked, this, &UpdateDialog::onInstall);
    btns->addWidget(m_btnInstall);

    m_btnDownload = new QPushButton(QStringLiteral("下载新版本"), m_card);
    m_btnDownload->setObjectName(QStringLiteral("btnEntry"));
    m_btnDownload->setCursor(Qt::PointingHandCursor);
    connect(m_btnDownload, &QPushButton::clicked, this, &UpdateDialog::onDownload);
    btns->addWidget(m_btnDownload);
    cl->addLayout(btns);
    cl->addStretch(1);

    lay->addWidget(m_card, 0, 0, Qt::AlignCenter);

    // 主窗口缩放时遮罩重新贴合屏幕
    if (parent)
        parent->installEventFilter(this);
}

bool UpdateDialog::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == parentWidget() && event->type() == QEvent::Resize)
        syncOverlay();
    return OverlayMask::eventFilter(watched, event);
}

void UpdateDialog::mousePressEvent(QMouseEvent *event)
{
    // 点击卡片外的遮罩空白处关闭，行为与登录验证弹窗一致
    if (m_card) {
        const QRect cardRect(m_card->mapToGlobal(QPoint(0, 0)), m_card->size());
        if (!cardRect.contains(event->globalPosition().toPoint()))
            closeDialog();
    }
    OverlayMask::mousePressEvent(event);
}

void UpdateDialog::closeDialog()
{
    // 与其它遮罩一致：只隐藏，实例随主窗口复用、随主窗口销毁
    hide();
}

void UpdateDialog::fillInfo(const UpdateInfo &info, const QString &err)
{
    m_info = info;
    m_downloadPath.clear();
    m_bar->setVisible(false);
    m_bar->setValue(0);
    const QString local = UpdateManager::instance().localVersion();
    QStringList lines;
    lines.append(QStringLiteral("当前版本：%1（版本号即程序构建日期）").arg(local));
    if (info.available) {
        lines.append(QStringLiteral("服务器版本：%1").arg(info.version));
        lines.append(QStringLiteral("安装包：%1（%2）")
                         .arg(info.fileName, UpdateManager::readableSize(info.size)));
        if (!info.uploadedAt.isEmpty())
            lines.append(QStringLiteral("上传时间：%1").arg(info.uploadedAt));
    } else {
        lines.append(QStringLiteral("服务器版本：暂无安装包"));
    }
    m_lblDetail->setText(lines.join(QLatin1Char('\n')));

    m_btnDownload->setText(QStringLiteral("下载新版本"));
    m_btnDownload->setVisible(false);
    m_btnInstall->setVisible(false);
    if (!err.isEmpty()) {
        m_lblStatus->setText(QStringLiteral("检查失败：%1").arg(err));
    } else if (!info.available) {
        m_lblStatus->setText(QStringLiteral(
            "服务器还没有安装包。请先在「设置 → 版本管理」上传新版安装包。"));
    } else if (UpdateManager::instance().hasUpdate(info)) {
        m_lblStatus->setText(QStringLiteral(
            "发现新版本 %1，点击「下载新版本」获取安装包，下载完成后可直接覆盖安装。")
                                 .arg(info.version));
        m_btnDownload->setVisible(true);
        m_btnDownload->setEnabled(true);
    } else {
        // 版本相同也允许重新下载服务器上的安装包，用于重装 / 修复 / 强制覆盖
        m_lblStatus->setText(QStringLiteral(
            "当前已是最新版本。如需重装或修复，可点「覆盖更新」重新下载服务器上的安装包并覆盖安装。"));
        m_btnDownload->setText(QStringLiteral("覆盖更新"));
        m_btnDownload->setVisible(true);
        m_btnDownload->setEnabled(true);
    }
}

void UpdateDialog::onDownload()
{
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
    if (dir.isEmpty()) {
        m_lblStatus->setText(QStringLiteral("找不到系统下载目录，无法保存安装包"));
        return;
    }
    QDir().mkpath(dir);
    QString name = m_info.fileName;
    if (name.isEmpty())
        name = QStringLiteral("ND_DrawingManagement_PC_Setup.exe");
    const QString path = QDir(dir).filePath(name);
    if (QFile::exists(path) && !QFile::remove(path)) {
        m_lblStatus->setText(QStringLiteral("同名安装包已被占用，请先关闭或删除：%1").arg(path));
        return;
    }

    m_btnDownload->setEnabled(false);
    m_btnInstall->setVisible(false);
    m_bar->setVisible(true);
    m_bar->setValue(0);
    m_lblStatus->setText(QStringLiteral("正在下载安装包…"));
    QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);

    QString err;
    const bool ok = UpdateManager::instance().downloadPackage(path, &err,
        [this](int percent, qint64 received, qint64 total) {
            m_bar->setValue(percent);
            m_lblStatus->setText(QStringLiteral("正在下载：%1 / %2（%3%）")
                .arg(UpdateManager::readableSize(received),
                     UpdateManager::readableSize(total))
                .arg(percent));
            QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
        });
    if (!ok) {
        m_lblStatus->setText(QStringLiteral("下载失败：%1").arg(err));
        m_btnDownload->setEnabled(true);
        return;
    }
    m_downloadPath = path;
    m_bar->setValue(100);
    m_lblStatus->setText(QStringLiteral(
        "下载完成：%1\n点击「立即覆盖安装」启动安装程序；安装向导会直接覆盖旧版本，无需先卸载。")
                             .arg(path));
    m_btnDownload->setVisible(false); // 已有「立即覆盖安装」，避免两个绿色按钮并排
    m_btnInstall->setVisible(true);
    m_btnInstall->setEnabled(true);
}

void UpdateDialog::onInstall()
{
    if (m_downloadPath.isEmpty() || !QFile::exists(m_downloadPath))
        return;
    if (!QProcess::startDetached(m_downloadPath, QStringList())) {
        m_lblStatus->setText(QStringLiteral("安装程序启动失败，请手动运行：%1").arg(m_downloadPath));
        return;
    }
    // 安装脚本会强制结束正在运行的程序；这里主动退出，避免 exe/dll 被占用
    closeDialog();
    QTimer::singleShot(200, qApp, [] { QCoreApplication::quit(); });
}
