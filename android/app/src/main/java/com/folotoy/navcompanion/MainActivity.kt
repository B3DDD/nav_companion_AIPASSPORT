package com.folotoy.navcompanion

import android.Manifest
import android.app.Activity
import android.content.Intent
import android.content.pm.PackageManager
import android.net.Uri
import android.os.Bundle
import android.os.Handler
import android.os.Looper
import android.view.Gravity
import android.widget.Button
import android.widget.LinearLayout
import android.widget.ScrollView
import android.widget.TextView
import android.widget.Toast

/**
 * 主界面: 请求权限 → 选择 GPX 轨迹文件 → 启动/停止导航前台服务。
 * 纯程序化 UI, 无第三方依赖。
 */
class MainActivity : Activity() {

    companion object {
        private const val REQ_PERMISSIONS = 100
        private const val REQ_PICK_GPX = 200
        private val REQUIRED = arrayOf(
            Manifest.permission.BLUETOOTH_SCAN,
            Manifest.permission.BLUETOOTH_CONNECT,
            Manifest.permission.ACCESS_FINE_LOCATION,
            Manifest.permission.ACCESS_COARSE_LOCATION,
            Manifest.permission.POST_NOTIFICATIONS
        )
    }

    private var gpxUri: Uri? = null
    private var reverse = false
    private lateinit var statusText: TextView
    private lateinit var trackInfoText: TextView
    private lateinit var bleStatusText: TextView
    private lateinit var btnDirection: Button
    private lateinit var btnPick: Button
    private lateinit var btnStart: Button
    private lateinit var btnStop: Button

    private val statusHandler = Handler(Looper.getMainLooper())
    private val statusPoller = object : Runnable {
        override fun run() {
            trackInfoText.text = BleNavService.trackInfo
            bleStatusText.text = BleNavService.status
            statusHandler.postDelayed(this, 1000)
        }
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        buildUi()
        requestPermissionsIfNeeded()
        statusHandler.post(statusPoller)
    }

    override fun onDestroy() {
        statusHandler.removeCallbacks(statusPoller)
        super.onDestroy()
    }

    private fun buildUi() {
        val root = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            gravity = Gravity.CENTER_HORIZONTAL
            setPadding(48, 48, 48, 48)
        }
        val title = TextView(this).apply {
            text = "AI Passport 导航副屏"
            textSize = 20f
            gravity = Gravity.CENTER
        }
        statusText = TextView(this).apply {
            text = "尚未选择轨迹"
            textSize = 14f
            gravity = Gravity.CENTER
            setPadding(0, 24, 0, 8)
        }
        trackInfoText = TextView(this).apply {
            text = "尚未选择轨迹"
            textSize = 12f
            gravity = Gravity.CENTER
            setPadding(0, 0, 0, 8)
        }
        bleStatusText = TextView(this).apply {
            text = "蓝牙: 未启动"
            textSize = 13f
            gravity = Gravity.CENTER
            setPadding(0, 0, 0, 24)
        }
        btnDirection = Button(this).apply {
            text = "方向：正穿（点击切换）"
            setOnClickListener {
                reverse = !reverse
                text = if (reverse) "方向：反穿（点击切换）" else "方向：正穿（点击切换）"
            }
        }
        btnPick = Button(this).apply {
            text = "选择 GPX 轨迹"
            setOnClickListener { pickGpx() }
        }
        btnStart = Button(this).apply {
            text = "开始导航"
            isEnabled = false
            setOnClickListener { startNav() }
        }
        btnStop = Button(this).apply {
            text = "停止"
            isEnabled = false
            setOnClickListener { stopNav() }
        }

        root.addView(title)
        root.addView(statusText)
        root.addView(trackInfoText)
        root.addView(bleStatusText)
        root.addView(btnPick)
        root.addView(btnDirection)
        root.addView(btnStart)
        root.addView(btnStop)

        setContentView(ScrollView(this).apply { addView(root) })
    }

    private fun requestPermissionsIfNeeded() {
        val missing = REQUIRED.filter {
            checkSelfPermission(it) != PackageManager.PERMISSION_GRANTED
        }
        if (missing.isNotEmpty()) {
            requestPermissions(missing.toTypedArray(), REQ_PERMISSIONS)
        }
    }

    private fun pickGpx() {
        val intent = Intent(Intent.ACTION_OPEN_DOCUMENT).apply {
            addCategory(Intent.CATEGORY_OPENABLE)
            type = "*/*"
        }
        startActivityForResult(intent, REQ_PICK_GPX)
    }

    override fun onActivityResult(requestCode: Int, resultCode: Int, data: Intent?) {
        super.onActivityResult(requestCode, resultCode, data)
        if (requestCode == REQ_PICK_GPX && resultCode == RESULT_OK) {
            val uri = data?.data ?: return
            try {
                contentResolver.takePersistableUriPermission(
                    uri, Intent.FLAG_GRANT_READ_URI_PERMISSION
                )
            } catch (_: Exception) {
                // 部分来源不支持持久化授权, 忽略
            }
            gpxUri = uri
            statusText.text = "已选择: ${uri.lastPathSegment ?: "轨迹"}"
            btnStart.isEnabled = true
        }
    }

    private fun startNav() {
        val uri = gpxUri ?: return
        val intent = Intent(this, BleNavService::class.java).apply {
            action = BleNavService.ACTION_START
            putExtra(BleNavService.EXTRA_GPX_URI, uri.toString())
            putExtra(BleNavService.EXTRA_REVERSE, reverse)
        }
        startForegroundService(intent)
        btnStart.isEnabled = false
        btnStop.isEnabled = true
        statusText.text = if (reverse) "导航运行中（反穿）…" else "导航运行中（正穿）…"
        Toast.makeText(this, "已启动，走到起点 100m 内开始记录", Toast.LENGTH_LONG).show()
    }

    private fun stopNav() {
        val intent = Intent(this, BleNavService::class.java).apply {
            action = BleNavService.ACTION_STOP
        }
        startService(intent)
        btnStart.isEnabled = gpxUri != null
        btnStop.isEnabled = false
        statusText.text = "已停止"
    }

    override fun onRequestPermissionsResult(
        requestCode: Int,
        permissions: Array<out String>,
        grantResults: IntArray
    ) {
        super.onRequestPermissionsResult(requestCode, permissions, grantResults)
        if (requestCode == REQ_PERMISSIONS) {
            val denied = grantResults.any { it != PackageManager.PERMISSION_GRANTED }
            if (denied) {
                Toast.makeText(this, "缺少必要权限, 导航可能无法工作", Toast.LENGTH_LONG).show()
            }
        }
    }
}
