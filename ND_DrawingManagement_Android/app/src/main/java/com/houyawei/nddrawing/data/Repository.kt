package com.houyawei.nddrawing.data

import java.io.File

// 简单的全局访问点：在 MainActivity.onCreate 中初始化
object Repository {
    lateinit var db: LocalDb
    // 同步根目录：Download/南都图纸（未授权时回退为应用私有外部存储/南都图纸）
    lateinit var baseDir: File
    // 当前根目录是否为公共 Download/南都图纸（用于界面提示与授权入口）
    var usesPublicDownload: Boolean = false
}
