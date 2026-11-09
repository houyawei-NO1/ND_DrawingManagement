#ifndef XLSREADER_H
#define XLSREADER_H

#include <QString>
#include <QStringList>
#include <QList>

// 轻量 BIFF8(.xls) 读取器：解析 OLE2 复合文档中的 Workbook 流，
// 支持共享字符串 SST/CONTINUE、LABELSST、NUMBER、RK、MULRK、LABEL。
// 只提取单元格文本，不处理公式、单元格格式、日期序列号；返回值按行号升序，
// 每行都补齐到最大列数，空单元格为空串。
namespace XlsReader {

bool read(const QString &path, QList<QStringList> &rows, QString *err = nullptr);

} // namespace XlsReader

#endif // XLSREADER_H
