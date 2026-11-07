#ifndef PRODUCT_H
#define PRODUCT_H

#include <QString>
#include <QDate>
#include <QDateTime>
#include <QMap>

// 图纸类型
enum DrawingType {
    TypeIssued = 0,   // 下发图
    TypeCountersign = 1 // 会签图
};

// 图纸记录
struct Drawing {
    int id = -1;
    int productId = -1;
    int dtype = TypeIssued;   // DrawingType
    QString format;           // jpg / png / pdf
    QString filePath;         // 相对存储目录的路径
    QDate drawingDate;        // 图纸时间（出图日期，优先展示）
    QDateTime createdAt;      // 录入时间
};

// 分类特有字段配置（可在设置页维护）
struct FieldConfig {
    int id = -1;
    QString category;   // 所属分类
    QString fieldKey;   // 字段键，如 sheath_pins
    QString fieldLabel; // 显示名，如 护套针数
    int sort = 0;
    bool enabled = true;
};

// 产品记录（公共字段）
struct Product {
    int id = -1;
    QString category;      // 控制器/电器盒/开关/传感器/仪表/线束/方向盘
    QString materialCode;  // 物料编码
    QString name;          // 名称
    QString model;         // 型号
    QString hwVersion;     // 硬件版本号
    QString swVersion;     // 软件版本号
    QString remark;        // 备注（含 ZSGG 更改通知单号）
    QString creator;       // 录入人
    QDateTime createdAt;   // 录入时间
    QDateTime updatedAt;   // 修改时间

    QMap<QString, QString> extFields; // 分类特有字段 fieldKey -> value

    // 查询时联表带出：
    QDate drawingDate;     // 最新图纸时间（无效则展示录入时间）
    bool hasDrawing = false;

    // 库存数量：数量 / 最小数量（安全下限）/ 最大数量（安全上限）
    int qty = 0;
    int minQty = 0;
    int maxQty = 0;
};

// 更改记录
struct ChangeLog {
    int id = -1;
    int productId = -1;
    QString content;   // 更改说明（手动填写）
    QString oper;      // 操作人
    QDateTime createdAt;
};

// 数量录入日志（按日期记录，供日/周/月/年用量统计）
struct QtyLog {
    int id = -1;
    int productId = -1;
    int qty = 0;
    QDate recordDate;     // 录入日期（做用量统计用）
    QDateTime createdAt;  // 落库时间
};

// 用户操作日志：谁、对哪个型号、做了什么修改，修改前/后是什么
struct OpLog {
    int id = -1;
    QString oper;        // 操作账号（admin 或客户端登录用户名）
    QString action;      // 操作类型：新增产品 / 修改产品 / 删除产品 …
    QString model;       // 型号
    QString name;        // 名称
    QString detail;      // JSON 数组：[{"field":"分类","before":"…","after":"…"}]
    QDateTime createdAt;
};

#endif // PRODUCT_H
