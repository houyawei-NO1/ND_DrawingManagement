package com.houyawei.nddrawing.data

data class Product(
    val id: Int,
    val category: String,
    val materialCode: String,
    val name: String,
    val model: String,
    val hwVersion: String,
    val swVersion: String,
    val remark: String,
    val creator: String,
    val createdAt: String,
    val updatedAt: String,
    val ext: Map<String, String>,
    // 库存页：PC 端 /api/sync/products 已下发，手机端此前解析时丢弃
    val qty: Int = 0,
    val minQty: Int = 0,
    val maxQty: Int = 0
)

// PC 端 field_config 的同步结果：某分类下特有字段的键、中文标签与显示顺序
data class FieldConfig(
    val category: String,
    val key: String,
    val label: String,
    val sort: Int = 0
)

data class Drawing(
    val id: Int,
    val productId: Int,
    val dtype: Int,        // 0 下发图, 1 会签图
    val format: String,    // jpg / png / pdf
    val drawingDate: String,
    val createdAt: String,
    val relPath: String,   // PC 端相对存储根的路径
    val fileName: String,
    var localPath: String = ""  // 手机端下载后的本地路径
)

// 每日结存数量记录（PC 端 /api/sync/qty_logs 下发）
// recordDate 为 yyyy-MM-dd；同一产品同一天只保留一条，手机端按日期升序算用量
data class QtyLog(
    val productId: Int,
    val qty: Int,
    val recordDate: String,
    val createdAt: String = ""
)
