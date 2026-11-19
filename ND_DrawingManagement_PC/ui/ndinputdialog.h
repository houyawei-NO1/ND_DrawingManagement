#ifndef NDINPUTDIALOG_H
#define NDINPUTDIALOG_H

#include "overlaymask.h"

#include <QVariant>

class QEventLoop;
class QEvent;
class QKeyEvent;

// 统一风格的输入弹窗（全屏半透明遮罩 + 深色卡片），与 NdMessageBox 同款。
// ok=true 表示点了确定；value 为输入结果（文本或整数）。
class NdInputDialog : public OverlayMask
{
public:
    // 文本输入
    static bool getText(QWidget *parent, const QString &title, const QString &label,
                        const QString &defaultText, QString *out, int width = 440);
    // 整数输入（带上下箭头）
    static bool getInt(QWidget *parent, const QString &title, const QString &label,
                       int defaultValue, int min, int max, int *out, int width = 400);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;

private:
    NdInputDialog(QWidget *parent, const QString &title, const QString &label,
                  const QVariant &defaultValue, bool isInt, int min, int max, int width);
    void finish(bool ok);

    QEventLoop *m_loop = nullptr;
    bool m_ok = false;
    bool m_isInt = false;
    class QLineEdit *m_edit = nullptr;
    class QSpinBox *m_spin = nullptr;
    QVariant m_value;
};

#endif // NDINPUTDIALOG_H
