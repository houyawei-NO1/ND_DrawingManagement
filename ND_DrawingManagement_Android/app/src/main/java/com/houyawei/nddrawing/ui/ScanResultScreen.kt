package com.houyawei.nddrawing.ui

import androidx.compose.foundation.Image
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.layout.ContentScale
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.unit.dp
import androidx.navigation.NavHostController
import android.net.Uri
import com.houyawei.nddrawing.data.Drawing
import com.houyawei.nddrawing.data.Repository
import java.io.File

@Composable
fun ScanResultScreen(code: String, nav: NavHostController) {
    // 路由中存放的是编码后的二维码内容，需还原
    val info = remember { parseQr(Uri.decode(code)) }
    val product = remember { Repository.db.productByMaterialCode(info.materialCode) }
    val drawings = remember { product?.let { Repository.db.drawingsOf(it.id) } ?: emptyList() }
    val sync = rememberProductDrawingSync(drawings)
    // 特有字段按分类配置显示，硬件/软件版本号不再作为固定行重复显示
    val extFields = remember(product?.id) { product?.let { displayedExtFields(it) } ?: emptyList() }
    var showAbout by remember { mutableStateOf(false) }

    Column(Modifier.fillMaxSize().navigationBarsPadding()) {
        AppMenuBar(current = "scan", nav = nav, onAbout = { showAbout = true })
        if (showAbout) AboutDialog(onDismiss = { showAbout = false })

        Column(
            Modifier
                .fillMaxSize()
                .verticalScroll(rememberScrollState())
                .statusBarsPadding()
                .padding(16.dp)
        ) {
            Text("二维码解析", style = MaterialTheme.typography.titleMedium)
            Spacer(Modifier.height(8.dp))
            InfoRow("产品类型", info.category)
            InfoRow("制造商", info.manufacturer)
            InfoRow("物料编码", info.materialCode)
            InfoRow("生产日期", info.date)
            InfoRow("产品序列号", info.serial)
            InfoRow("原始数据", info.raw)

            Spacer(Modifier.height(16.dp))
            HorizontalDivider()
            Spacer(Modifier.height(16.dp))
            Text("数据库内容", style = MaterialTheme.typography.titleMedium)

            if (product == null) {
                Spacer(Modifier.height(8.dp))
                if (info.materialCode.isEmpty()) {
                    Text(
                        "二维码未解析到物料编码，无法匹配数据库。",
                        style = MaterialTheme.typography.bodyMedium
                    )
                } else {
                    Text(
                        "未找到物料编码为「${info.materialCode}」的产品，请先通过「同步」拉取数据。",
                        style = MaterialTheme.typography.bodyMedium
                    )
                }
            } else {
                Spacer(Modifier.height(8.dp))
                Text(product.model, style = MaterialTheme.typography.headlineSmall)
                Spacer(Modifier.height(4.dp))
                Text(product.name, style = MaterialTheme.typography.titleMedium)
                Spacer(Modifier.height(8.dp))
                InfoRow("物料编码", product.materialCode)
                InfoRow("备注", product.remark)
                InfoRow("创建人", product.creator)
                InfoRow("分类", product.category)
                if (extFields.isNotEmpty()) {
                    Spacer(Modifier.height(12.dp))
                    Text("扩展字段", style = MaterialTheme.typography.titleMedium)
                    extFields.forEach { (label, value) -> InfoRow(label, value) }
                }

                Spacer(Modifier.height(16.dp))
                Text("图纸（${drawings.size}）", style = MaterialTheme.typography.titleMedium)
                if (sync.status.isNotEmpty()) {
                    Text(
                        sync.status,
                        style = MaterialTheme.typography.bodySmall,
                        color = MaterialTheme.colorScheme.primary
                    )
                }
                Spacer(Modifier.height(8.dp))
                drawings.forEach { d -> ScanDrawingItem(d, sync.revision) }
            }
        }
    }
}

@Composable
private fun InfoRow(label: String, value: String) {
    Row(Modifier.fillMaxWidth().padding(vertical = 2.dp)) {
        Text("$label：", style = MaterialTheme.typography.bodyMedium)
        Text(value, style = MaterialTheme.typography.bodyMedium)
    }
}

@Composable
private fun ScanDrawingItem(d: Drawing, revision: Int) {
    val ctx = LocalContext.current
    val label = (if (d.dtype == 1) "会签图" else "下发图") +
            if (d.drawingDate.isNotEmpty()) "  ${d.drawingDate}" else ""
    var showViewer by remember { mutableStateOf(false) }
    // 数据库没写 local_path 时，按 relPath 找刚从局域网补下来的文件
    val path = localDrawingPath(d)
    Card(Modifier.fillMaxWidth().padding(vertical = 4.dp)) {
        Column(Modifier.fillMaxWidth().padding(12.dp)) {
            Text(label, style = MaterialTheme.typography.titleSmall)
            Spacer(Modifier.height(4.dp))
            if (d.format.equals("pdf", true)) {
                if (File(path).exists()) {
                    OutlinedButton(onClick = { openPdf(ctx, path) }) {
                        Text("打开 PDF：${d.fileName}")
                    }
                } else {
                    Text(
                        "（PDF 未下载或文件缺失：${d.fileName}）",
                        style = MaterialTheme.typography.bodySmall
                    )
                }
            } else {
                val bmp = remember(path, revision) { loadBitmap(path) }
                if (bmp != null) {
                    Image(
                        bmp, contentDescription = d.fileName,
                        modifier = Modifier.fillMaxWidth().clickable { showViewer = true },
                        contentScale = ContentScale.FillWidth
                    )
                    Spacer(Modifier.height(4.dp))
                    Text(
                        "点击图片可放大查看，或用手机相册打开",
                        style = MaterialTheme.typography.bodySmall,
                        color = MaterialTheme.colorScheme.primary
                    )
                } else {
                    Text(
                        "（图片未下载或文件缺失：${d.fileName}）",
                        style = MaterialTheme.typography.bodySmall
                    )
                }
            }
        }
    }
    if (showViewer) ImageViewerDialog(path = path, onClose = { showViewer = false })
}
