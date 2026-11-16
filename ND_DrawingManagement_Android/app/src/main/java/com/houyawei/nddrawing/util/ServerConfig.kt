package com.houyawei.nddrawing.util

// 局域网服务器默认地址：PC 客户端与手机端统一。
// 用户仍可在「设置 → 数据同步」里修改，修改后保存在 SharedPreferences("sync") 的 host / port，
// 之后以保存值为准；这里只作为首次使用 / 未保存时的默认值。
const val DEFAULT_SERVER_HOST = "192.168.1.16"
const val DEFAULT_SERVER_PORT = "8080"
