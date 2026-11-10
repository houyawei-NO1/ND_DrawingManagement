#ifndef TOOLSPAGE_H
#define TOOLSPAGE_H

#include <QWidget>
#include <QList>
#include <QStringList>

class QTabWidget;
class ChipQueryPage;
class QComboBox;
class QTableWidget;
class QLineEdit;
class QLabel;

// 工具页：子菜单沿用设置页的 QTabWidget 风格，包含「库存查询」和「关于」
class ToolsPage : public QWidget
{
    Q_OBJECT
public:
    explicit ToolsPage(QWidget *parent = nullptr);

    // 切到工具页时由 MainWindow 调用：批次为空才向服务器取列表，离线时不阻塞
    void onPageShown();

    // 登录态变化由 MainWindow 推入：芯片查询页据此开放/关闭新增、修改、删除
    void setLoggedIn(const QString &user);

private slots:
    void onPickStockFile();
    void onSearchStock();
    void onDeleteStockBatch();
    void onStockDoubleClicked(int row, int column);
    void onStockContextMenu(const QPoint &pos);
    void refreshMobileVersion();
    void onDownloadApk();

private:
    QWidget *buildStockPage();
    QWidget *buildAboutPage();
    void refreshStockBatches();
    void fillStockTable(const QList<QStringList> &rows);

    QTabWidget *m_tabs = nullptr;
    QLabel *m_lblMobileVersion = nullptr;

    // 库存查询
    QComboBox *m_cmbStockBatch = nullptr;
    QLineEdit *m_edtStockKeyword = nullptr;
    QTableWidget *m_stockTable = nullptr;
    QLabel *m_lblStockInfo = nullptr;
    QLabel *m_lblStockResult = nullptr;

    // 芯片查询
    ChipQueryPage *m_chipPage = nullptr;
};

#endif // TOOLSPAGE_H
