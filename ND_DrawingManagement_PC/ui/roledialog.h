#ifndef ROLEDIALOG_H
#define ROLEDIALOG_H

#include "overlaymask.h"

class QButtonGroup;
class QPushButton;
class QLineEdit;
class QLabel;
class QEventLoop;
class QKeyEvent;

// 角色选择结果。定义在类外：嵌套类型带默认成员初始化时，
// 不能在外层类的成员函数声明里直接用作默认参数（GCC 会报 incomplete class 错误）
struct RoleChoice {
    bool ok = false;          // 是否点了「确定」
    QString role;             // "server" / "client"
    QString host;             // 客户端需要连接的服务器地址
    int port = 8080;
};

// 选择本机角色：全屏黑色半透明遮罩 + 深色信息卡，
// 程序内所有遮罩统一走 OverlayMask，视觉与覆盖范围完全一致。
// 首次启动时（还没有主窗口）作为独立顶层窗口显示。
class RoleSelectDialog : public OverlayMask
{
    Q_OBJECT
public:
    // 同步显示，返回用户的选择；current 用于回填已有配置
    static RoleChoice choose(const RoleChoice &current = RoleChoice());

protected:
    void keyPressEvent(QKeyEvent *event) override;

private:
    explicit RoleSelectDialog(const RoleChoice &current, QWidget *parent = nullptr);
    void onRoleChanged();
    void finish(bool ok);

    RoleChoice m_result;
    QButtonGroup *m_roleGroup = nullptr;
    QPushButton *m_btnServer = nullptr;
    QPushButton *m_btnClient = nullptr;
    QLineEdit *m_edtHost = nullptr;
    QLineEdit *m_edtPort = nullptr;
    QLabel *m_status = nullptr;
    QEventLoop *m_loop = nullptr;
};

#endif // ROLEDIALOG_H
