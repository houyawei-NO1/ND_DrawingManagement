package com.houyawei.nddrawing.ui

import android.widget.Toast
import androidx.compose.foundation.ExperimentalFoundationApi
import androidx.compose.foundation.background
import androidx.compose.foundation.combinedClickable
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.text.BasicTextField
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalClipboardManager
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.platform.LocalLifecycleOwner
import androidx.compose.ui.text.AnnotatedString
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.navigation.NavHostController
import androidx.lifecycle.Lifecycle
import androidx.lifecycle.LifecycleEventObserver
import com.houyawei.nddrawing.data.DEFAULT_CATEGORIES
import com.houyawei.nddrawing.data.Product
import com.houyawei.nddrawing.data.Repository
import com.houyawei.nddrawing.util.StoragePermission

@OptIn(ExperimentalFoundationApi::class)
@Composable
fun ProductListScreen(nav: NavHostController) {
    var kw by remember { mutableStateOf("") }
    var category by remember { mutableStateOf("") } // "" 表示不限分类
    var products by remember { mutableStateOf(emptyList<Product>()) }
    var cats by remember { mutableStateOf(emptyList<String>()) }
    // 长按产品项后显示复制菜单；同时已默认复制三项，菜单用于只复制其中一项
    var menuProduct by remember { mutableStateOf<Product?>(null) }

    val ctx = LocalContext.current
    val clipboard = LocalClipboardManager.current
    var usesPublic by remember { mutableStateOf(Repository.usesPublicDownload) }
    val lifecycleOwner = LocalLifecycleOwner.current
    DisposableEffect(lifecycleOwner) {
        val obs = LifecycleEventObserver { _, e ->
            if (e == Lifecycle.Event.ON_RESUME) usesPublic = Repository.usesPublicDownload
        }
        lifecycleOwner.lifecycle.addObserver(obs)
        onDispose { lifecycleOwner.lifecycle.removeObserver(obs) }
    }

    fun copyText(text: String, tip: String) {
        if (text.isBlank()) {
            Toast.makeText(ctx, "内容为空", Toast.LENGTH_SHORT).show()
            return
        }
        clipboard.setText(AnnotatedString(text))
        Toast.makeText(ctx, tip, Toast.LENGTH_SHORT).show()
    }

    // 长按默认复制这三项（换行分隔），与 PC 端「复制整行数据」习惯一致
    fun allThree(p: Product): String =
        listOf(p.model, p.name, p.materialCode).filter { it.isNotBlank() }.joinToString("\n")

    LaunchedEffect(Unit) {
        // 分类与 PC 端保持一致：优先用同步下来的，未同步则使用兜底列表
        val stored = Repository.db.getCategories()
        cats = if (stored.isEmpty()) DEFAULT_CATEGORIES else stored
        products = Repository.db.allProducts("", "")
    }

    Column(Modifier.fillMaxSize().navigationBarsPadding()) {
        // 顶部 PC 风格菜单栏（主页 / 扫描 / 设置 / 关于，当前页常驻高亮）
        var showAbout by remember { mutableStateOf(false) }
        AppMenuBar(current = "list", nav = nav, onAbout = { showAbout = true })
        if (showAbout) AboutDialog(onDismiss = { showAbout = false })

        if (!usesPublic) {
            Card(
                Modifier.fillMaxWidth().padding(horizontal = 12.dp, vertical = 4.dp),
                colors = CardDefaults.cardColors(containerColor = MaterialTheme.colorScheme.secondaryContainer)
            ) {
                Column(Modifier.padding(10.dp)) {
                    Text(
                        "图纸目前保存在应用私有目录，未进入手机 Download。授予「存储 / 所有文件访问」权限后，将保存到 Download/南都图纸（分类/型号/图纸名）。",
                        fontSize = 12.sp
                    )
                    Spacer(Modifier.height(4.dp))
                    Button(
                        onClick = { StoragePermission.open(ctx) },
                        contentPadding = PaddingValues(horizontal = 14.dp, vertical = 0.dp),
                        modifier = Modifier.height(32.dp)
                    ) { Text("去授权", fontSize = 13.sp) }
                }
            }
        }

        // 搜索（跨全部分类检索）
        Row(
            Modifier.fillMaxWidth().padding(horizontal = 12.dp, vertical = 4.dp),
            verticalAlignment = Alignment.CenterVertically
        ) {
            Box(
                Modifier.weight(1f).height(38.dp)
                    .background(MaterialTheme.colorScheme.surfaceVariant, MaterialTheme.shapes.small)
                    .padding(horizontal = 12.dp),
                contentAlignment = Alignment.CenterStart
            ) {
                BasicTextField(
                    value = kw,
                    onValueChange = { kw = it },
                    singleLine = true,
                    textStyle = MaterialTheme.typography.bodyMedium,
                    decorationBox = { inner ->
                        Box(contentAlignment = Alignment.CenterStart) {
                            if (kw.isEmpty()) {
                                Text(
                                    "型号/编码/名称",
                                    fontSize = 13.sp,
                                    color = MaterialTheme.colorScheme.onSurfaceVariant
                                )
                            }
                            inner()
                        }
                    }
                )
            }
            Spacer(Modifier.width(8.dp))
            Button(
                onClick = { products = Repository.db.allProducts(category, kw) },
                contentPadding = PaddingValues(horizontal = 14.dp, vertical = 0.dp),
                modifier = Modifier.height(32.dp)
            ) { Text("搜索", fontSize = 12.sp) }
        }

        // 分类标签：首位「全部」= 不限分类（与 PC 端「所有类别」一致），其后为几大分类。
        // 搜索范围由当前页签决定：「全部」搜所有分类，具体分类只搜该分类。
        if (cats.isNotEmpty()) {
            val tabLabels = listOf("全部") + cats
            val selIdx = if (category.isEmpty()) 0 else (cats.indexOf(category) + 1).coerceAtLeast(0)
            ScrollableTabRow(
                selectedTabIndex = selIdx,
                edgePadding = 8.dp,
                containerColor = MaterialTheme.colorScheme.surface
            ) {
                tabLabels.forEachIndexed { i, label ->
                    val value = if (i == 0) "" else cats[i - 1] // "" 表示不限分类
                    Tab(
                        selected = category == value,
                        onClick = {
                            category = value
                            products = Repository.db.allProducts(value, kw)
                        },
                        modifier = Modifier.height(38.dp)
                    ) { Text(label, fontSize = 13.sp, modifier = Modifier.padding(horizontal = 8.dp)) }
                }
            }
        }

        if (products.isEmpty()) {
            Box(Modifier.fillMaxSize(), contentAlignment = Alignment.Center) {
                Text("暂无数据，请到顶部「设置 → 数据同步」从电脑拉取图纸", fontSize = 14.sp)
            }
        } else {
            LazyColumn(Modifier.fillMaxSize()) {
                items(products) { p ->
                    Box(Modifier.fillMaxWidth()) {
                        Column(
                            Modifier.fillMaxWidth()
                                .combinedClickable(
                                    onClick = { nav.navigate("detail/${p.id}") },
                                    onLongClick = {
                                        menuProduct = p
                                        copyText(
                                            allThree(p),
                                            "已复制型号 / 名称 / 物料编码，可在菜单里只复制一项"
                                        )
                                    }
                                )
                                .padding(horizontal = 14.dp, vertical = 9.dp)
                        ) {
                            Text(
                                p.model,
                                fontSize = 15.sp,
                                fontWeight = FontWeight.Medium,
                                color = MaterialTheme.colorScheme.onSurface
                            )
                            Spacer(Modifier.height(3.dp))
                            Text(
                                "${p.name}  ${p.materialCode}",
                                fontSize = 12.sp,
                                color = MaterialTheme.colorScheme.onSurfaceVariant
                            )
                        }
                        // 长按后弹出的单选复制菜单（参考 PC 端右键菜单）
                        DropdownMenu(
                            expanded = menuProduct?.id == p.id,
                            onDismissRequest = { menuProduct = null }
                        ) {
                            DropdownMenuItem(
                                text = { Text("复制全部（型号 / 名称 / 物料编码）", fontSize = 13.sp) },
                                onClick = {
                                    copyText(allThree(p), "已复制三项")
                                    menuProduct = null
                                }
                            )
                            HorizontalDivider()
                            DropdownMenuItem(
                                text = { Text("复制「型号」", fontSize = 13.sp) },
                                onClick = { copyText(p.model, "已复制型号"); menuProduct = null }
                            )
                            DropdownMenuItem(
                                text = { Text("复制「名称」", fontSize = 13.sp) },
                                onClick = { copyText(p.name, "已复制名称"); menuProduct = null }
                            )
                            DropdownMenuItem(
                                text = { Text("复制「物料编码」", fontSize = 13.sp) },
                                onClick = {
                                    copyText(p.materialCode, "已复制物料编码")
                                    menuProduct = null
                                }
                            )
                        }
                    }
                    HorizontalDivider(color = MaterialTheme.colorScheme.outlineVariant)
                }
            }
        }
    }
}
