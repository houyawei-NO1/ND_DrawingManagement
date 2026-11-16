#ifndef CODECONVERTPAGE_H
#define CODECONVERTPAGE_H

#include <QWidget>

class QLineEdit;
class QLabel;
class QPushButton;
class QTableWidget;

// 工具 → 编码转换：展示中小拖型号编码对照规则，并按规则转换型号
class CodeConvertPage : public QWidget
{
    Q_OBJECT
public:
    explicit CodeConvertPage(QWidget *parent = nullptr);

private slots:
    void onConvert();
    void onCopyResult();
    void onFillExample();

private:
    void buildUi();
    QTableWidget *buildMachineTable(QWidget *parent);
    QTableWidget *buildPowerTable(QWidget *parent);
    void setHint(const QString &text, bool error);

    QLineEdit *m_edtInput = nullptr;
    QLineEdit *m_edtOutput = nullptr;
    QLabel *m_lblHint = nullptr;
    QPushButton *m_btnCopy = nullptr;
};

#endif // CODECONVERTPAGE_H
