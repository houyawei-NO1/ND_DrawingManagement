package com.houyawei.nddrawing.ui

/**
 * 二维码内容解析。
 * 约定 31 位数字串，分段如下：
 *   0..2   前两位：87 / 55 → 控制器；其他 → 暂未录入
 *   2..9   7 位：1010511 → 河南南都汽车电子；其他 → 非南都
 *   9..21  12 位：物料编码
 *   21..27 6 位：年月日（YYMMDD）
 *   27..31 4 位：产品序列号
 */
data class QrInfo(
    val category: String,     // 控制器 / 暂未录入
    val manufacturer: String, // 河南南都汽车电子 / 非南都
    val materialCode: String,
    val date: String,
    val serial: String,
    val raw: String
)

fun parseQr(raw: String): QrInfo {
    val s = raw.trim()
    val seg: (Int, Int) -> String = { start, len ->
        if (s.length >= start + len) s.substring(start, start + len) else ""
    }

    val prefix = seg(0, 2)
    val category = if (prefix == "87" || prefix == "55") "控制器" else "暂未录入"

    val mfr = seg(2, 7)
    val manufacturer = when {
        mfr == "1010511" -> "河南南都汽车电子"
        mfr.isEmpty() -> "未知"
        else -> "非南都"
    }

    val materialCode = seg(9, 12)

    val ymd = seg(21, 6)
    val date = if (ymd.length == 6) {
        "20${ymd.substring(0, 2)}-${ymd.substring(2, 4)}-${ymd.substring(4, 6)}"
    } else ymd

    val serial = seg(27, 4)

    return QrInfo(category, manufacturer, materialCode, date, serial, s)
}
