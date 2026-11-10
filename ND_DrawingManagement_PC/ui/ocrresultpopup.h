#ifndef OCRRESULTPOPUP_H
#define OCRRESULTPOPUP_H

#include <QFrame>
#include <QList>
#include <QStringList>
#include <QTextCursor>

class QLabel;
class QLineEdit;
class QListWidget;
class QPlainTextEdit;

// 主界面右侧的半透明黑色浮层：
// 上半部分识别型号，中间查找框快速定位 OCR 原文，下半部分 OCR 原文。
class OcrResultPopup : public QFrame
{
    Q_OBJECT
public:
    explicit OcrResultPopup(QWidget *parent = nullptr);

    // rawModels 是纠错前的原始 OCR 型号：列表展示纠正后的型号并标注「已纠错」，
    // 原文查找框仍用原始型号，才能在 OCR 原文里定位到那一行。
    void setResult(const QString &rawText, const QStringList &models,
                   const QList<int> &matchCounts, const QString &fileName,
                   const QStringList &rawModels = QStringList());

signals:
    void closed();

private:
    void onFindTextChanged(const QString &kw);
    void onFindNext();
    void onModelClicked(int row);
    void highlight(const QString &kw, int current);
    void updateCountLabel();
    QWidget *makeModelRow(int index, const QString &model, int count,
                          const QString &rawModel);

    QLabel *m_fileLabel = nullptr;
    QLabel *m_modelTitle = nullptr;
    QListWidget *m_modelList = nullptr;
    QLineEdit *m_findEdit = nullptr;
    QLabel *m_countLabel = nullptr;
    QPlainTextEdit *m_rawText = nullptr;
    QLabel *m_footLabel = nullptr;

    QList<QTextCursor> m_matches;
    int m_current = -1;
    QString m_keyword;
};

#endif // OCRRESULTPOPUP_H
