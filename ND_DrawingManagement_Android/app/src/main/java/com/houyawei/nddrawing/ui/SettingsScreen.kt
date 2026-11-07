package com.houyawei.nddrawing.ui

import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.Card
import androidx.compose.material3.CardDefaults
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.navigation.NavHostController

// 与 PC 端设置页一致的色板：深底卡片 + 绿色强调 + 次级灰字
private val SettingsCardBg = Color(0xFF2C344A)
private val SettingsAccent = Color(0xFF43CF7C)
private val SettingsSubText = Color(0xFFAAB4C8)

/**
 * 设置入口页：与 PC 端设置页的多子页结构一致，按功能进入二级页面。
 * 当前子页：数据同步（原「同步」页）、图纸管理。
 */
@Composable
fun SettingsScreen(nav: NavHostController) {
    var showAbout by remember { mutableStateOf(false) }
    Column(Modifier.fillMaxSize().navigationBarsPadding()) {
        AppMenuBar(current = "settings", nav = nav, onAbout = { showAbout = true })
        if (showAbout) AboutDialog(onDismiss = { showAbout = false })

        Column(
            Modifier.fillMaxSize().padding(16.dp),
            verticalArrangement = Arrangement.spacedBy(12.dp)
        ) {
            Text("设置", fontSize = 20.sp, fontWeight = FontWeight.Bold, color = Color.White)
            Text("按功能进入子页面，风格与 PC 端设置一致", fontSize = 12.sp, color = SettingsSubText)
            Spacer(Modifier.height(4.dp))
            SettingsEntry(
                title = "数据同步",
                desc = "从电脑导入产品、分类、字段配置与图纸",
                onClick = { nav.navigate("settings/sync") }
            )
            SettingsEntry(
                title = "图纸管理",
                desc = "按分类查看图纸占用空间，清理已下载文件",
                onClick = { nav.navigate("settings/drawings") }
            )
        }
    }
}

@Composable
private fun SettingsEntry(title: String, desc: String, onClick: () -> Unit) {
    Card(
        Modifier.fillMaxWidth().clickable(onClick = onClick),
        shape = RoundedCornerShape(8.dp),
        colors = CardDefaults.cardColors(containerColor = SettingsCardBg)
    ) {
        Row(
            Modifier.fillMaxWidth().padding(16.dp),
            verticalAlignment = Alignment.CenterVertically
        ) {
            Column(Modifier.weight(1f)) {
                Text(title, fontSize = 16.sp, fontWeight = FontWeight.Medium, color = Color.White)
                Spacer(Modifier.height(4.dp))
                Text(desc, fontSize = 12.sp, color = SettingsSubText)
            }
            Text("›", fontSize = 22.sp, color = SettingsAccent)
        }
    }
}

/** 设置子页统一标题栏：返回「设置」+ 当前子页名称 */
@Composable
fun SettingsSubPageHeader(title: String, onBack: () -> Unit) {
    Row(
        Modifier
            .fillMaxWidth()
            .background(SettingsCardBg)
            .padding(horizontal = 12.dp, vertical = 6.dp),
        verticalAlignment = Alignment.CenterVertically
    ) {
        TextButton(onClick = onBack) {
            Text("‹ 返回设置", color = SettingsAccent, fontSize = 13.sp)
        }
        Spacer(Modifier.width(6.dp))
        Text(title, fontSize = 16.sp, fontWeight = FontWeight.Medium, color = Color.White)
    }
}
