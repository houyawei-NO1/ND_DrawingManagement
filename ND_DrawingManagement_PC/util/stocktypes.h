#ifndef STOCKTYPES_H
#define STOCKTYPES_H

#include <QString>

// 一份 ERP 库存档案（对应数据库文件夹里的一个 .xls 文件）
struct StockBatch
{
    QString key;        // 批次标识：文件名去掉扩展名
    QString fileName;   // 原始文件名
    QString label;      // 界面展示名，如「管家婆20261107 16:40数据」
    int rows = 0;       // 导入的数据行数
    QString importedAt; // 导入时间
};

#endif // STOCKTYPES_H
