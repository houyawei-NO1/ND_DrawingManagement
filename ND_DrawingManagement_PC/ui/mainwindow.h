#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QWidget>
#include <QList>
#include <QTimer>
#include "model/product.h"

class QLineEdit;
class QPushButton;
class QComboBox;
class QTableWidget;
class QLabel;
class QButtonGroup;
class QTextBrowser;
class QListWidget;
class QSplitter;
class QMouseEvent;
class QEvent;
class QStackedWidget;
class QPdfDocument;
class QPdfView;
class LanHttpServer;
class EntryDialog;
class ManagePage;
class SettingsDialog;

// 主窗口：无边框深色风格，分类Tab + 搜索 + 列表 + 图纸预览
class MainWindow : public QWidget
{
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

protected:
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void changeEvent(QEvent *event) override; // 最大化/还原时重排左右比例
    bool eventFilter(QObject *watched, QEvent *event) override;

private slots:
    void onSearch();
    void onCategoryChanged(int index);
    void onRowSelected(int row, int column);
    void onTableDoubleClicked(int row, int column); // 双击行：复制整行数据
    void onTableContextMenu(const QPoint &pos);     // 右键表格：按列复制该行数据
    void onPreviewDoubleClicked();
    void onPreviewContextMenu(const QPoint &globalPos); // 右键图纸框：资源管理器打开路径
    void onSupplementClicked(); // 选中的记录带入录入页补充信息
    void refreshSyncStatus();   // 定时刷新局域网同步状态栏

private:
    void buildTitleBar();
    void buildCategoryTabs();
    void buildToolBar();
    void buildBody();
    void buildAboutOverlay();   // 关于窗口：半透明遮罩 + 信息卡
    void buildLoginOverlay();   // 登录验证：半透明遮罩 + 账号密码卡
    void doLogin();             // 登录成功后更新状态并跳转
    void updateLoginLabel();    // 刷新标题栏登录状态
    void restartApplication();  // 切换本机角色后重新启动程序使配置生效
    void reloadTable();
    void showProductDetail(int productId);
    void previewDrawing(const Drawing &d); // 把指定图纸加载到右侧预览区
    void updatePreviewPixmap();           // 按预览框当前尺寸重新缩放图片
    void refitPdfView();                  // 按预览框当前尺寸重新适配 PDF 页面
    void applySplitRatio();               // 左右强制比例：普通 3:2，最大化/全屏 1:1
    void clearClientCache();              // 客户端：清理本机图纸预览缓存
    void applyStyle();

    // 标题栏
    QPoint m_dragPos;
    bool m_dragging = false;
    bool m_restarting = false; // 切换角色重启中：防止按钮连点启动多个进程

    // 分类
    QButtonGroup *m_menuGroup = nullptr; // 顶部菜单（主界面/设置/关于）
    QButtonGroup *m_catGroup = nullptr;
    QString m_currentCat;

    // 工具栏
    QLineEdit *m_searchEdit = nullptr;
    QPushButton *m_btnSearch = nullptr;
    QComboBox *m_cmbDrawingType = nullptr;

    // 列表
    QTableWidget *m_table = nullptr;
    QList<Product> m_products;

    // 预览
    QLabel *m_dtypeLabel = nullptr;      // 图纸类型：下发图 / 会签图 / 待补充
    QPushButton *m_btnSupplement = nullptr; // 补充信息（选中行后可用）
    int m_selectedProductId = -1;
    QStackedWidget *m_previewStack = nullptr;
    QLabel *m_previewLabel = nullptr;
    QPdfDocument *m_pdfDoc = nullptr;
    QPdfView *m_pdfView = nullptr;
    QLabel *m_previewInfo = nullptr;
    QString m_currentPreviewFile;
    QPixmap m_previewPix;  // 当前图纸原图，预览框尺寸变化时据此重新缩放
    QSize m_pdfFitSize;    // 上次 PDF 适配时的预览框尺寸，避免重复重排

    // 详情
    QTextBrowser *m_extFieldView = nullptr; // 分类特有字段（护套针数/硬件版本号等）
    QTextBrowser *m_remarkView = nullptr;
    QTextBrowser *m_changeLogView = nullptr;
    QLabel *m_moreTitle = nullptr;         // 「更多图纸」标题（框内，与另两栏一致）
    QListWidget *m_moreDrawings = nullptr; // 更多图纸：同一产品全部图纸缩略图
    QList<Drawing> m_drawings;             // 当前产品图纸（缩略图索引与之对应）
    QSplitter *m_split = nullptr;          // 列表 / 预览的分割器（左侧宽度按列宽自适应）

    LanHttpServer *m_httpServer = nullptr;
    QWidget *m_aboutOverlay = nullptr;
    QWidget *m_pwdOverlay = nullptr;   // 登录/验证遮罩
    QLabel *m_pwdTitle = nullptr;
    QLineEdit *m_userEdit = nullptr;
    QLineEdit *m_pwdEdit = nullptr;
    QLabel *m_pwdError = nullptr;
    int m_pwdTargetPage = 1; // 验证通过后进入的页面：1录入 2管理 3仅登录

    // 登录状态
    QLabel *m_loginLabel = nullptr;
    QString m_currentUser;
    bool m_isAdmin = false;

    // 局域网服务器信息（标题栏显示，可点击复制）
    QLabel *m_serverInfo = nullptr;
    quint16 m_serverPort = 0;
    QString m_accessUrl;
    void updateServerInfo(); // 刷新标题栏显示的局域网IP与端口

    // 局域网同步状态（底部状态栏显示连接设备与进度）
    QLabel *m_syncStatusLabel = nullptr;
    QLabel *m_clientStatusLabel = nullptr;  // 状态栏右侧：已连接客户端 IP（仅服务器显示）
    QTimer *m_syncTimer = nullptr;

    // 页面切换：0主界面 1录入 2管理 3设置
    QStackedWidget *m_stack = nullptr;
    EntryDialog *m_entryPage = nullptr;
    ManagePage *m_managePage = nullptr;
    SettingsDialog *m_settingsPage = nullptr;
};

#endif // MAINWINDOW_H
