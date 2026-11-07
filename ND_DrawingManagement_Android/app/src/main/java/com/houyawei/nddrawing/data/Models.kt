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
    val ext: Map<String, String>
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
