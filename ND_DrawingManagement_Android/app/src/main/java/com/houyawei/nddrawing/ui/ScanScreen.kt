package com.houyawei.nddrawing.ui

import android.Manifest
import android.content.pm.PackageManager
import android.os.Handler
import android.os.Looper
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.camera.core.CameraSelector
import androidx.camera.core.ImageAnalysis
import androidx.camera.core.ImageProxy
import androidx.camera.core.Preview
import androidx.camera.lifecycle.ProcessCameraProvider
import androidx.camera.view.PreviewView
import androidx.compose.foundation.border
import androidx.compose.foundation.layout.*
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.platform.LocalLifecycleOwner
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.compose.ui.viewinterop.AndroidView
import androidx.core.content.ContextCompat
import androidx.navigation.NavHostController
import android.graphics.BitmapFactory
import android.net.Uri
import com.google.zxing.BarcodeFormat
import com.google.zxing.BinaryBitmap
import com.google.zxing.DecodeHintType
import com.google.zxing.MultiFormatReader
import com.google.zxing.PlanarYUVLuminanceSource
import com.google.zxing.RGBLuminanceSource
import com.google.zxing.common.HybridBinarizer
import java.util.concurrent.Executors

@Composable
fun ScanScreen(nav: NavHostController) {
    val context = LocalContext.current
    val lifecycleOwner = LocalLifecycleOwner.current
    val mainHandler = Handler(Looper.getMainLooper())

    var hasPermission by remember {
        mutableStateOf(
            ContextCompat.checkSelfPermission(context, Manifest.permission.CAMERA) == PackageManager.PERMISSION_GRANTED
        )
    }
    var scanning by remember { mutableStateOf(true) }
    var error by remember { mutableStateOf<String?>(null) }

    val launcher = rememberLauncherForActivityResult(ActivityResultContracts.RequestPermission()) { granted ->
        hasPermission = granted
    }
    LaunchedEffect(Unit) { if (!hasPermission) launcher.launch(Manifest.permission.CAMERA) }

    // 从相册/文件选择含二维码的图片进行扫描
    val pickLauncher = rememberLauncherForActivityResult(ActivityResultContracts.GetContent()) { uri: Uri? ->
        uri ?: return@rememberLauncherForActivityResult
        val cr = context.contentResolver
        try {
            val opts = BitmapFactory.Options().apply { inJustDecodeBounds = true }
            cr.openInputStream(uri)?.use { BitmapFactory.decodeStream(it, null, opts) }
            var sample = 1
            val maxDim = 1024
            while (opts.outWidth / sample > maxDim || opts.outHeight / sample > maxDim) sample *= 2
            val opts2 = BitmapFactory.Options().apply { inSampleSize = sample }
            val bmp = cr.openInputStream(uri)?.use { BitmapFactory.decodeStream(it, null, opts2) }
            bmp?.let { b ->
                val w = b.width
                val h = b.height
                val px = IntArray(w * h)
                b.getPixels(px, 0, w, 0, 0, w, h)
                val source = RGBLuminanceSource(w, h, px)
                val bin = BinaryBitmap(HybridBinarizer(source))
                val reader = MultiFormatReader()
                val hints = mapOf(DecodeHintType.POSSIBLE_FORMATS to listOf(BarcodeFormat.QR_CODE))
                val text = try { reader.decode(bin, hints).text } catch (_: Exception) { null }
                if (!text.isNullOrEmpty()) {
                    scanning = false
                    // 二维码原文可能含 / ? # : 等字符，会破坏导航路由导致崩溃，先编码
                    val safe = Uri.encode(text)
                    nav.navigate("scanresult/$safe") { popUpTo("scan") { inclusive = true } }
                } else {
                    error = "未能从图片中识别二维码"
                }
            }
        } catch (e: Exception) {
            error = "图片识别失败：${e.message}"
        }
    }

    Column(Modifier.fillMaxSize().navigationBarsPadding()) {
        // 顶部 PC 风格菜单栏
        var showAbout by remember { mutableStateOf(false) }
        AppMenuBar(current = "scan", nav = nav, onAbout = { showAbout = true })
        if (showAbout) AboutDialog(onDismiss = { showAbout = false })
        // 记录相机提供者，离开本页（导航/销毁）时解绑，避免相机在其它页面继续运行造成闪屏与资源占用
        val cameraProviderRef = remember { mutableStateOf<ProcessCameraProvider?>(null) }
        Box(Modifier.fillMaxSize()) {
            if (!hasPermission) {
                Column(
                    Modifier.fillMaxSize(),
                    verticalArrangement = Arrangement.Center,
                    horizontalAlignment = Alignment.CenterHorizontally
                ) {
                    Text("扫码需要相机权限", style = MaterialTheme.typography.bodyLarge)
                    Spacer(Modifier.height(12.dp))
                    Button(onClick = { launcher.launch(Manifest.permission.CAMERA) }) { Text("授予权限") }
                }
            } else {
                AndroidView(
                    modifier = Modifier.fillMaxSize(),
                    factory = { ctx ->
                        val previewView = PreviewView(ctx).apply {
                            // 关键：用 TextureView 而非默认 SurfaceView。
                            // SurfaceView 的相机画面由系统单独合成、会脱离 Compose 层级叠加显示，
                            // 导致从扫描切到其它页时仍能看到相机界面。TextureView 随视图树一起销毁，可彻底消除该闪屏。
                            implementationMode = PreviewView.ImplementationMode.COMPATIBLE
                        }
                        val future = ProcessCameraProvider.getInstance(ctx)
                        future.addListener({
                            try {
                                val cameraProvider = future.get()
                                cameraProviderRef.value = cameraProvider
                                val preview = Preview.Builder().build()
                                preview.setSurfaceProvider(previewView.surfaceProvider)
                                val analysis = ImageAnalysis.Builder()
                                    .setBackpressureStrategy(ImageAnalysis.STRATEGY_KEEP_ONLY_LATEST)
                                    .build()
                                val reader = MultiFormatReader()
                                val hints = mapOf(DecodeHintType.POSSIBLE_FORMATS to listOf(BarcodeFormat.QR_CODE))
                                val executor = Executors.newSingleThreadExecutor()
                                analysis.setAnalyzer(executor) { proxy: ImageProxy ->
                                    try {
                                        if (scanning) {
                                            // 直接取 Y 平面做灰度解码，避免额外的 RGB 转换
                                            val plane = proxy.planes[0]
                                            val rowStride = plane.rowStride
                                            val data = ByteArray(rowStride * proxy.height)
                                            plane.buffer.get(data, 0, minOf(data.size, plane.buffer.remaining()))
                                            val source = PlanarYUVLuminanceSource(
                                                data, rowStride, proxy.height,
                                                0, 0, proxy.width, proxy.height, false
                                            )
                                            val bitmap = BinaryBitmap(HybridBinarizer(source))
                                            val text = try {
                                                reader.decode(bitmap, hints).text
                                            } catch (_: Exception) {
                                                null
                                            }
                                            if (!text.isNullOrEmpty()) {
                                                scanning = false
                                                val code = Uri.encode(text)
                                                mainHandler.post {
                                                    nav.navigate("scanresult/$code") {
                                                        popUpTo("scan") { inclusive = true }
                                                    }
                                                }
                                            }
                                        }
                                    } finally {
                                        proxy.close()
                                    }
                                }
                                cameraProvider.unbindAll()
                                cameraProvider.bindToLifecycle(
                                    lifecycleOwner,
                                    CameraSelector.DEFAULT_BACK_CAMERA,
                                    preview, analysis
                                )
                            } catch (e: Exception) {
                                error = e.message ?: "相机初始化失败"
                            }
                        }, ContextCompat.getMainExecutor(ctx))
                        previewView
                    },
                    onRelease = { cameraProviderRef.value?.unbindAll() }
                )

                // 取景框 + 提示 + 选择图片扫描（覆盖在预览之上，仅作视觉引导，不拦截交互）
                Column(
                    Modifier.fillMaxSize(),
                    verticalArrangement = Arrangement.Center,
                    horizontalAlignment = Alignment.CenterHorizontally
                ) {
                    // 取景框：告知用户把二维码放进去
                    Box(
                        Modifier
                            .size(220.dp)
                            .border(2.dp, MaterialTheme.colorScheme.primary, MaterialTheme.shapes.medium)
                    )
                    Spacer(Modifier.height(16.dp))
                    Text(
                        "将二维码放入框内，自动识别",
                        color = MaterialTheme.colorScheme.onSurface,
                        style = MaterialTheme.typography.bodyMedium
                    )
                    Spacer(Modifier.height(16.dp))
                    Button(onClick = { pickLauncher.launch("image/*") }) {
                        Text("选择图片扫描", fontSize = 13.sp)
                    }
                }

                if (error != null) {
                    Column(
                        Modifier.fillMaxSize(),
                        verticalArrangement = Arrangement.Center,
                        horizontalAlignment = Alignment.CenterHorizontally
                    ) {
                        Text("相机不可用：$error", color = MaterialTheme.colorScheme.error)
                        Spacer(Modifier.height(12.dp))
                        Button(onClick = { nav.popBackStack() }) { Text("返回") }
                    }
                }
            }
        }
    }
}
