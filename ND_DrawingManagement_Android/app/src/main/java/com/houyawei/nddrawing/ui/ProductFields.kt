package com.houyawei.nddrawing.ui

import com.houyawei.nddrawing.data.Product
import com.houyawei.nddrawing.data.Repository

// PC 端已把硬件/软件版本号改成「控制器」分类的可配置特有字段（field_key=hw_version/sw_version）。
// 手机端未同步到字段配置（旧数据 / 旧版 PC 端）时用这两个内置中文标签兜底，避免露出英文 key。
private val BUILTIN_FIELD_LABELS = mapOf(
    "hw_version" to "硬件版本号",
    "sw_version" to "软件版本号"
)

// 按产品分类生成要显示的特有字段：中文标签 + 值，顺序与 PC/网页端 field_config 一致。
// 数据只取 product.ext（即 product_ext），不再额外显示 hwVersion/swVersion 固定行，避免重复；
// 没有配置或没有值的字段不显示，所以「开关」等分类不会出现空的硬件/软件版本行。
fun displayedExtFields(p: Product): List<Pair<String, String>> {
    val cfgs = Repository.db.fieldConfigs(p.category)
    val rows = mutableListOf<Pair<String, String>>()
    val used = mutableSetOf<String>()

    for (f in cfgs) {
        used.add(f.key)
        val v = p.ext[f.key].orEmpty()
        if (v.isNotBlank()) rows.add((f.label.ifEmpty { f.key }) to v)
    }
    // 兜底：没同步到字段配置时按内置映射显示；配置外实际存在的 ext 也补上，避免丢数据
    p.ext.forEach { (k, v) ->
        if (k !in used && v.isNotBlank())
            rows.add((BUILTIN_FIELD_LABELS[k] ?: k) to v)
    }
    return rows
}
