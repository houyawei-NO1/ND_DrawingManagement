package com.houyawei.nddrawing.util

import android.content.Context
import android.net.ConnectivityManager
import android.net.LinkAddress
import android.net.LinkProperties
import android.net.NetworkCapabilities
import android.os.Build
import java.net.Inet4Address
import java.net.InetSocketAddress
import java.net.Socket
import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.cancel
import kotlinx.coroutines.coroutineScope
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext

// USB 网络共享（有线）同步：通常是「手机开 USB 网络共享、手机当热点」，电脑作为 DHCP 客户端
// 拿到与手机同一子网的一个 IP。手机需在自己的 USB 子网里找到运行 LanHttpServer(8080) 的电脑。
object UsbSync {

    // 自动查找电脑 IP：优先用已保存的 IP，否则扫描手机 USB/RNDIS 接口所在子网的 8080。
    // 返回可用 IP，找不到或异常时返回 null。
    suspend fun findHost(ctx: Context, savedHost: String?, port: Int): String? = withContext(Dispatchers.IO) {
        val candidates = LinkedHashSet<String>()
        savedHost?.takeIf { it.isNotBlank() }?.let { candidates.add(it) }
        candidates.addAll(collectUsbSubnetHosts(ctx))
        if (candidates.isEmpty()) return@withContext null
        var result: String? = null
        try {
            coroutineScope {
                for (c in candidates) {
                    launch {
                        if (result == null && tcpReachable(c, port)) {
                            result = c
                            this@coroutineScope.cancel() // 命中后取消其余探测，立即返回
                        }
                    }
                }
            }
        } catch (_: CancellationException) {
            // 命中后主动取消，属正常流程
        }
        result
    }

    // 收集手机 USB/RNDIS 接口所在子网内的所有主机地址（排除手机自身）
    private fun collectUsbSubnetHosts(ctx: Context): List<String> {
        val out = mutableListOf<String>()
        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.LOLLIPOP) return out
        val cm = ctx.getSystemService(Context.CONNECTIVITY_SERVICE) as? ConnectivityManager ?: return out
        for (net in cm.allNetworks) {
            val caps = cm.getNetworkCapabilities(net) ?: continue
            val lp = cm.getLinkProperties(net) ?: continue
            val name = lp.interfaceName ?: ""
            val isUsb = (Build.VERSION.SDK_INT >= Build.VERSION_CODES.LOLLIPOP &&
                    caps.hasTransport(NetworkCapabilities.TRANSPORT_USB)) ||
                    name.contains("rndis", true) || name.contains("usb", true) ||
                    name.contains("tun", true)
            if (!isUsb) continue
            for (la in lp.linkAddresses) {
                val addr = la.address as? Inet4Address ?: continue
                val prefix = la.prefixLength
                if (prefix <= 0 || prefix > 30) continue
                val base = ipToInt(addr) and maskToInt(prefix)
                val count = 1 shl (32 - prefix)
                for (offset in 1 until count - 1) { // 跳过网络地址与广播地址
                    val ip = intToIp(base + offset)
                    if (ip != addr.hostAddress) out.add(ip)
                }
            }
        }
        return out
    }

    private fun ipToInt(a: Inet4Address): Int {
        val b = a.address
        return (b[0].toInt() and 0xff shl 24) or
                (b[1].toInt() and 0xff shl 16) or
                (b[2].toInt() and 0xff shl 8) or
                (b[3].toInt() and 0xff)
    }

    private fun maskToInt(prefix: Int): Int =
        if (prefix == 0) 0 else (0xffffffff.toInt() shl (32 - prefix))

    private fun intToIp(v: Int): String =
        "${(v shr 24) and 0xff}.${(v shr 16) and 0xff}.${(v shr 8) and 0xff}.${v and 0xff}"

    private fun tcpReachable(host: String, port: Int, timeoutMs: Int = 500): Boolean {
        return try {
            Socket().use { s -> s.connect(InetSocketAddress(host, port), timeoutMs); true }
        } catch (_: Exception) {
            false
        }
    }

    // 设备名：优先取蓝牙名称（通常是用户可见的设备名），取不到则用机型
    fun deviceName(ctx: Context): String {
        return try {
            val bt = android.bluetooth.BluetoothAdapter.getDefaultAdapter()
            bt?.name?.takeIf { it.isNotBlank() } ?: android.os.Build.MODEL
        } catch (_: Exception) {
            android.os.Build.MODEL
        }
    }
}
