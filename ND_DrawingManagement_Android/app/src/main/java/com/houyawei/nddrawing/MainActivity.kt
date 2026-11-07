package com.houyawei.nddrawing

import android.content.Context
import android.os.Bundle
import android.os.Environment
import androidx.activity.ComponentActivity
import androidx.core.view.WindowCompat
import androidx.core.view.WindowInsetsControllerCompat
import androidx.activity.compose.setContent
import androidx.compose.runtime.Composable
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Surface
import androidx.compose.ui.Modifier
import androidx.navigation.NavHostController
import androidx.navigation.compose.NavHost
import androidx.navigation.compose.composable
import androidx.navigation.compose.rememberNavController
import androidx.compose.animation.fadeIn
import androidx.compose.animation.fadeOut
import androidx.compose.animation.core.tween
import com.houyawei.nddrawing.data.LocalDb
import com.houyawei.nddrawing.data.Repository
import com.houyawei.nddrawing.ui.DrawingsScreen
import com.houyawei.nddrawing.ui.ProductDetailScreen
import com.houyawei.nddrawing.ui.ProductListScreen
import com.houyawei.nddrawing.ui.ScanScreen
import com.houyawei.nddrawing.ui.ScanResultScreen
import com.houyawei.nddrawing.ui.SettingsScreen
import com.houyawei.nddrawing.ui.SyncScreen
import com.houyawei.nddrawing.ui.theme.NDDrawingTheme
import com.houyawei.nddrawing.util.StoragePermission
import java.io.File

class MainActivity : ComponentActivity() {
    // 记录当前使用的根目录路径，仅在变化时重建数据库，避免每次 onResume 都重连
    private var currentBasePath: String? = null

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setupEdgeToEdge()
        ensureStorage(this)
        requestStoragePermissionIfNeeded()
        setContent {
            NDDrawingTheme {
                val nav = rememberNavController()
                Surface(
                    color = MaterialTheme.colorScheme.background,
                    modifier = Modifier.fillMaxSize()
                ) {
                    AppNav(nav)
                }
            }
        }
    }

    override fun onResume() {
        super.onResume()
        // 用户在系统设置里授予「所有文件访问」权限后回到应用，即时切换到公共 Download 目录
        ensureStorage(this)
    }

    // 沉浸式：状态栏/导航栏透明，让应用深色背景透到其下方，系统图标用浅色
    private fun setupEdgeToEdge() {
        val w = window
        WindowCompat.setDecorFitsSystemWindows(w, false)
        w.statusBarColor = android.graphics.Color.TRANSPARENT
        w.navigationBarColor = android.graphics.Color.TRANSPARENT
        val controller = WindowInsetsControllerCompat(w, w.decorView)
        controller.isAppearanceLightStatusBars = false
        controller.isAppearanceLightNavigationBars = false
    }

    // 确定同步根目录：优先实测能否写入公共 Download/南都图纸，失败则退回应用私有外部存储。
    // 不依赖 isExternalStorageManager 判断（HarmonyOS/部分 ROM 上该判断与实际能力不一致）。
    private fun storageRoot(ctx: Context): File {
        val publicDir = File(
            Environment.getExternalStoragePublicDirectory(Environment.DIRECTORY_DOWNLOADS),
            "南都图纸"
        )
        if (ensureWritable(publicDir)) return publicDir
        val priv = File(ctx.getExternalFilesDir(null) ?: ctx.filesDir, "南都图纸")
        priv.mkdirs()
        return priv
    }

    // 在目录里建一个探针文件再删除：真正能写才返回 true
    private fun ensureWritable(dir: File): Boolean {
        return try {
            if (!dir.exists() && !dir.mkdirs()) return false
            val probe = File(dir, ".write_test")
            probe.writeText("ok")
            probe.delete()
            true
        } catch (_: Exception) {
            false
        }
    }

    private fun ensureStorage(ctx: Context) {
        val base = storageRoot(ctx)
        val path = base.absolutePath
        if (path != currentBasePath) {
            if (!base.exists()) base.mkdirs()
            Repository.baseDir = base
            val publicPath = Environment
                .getExternalStoragePublicDirectory(Environment.DIRECTORY_DOWNLOADS)
                .absolutePath
            Repository.usesPublicDownload = path.startsWith(publicPath)
            // 数据库必须放在应用私有目录：Android 11+ 禁止在公共 Download 等共享存储上直接以 SQLite 打开数据库文件
            val dbFile = ctx.applicationContext.getDatabasePath("nd_drawing.db")
            dbFile.parentFile?.mkdirs()
            Repository.db = LocalDb(ctx.applicationContext, dbFile)
            currentBasePath = path
        }
    }

    // 首次启动自动尝试申请一次「所有文件访问」权限；之后由离线页/同步页上的「去授权」按钮手动触发。
    private fun requestStoragePermissionIfNeeded() {
        if (Repository.usesPublicDownload) return
        val prefs = getSharedPreferences("sync", Context.MODE_PRIVATE)
        if (prefs.getBoolean("asked_storage", false)) return
        prefs.edit().putBoolean("asked_storage", true).apply()
        StoragePermission.open(this)
    }
}

@Composable
fun AppNav(nav: NavHostController) {
    NavHost(
        navController = nav,
        startDestination = "list",
        // 进入新页时内容在「不透明的主题背景」之上淡入：
        // 先看到纯色背景、再加载内容，从而盖掉切页瞬间任何残留画面（如相机）。
        enterTransition = { fadeIn(animationSpec = tween(160)) },
        exitTransition = { fadeOut(animationSpec = tween(160)) },
        popEnterTransition = { fadeIn(animationSpec = tween(160)) },
        popExitTransition = { fadeOut(animationSpec = tween(160)) }
    ) {
        // 设置：入口页 + 两个二级子页面（与 PC 端设置的多子页一致）
        composable("settings") { SettingsScreen(nav) }
        composable("settings/sync") { SyncScreen(nav) }
        composable("settings/drawings") { DrawingsScreen(nav) }
        composable("list") { ProductListScreen(nav) }
        composable("detail/{id}") { backStack ->
            val id = backStack.arguments?.getString("id")?.toIntOrNull() ?: 0
            ProductDetailScreen(id = id, nav = nav)
        }
        composable("scan") { ScanScreen(nav) }
        composable("scanresult/{code}") { backStack ->
            val code = backStack.arguments?.getString("code") ?: ""
            ScanResultScreen(code = code, nav = nav)
        }
    }
}
