#ifndef NDMESSAGEBOX_H
#define NDMESSAGEBOX_H

#include "overlaymask.h"

class QEventLoop;
class QEvent;
class QKeyEvent;

// 统一小提示窗：全屏半透明黑遮罩 + 深色卡片，与「选择本机角色」同风格
class NdMessageBox : public OverlayMask
{
public:
    enum Kind { Info, Warning, Error, Question };

    static void information(QWidget *parent, const QString &title, const QString &text);
    static void warning(QWidget *parent, const QString &title, const QString &text);
    static void critical(QWidget *parent, const QString &title, const QString &text);
    // 宽卡片详情窗（可传 HTML 表格），用于展示「修改前 / 修改后」这类多行明细
    static void details(QWidget *parent, const QString &title, const QString &html);
    // 返回 true 表示点了“确定”，false 表示取消/点遮罩
    // blueOk=true 时“确定”按钮用蓝色（用于清理缓存等“执行动作”类确认）
    static bool question(QWidget *parent, const QString &title, const QString &text,
                         bool blueOk = false);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;

private:
    NdMessageBox(QWidget *parent, Kind kind, const QString &title, const QString &text,
                 bool blueOk = false, int width = 420);
    static bool run(QWidget *parent, Kind kind, const QString &title, const QString &text,
                    bool blueOk = false, int width = 420);
    void finish(bool ok);

    QEventLoop *m_loop = nullptr;
    bool m_ok = false;
};

#endif // NDMESSAGEBOX_H
