package com.houyawei.nddrawing.data

// 与 PC 端 DatabaseManager::categories() 完全一致；仅在尚未同步过分类时使用。
// 同步页（图纸分类勾选）、图纸管理页、主页分类页签共用这一份，避免各处硬编码不一致。
val DEFAULT_CATEGORIES = listOf(
    "控制器", "电器盒", "开关", "传感器", "仪表", "线束", "方向盘"
)
