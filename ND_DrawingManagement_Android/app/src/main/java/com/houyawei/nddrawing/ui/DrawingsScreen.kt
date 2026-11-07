package com.houyawei.nddrawing.ui

import android.widget.Toast
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.navigation.NavHostController
import com.houyawei.nddrawing.data.DEFAULT_CATEGORIES
import com.houyawei.nddrawing.data.Repository
import com.houyawei.nddrawing.util.StorageUtils

/**
 * 图纸管理（设置子页）：列出所有分类及其文件夹大小，可单独删除该分类已下载的图纸文件；
 * 下方显示 APP 占用手机存储与图纸文件总占用。
 */
@Composable
fun DrawingsScreen(nav: NavHostController) {
    val ctx = LocalContext.current
    var showAbout by remember { mutableStateOf(false) }
    var refresh by remember { mutableStateOf(0) }
    var pendingDelete by remember { mutableStateOf<String?>(null) }

    val categories = remember {
        val stored = Repository.db.getCategories()
        if (stored.isEmpty()) DEFAULT_CATEGORIES else stored
    }
    // refresh 变化时重新统计（删除文件/点刷新后）
    val sizes = remember(refresh) {
        categories.associateWith { StorageUtils.categorySize(Repository.baseDir, it) }
    }
    val totalDrawings = remember(refresh) { StorageUtils.totalSize(Repository.baseDir) }
    val appUsed = remember(refresh) { StorageUtils.appUsage(ctx, Repository.baseDir) }

    val catToDelete = pendingDelete
    if (catToDelete != null) {
        AlertDialog(
            onDismissRequest = { pendingDelete = null },
            title = { Text("删除图纸文件") },
            text = {
                Text(
                    "将删除「$catToDelete」分类下的全部已下载图纸文件" +
                            "（${StorageUtils.formatSize(sizes[catToDelete] ?: 0L)}）。\n" +
                            "数据库清单保留，下次同步可重新下载。"
                )
            },
            confirmButton = {
                TextButton(onClick = {
                    val ok = StorageUtils.deleteCategory(Repository.baseDir, catToDelete)
                    Toast.makeText(
                        ctx,
                        if (ok) "已删除「$catToDelete」图纸文件" else "删除失败",
                        Toast.LENGTH_SHORT
                    ).show()
                    pendingDelete = null
                    refresh++
                }) { Text("删除", color = Color(0xFFE06C75)) }
            },
            dismissButton = {
                TextButton(onClick = { pendingDelete = null }) { Text("取消") }
            }
        )
    }

    Column(Modifier.fillMaxSize().navigationBarsPadding()) {
        AppMenuBar(current = "settings", nav = nav, onAbout = { showAbout = true })
        if (showAbout) AboutDialog(onDismiss = { showAbout = false })
        SettingsSubPageHeader(title = "图纸管理", onBack = { nav.popBackStack() })

        Column(
            Modifier
                .fillMaxSize()
                .padding(16.dp)
                .verticalScroll(rememberScrollState())
        ) {
            Text("按分类管理已下载的图纸文件", fontSize = 14.sp, color = Color(0xFFAAB4C8))
            Spacer(Modifier.height(12.dp))
            categories.forEach { cat ->
                val size = sizes[cat] ?: 0L
                Card(
                    Modifier.fillMaxWidth(),
                    shape = RoundedCornerShape(8.dp),
                    colors = CardDefaults.cardColors(containerColor = Color(0xFF2C344A))
                ) {
                    Row(
                        Modifier.fillMaxWidth().padding(horizontal = 14.dp, vertical = 10.dp),
                        verticalAlignment = Alignment.CenterVertically
                    ) {
                        Column(Modifier.weight(1f)) {
                            Text(cat, fontSize = 15.sp, fontWeight = FontWeight.Medium, color = Color.White)
                            Spacer(Modifier.height(3.dp))
                            Text(
                                "文件夹占用 ${StorageUtils.formatSize(size)}",
                                fontSize = 12.sp,
                                color = Color(0xFFAAB4C8)
                            )
                        }
                        Button(
                            onClick = { pendingDelete = cat },
                            enabled = size > 0L,
                            colors = ButtonDefaults.buttonColors(
                                containerColor = Color(0xFF6C2B34),
                                contentColor = Color.White
                            ),
                            contentPadding = PaddingValues(horizontal = 14.dp, vertical = 0.dp),
                            modifier = Modifier.height(34.dp)
                        ) { Text("删除", fontSize = 12.sp) }
                    }
                }
                Spacer(Modifier.height(8.dp))
            }

            Spacer(Modifier.height(10.dp))
            HorizontalDivider(color = Color(0xFF4A5468))
            Spacer(Modifier.height(12.dp))
            Text("存储占用", fontSize = 15.sp, fontWeight = FontWeight.Medium, color = Color.White)
            Spacer(Modifier.height(8.dp))
            UsageRow("APP 占用手机存储（约）", StorageUtils.formatSize(appUsed))
            UsageRow("图纸文件总占用", StorageUtils.formatSize(totalDrawings))
            Spacer(Modifier.height(8.dp))
            Text(
                "图纸目录：${Repository.baseDir.absolutePath}\n" +
                        "APP 占用 = 安装包 + 应用私有数据；不含已下载图纸。删除图纸后清单仍在，同步可重新下载。",
                fontSize = 11.sp,
                color = Color(0xFFAAB4C8),
                lineHeight = 16.sp
            )
            Spacer(Modifier.height(14.dp))
            Button(onClick = { refresh++ }, Modifier.fillMaxWidth()) {
                Text("刷新占用统计", fontSize = 14.sp)
            }
            Spacer(Modifier.height(10.dp))
        }
    }
}

@Composable
private fun UsageRow(label: String, value: String) {
    Row(
        Modifier.fillMaxWidth().padding(vertical = 4.dp),
        verticalAlignment = Alignment.CenterVertically
    ) {
        Text(label, Modifier.weight(1f), fontSize = 13.sp, color = Color(0xFFC8D2DC))
        Text(value, fontSize = 14.sp, fontWeight = FontWeight.Medium, color = Color(0xFF43CF7C))
    }
}
