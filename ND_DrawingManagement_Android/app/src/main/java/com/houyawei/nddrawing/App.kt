package com.houyawei.nddrawing

import android.app.Application
import android.os.Build
import android.os.Environment
import java.io.File
import java.io.PrintWriter
import java.io.StringWriter
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale

// 全局崩溃捕获：把未捕获异常写入文件，便于在 Logcat 被 ROM 限制时离线查看。
class App : Application() {
    override fun onCreate() {
        super.onCreate()
        val prev = Thread.getDefaultUncaughtExceptionHandler()
        Thread.setDefaultUncaughtExceptionHandler { t, e ->
            try {
                writeCrash(e)
            } catch (_: Throwable) {
                // 忽略写入失败，保证原异常继续抛出
            }
            prev?.uncaughtException(t, e)
        }
    }

    private fun writeCrash(e: Throwable) {
        val sw = StringWriter()
        e.printStackTrace(PrintWriter(sw))
        val ts = SimpleDateFormat("yyyy-MM-dd HH:mm:ss", Locale.getDefault()).format(Date())
        val text = buildString {
            append("时间: ").append(ts).append('\n')
            append("设备: ").append(Build.MANUFACTURER).append(' ')
                .append(Build.MODEL)
                .append(" / Android ").append(Build.VERSION.RELEASE)
                .append(" (API ").append(Build.VERSION.SDK_INT).append(")\n")
            append("异常: \n").append(sw.toString()).append('\n')
        }

        // 1) 公共 Download（已授予「所有文件访问」时，用手机文件管理器即可查看）
        try {
            val pub = File(
                Environment.getExternalStoragePublicDirectory(Environment.DIRECTORY_DOWNLOADS),
                "南都图纸崩溃日志.log"
            )
            pub.writeText(text)
        } catch (_: Throwable) {
        }

        // 2) 应用外部私有目录（adb pull 可获取，无需任何权限）
        try {
            val dir = getExternalFilesDir(null) ?: filesDir
            File(dir, "crash.log").writeText(text)
        } catch (_: Throwable) {
        }
    }
}
