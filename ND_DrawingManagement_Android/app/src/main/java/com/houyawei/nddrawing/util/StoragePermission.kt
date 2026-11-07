package com.houyawei.nddrawing.util

import android.Manifest
import android.app.Activity
import android.content.Context
import android.content.ContextWrapper
import android.content.Intent
import android.net.Uri
import android.os.Build
import android.provider.Settings
import android.widget.Toast
import androidx.core.app.ActivityCompat

// 统一的存储授权入口：供界面上的「去授权」按钮与首次启动调用。
//  - Android 11+（含荣耀 Android 17 / API 36）：跳转系统「所有文件访问」设置页，多入口依次尝试
//  - 更低版本：请求 WRITE/READ_EXTERNAL_STORAGE 运行时权限
object StoragePermission {

    fun open(ctx: Context) {
        val activity = ctx.findActivity()
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
            // 各家 ROM 的入口不同，依次尝试：应用级 → 列表级（带包名）→ 列表级
            val candidates = listOf(
                Intent(
                    Settings.ACTION_MANAGE_APP_ALL_FILES_ACCESS_PERMISSION,
                    Uri.parse("package:${ctx.packageName}")
                ),
                Intent(
                    Settings.ACTION_MANAGE_ALL_FILES_ACCESS_PERMISSION,
                    Uri.parse("package:${ctx.packageName}")
                ),
                Intent(Settings.ACTION_MANAGE_ALL_FILES_ACCESS_PERMISSION)
            )
            for (intent in candidates) {
                try {
                    if (activity != null) activity.startActivity(intent)
                    else ctx.startActivity(intent.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK))
                    return
                } catch (_: Exception) {
                    // 该入口不可用，继续尝试下一个
                }
            }
            // 所有入口都不可用：提示手动路径（荣耀 MagicOS 多在「特殊访问权限」里）
            Toast.makeText(
                ctx,
                "请前往 设置 → 应用 → 特殊访问权限 → 所有文件访问权限，允许「南都图纸」后返回",
                Toast.LENGTH_LONG
            ).show()
        } else {
            if (activity != null) {
                ActivityCompat.requestPermissions(
                    activity,
                    arrayOf(
                        Manifest.permission.WRITE_EXTERNAL_STORAGE,
                        Manifest.permission.READ_EXTERNAL_STORAGE
                    ),
                    100
                )
            }
        }
    }

    private fun Context.findActivity(): Activity? {
        var c: Context = this
        while (c is ContextWrapper) {
            if (c is Activity) return c
            c = c.baseContext
        }
        return null
    }
}
