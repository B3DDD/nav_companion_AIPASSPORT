package com.folotoy.navcompanion

import android.app.Notification
import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.PendingIntent
import android.app.Service
import android.bluetooth.BluetoothAdapter
import android.bluetooth.BluetoothGatt
import android.bluetooth.BluetoothGattCallback
import android.bluetooth.BluetoothGattCharacteristic
import android.bluetooth.BluetoothManager
import android.bluetooth.BluetoothProfile
import android.bluetooth.le.BluetoothLeScanner
import android.bluetooth.le.ScanCallback
import android.bluetooth.le.ScanFilter
import android.bluetooth.le.ScanResult
import android.bluetooth.le.ScanSettings
import android.content.Context
import android.content.Intent
import android.content.pm.PackageManager
import android.content.pm.ServiceInfo
import android.location.Location
import android.location.LocationListener
import android.location.LocationManager
import android.net.Uri
import android.os.Bundle
import android.os.Handler
import android.os.HandlerThread
import android.os.IBinder
import android.os.Looper
import android.os.ParcelUuid
import android.util.Log
import java.util.UUID

/**
 * 前台服务: 负责
 *  1) BLE central 扫描并连接 AI Passport 卡片(PassportNav);
 *  2) 读取 GPS, 结合 GPX 轨迹计算导航信息;
 *  3) 以 1Hz 将导航帧写入卡片 NAV 特征值。
 */
class BleNavService : Service() {

    companion object {
        private val TAG = "BleNavService"
        val SERVICE_UUID: UUID = UUID.fromString("6e40ab00-b5a3-f393-e0a9-e50e24dcca9e")
        val NAV_CHAR_UUID: UUID = UUID.fromString("6e40ab01-b5a3-f393-e0a9-e50e24dcca9e")
        const val DEVICE_NAME = "PassportNav"
        const val CHANNEL_ID = "nav_foreground"
        const val NOTIFICATION_ID = 1
        const val ACTION_START = "com.folotoy.navcompanion.START"
        const val ACTION_STOP = "com.folotoy.navcompanion.STOP"
        const val EXTRA_GPX_URI = "gpx_uri"
        const val EXTRA_REVERSE = "reverse"        // true = 反穿
        const val EXTRA_FORCE_START = "force_start" // true = 以当前位置为起点强制出发

        // 起点闸门：进入起点 100m 内并连续满足 2 次才正式出发。
        const val START_RADIUS_M = 100.0
        const val START_CONFIRM_SAMPLES = 2

        // 供 UI 轮询的内部状态（诊断用）。
        @Volatile
        var status: String = "未启动"

        // 轨迹解析结果（静态，加载后不再变化），单独一行显示，避免被连接状态覆盖。
        @Volatile
        var trackInfo: String = "尚未选择轨迹"
    }

    private var locationManager: LocationManager? = null
    private var scanner: BluetoothLeScanner? = null
    private var gatt: BluetoothGatt? = null
    private var navChar: BluetoothGattCharacteristic? = null
    private var track: List<GeoMath.LatLon> = emptyList()
    private var waypoints: List<GpxParser.Waypoint> = emptyList()
    private var scanning = false
    private var handlerThread: HandlerThread? = null
    private var bgLooper: Looper? = null
    private val mainHandler = Handler(Looper.getMainLooper())
    private var heartbeatOn = false
    private var lastFrameAt = 0L

    // ---- 起点锚定 / 方向 / 累计 ----
    private var direction = NavEngine.Direction.FORWARD
    private var forceStart = false
    private var armed = false                 // 是否已正式出发（累计从此开始）
    private var startPoint: GeoMath.LatLon? = null
    private var totalAscentM = 0              // 路线总爬升（GPX 预计算）
    private var totalM = 0                    // 路线总长（米）
    private var checkpoints: List<NavEngine.Checkpoint> = emptyList()
    private var cpsStr = ""                   // 发送给卡片的 CP 千分比刻度
    private var inRangeCount = 0
    private val accumulator = TrackAccumulator()

    // 没有 GPS 更新时(如室内/刚启动)也周期性发一帧"无定位",
    // 让卡片能区分"蓝牙已连接"和"没连上"。
    private val heartbeatRunnable = object : Runnable {
        override fun run() {
            if (!heartbeatOn) return
            if (navChar != null && System.currentTimeMillis() - lastFrameAt > 3000) {
                writeFrame(NavFrame.buildNoFix(if (armed) 1 else 0))
            }
            mainHandler.postDelayed(this, 2000)
        }
    }

    private val scanCallback = object : ScanCallback() {
        private var count = 0

        override fun onScanResult(callbackType: Int, result: ScanResult) {
            count++
            // 优先按服务 UUID 匹配(在广播包里, 比扫描响应里的名字可靠); 名字作兜底。
            val byUuid = result.scanRecord?.serviceUuids
                ?.any { it.uuid == SERVICE_UUID } == true
            val name = result.scanRecord?.deviceName ?: result.device.name
            val byName = name == DEVICE_NAME
            if (byUuid || byName) {
                Log.i(TAG, "发现 PassportNav: ${result.device.address} (uuid=$byUuid name=$byName)")
                status = "轨迹 ${track.size}点 | 发现 PassportNav, 连接中…"
                stopScan()
                connect(result.device.address)
            } else {
                status = "轨迹 ${track.size}点 | 扫描中, 已发现 $count 个蓝牙设备"
            }
        }

        override fun onScanFailed(errorCode: Int) {
            Log.w(TAG, "扫描失败: $errorCode")
            status = "扫描失败 code=$errorCode"
            scanning = false
        }
    }

    private val gattCallback = object : BluetoothGattCallback() {
        override fun onConnectionStateChange(g: BluetoothGatt, gattStatus: Int, newState: Int) {
            if (newState == BluetoothProfile.STATE_CONNECTED) {
                Log.i(TAG, "已连接")
                BleNavService.status = "已连接, 发现服务…"
                g.discoverServices()
            } else if (newState == BluetoothProfile.STATE_DISCONNECTED) {
                Log.i(TAG, "已断开 status=$gattStatus")
                BleNavService.status = "已断开(gatt=$gattStatus), 重连中…"
                navChar = null
                gatt = null
                g.close()
                scheduleRescan()
            }
        }

        override fun onServicesDiscovered(g: BluetoothGatt, discStatus: Int) {
            if (discStatus != BluetoothGatt.GATT_SUCCESS) {
                Log.w(TAG, "服务发现失败: $discStatus")
                BleNavService.status = "服务发现失败 gatt=$discStatus"
                g.disconnect()
                return
            }
            val service = g.getService(SERVICE_UUID)
            if (service == null) {
                Log.w(TAG, "未找到导航服务")
                BleNavService.status = "未找到导航服务 UUID"
                g.disconnect()
                return
            }
            val ch = service.getCharacteristic(NAV_CHAR_UUID)
            if (ch == null) {
                Log.w(TAG, "未找到 NAV 特征值")
                BleNavService.status = "未找到 NAV 特征值"
                g.disconnect()
                return
            }
            navChar = ch
            BleNavService.status = "已连接, 推送中"
            Log.i(TAG, "NAV 特征值就绪")
        }

        override fun onCharacteristicWrite(
            g: BluetoothGatt,
            characteristic: BluetoothGattCharacteristic,
            status: Int
        ) {
            if (status != BluetoothGatt.GATT_SUCCESS) {
                Log.w(TAG, "特征值写入失败: $status")
            }
        }
    }

    private val locationListener = object : LocationListener {
        override fun onLocationChanged(location: Location) {
            val alt = if (location.hasAltitude()) location.altitude else null
            val pos = GeoMath.LatLon(location.latitude, location.longitude, alt)
            val fix = location.accuracy < 50.0f
            val now = System.currentTimeMillis()

            if (track.size < 2) {
                writeFrame(NavFrame.buildNoFix(0))
                status = "轨迹无效"
                return
            }

            // ---- 起点闸门：进入起点 100m 内并连续确认，才正式出发 ----
            if (!armed) {
                val sp = startPoint
                val dStart = if (sp != null) GeoMath.distanceMeters(pos, sp) else Double.MAX_VALUE
                if (fix && dStart <= START_RADIUS_M) inRangeCount++ else inRangeCount = 0
                if (forceStart || inRangeCount >= START_CONFIRM_SAMPLES) {
                    armed = true
                    accumulator.reset(now)
                    accumulator.add(location.latitude, location.longitude, alt, location.accuracy, now)
                    Log.i(TAG, "出发：距起点 ${dStart.toInt()}m, 方向=$direction")
                } else {
                    val dStartInt = if (fix) dStart.toInt() else -1
                    writeFrame(NavFrame.buildPending(dStartInt, totalM, cpsStr))
                    status = if (fix) "距起点 ${dStartInt}m（100m 内出发）" else "等待 GPS 定位…"
                    return
                }
            }

            // ---- 已出发 ----
            accumulator.add(location.latitude, location.longitude, alt, location.accuracy, now)
            val cumM = accumulator.distanceM.toInt()
            val ascM = accumulator.ascentM.toInt()
            val altM = alt?.toInt() ?: -1

            val result = NavEngine.compute(track, checkpoints, pos, direction)
            val frame = when {
                result == null || !fix -> NavFrame.buildNoFix(1)
                result.remainM <= 30 -> NavFrame.buildArrived(result.totalM, cumM, ascM, altM, cpsStr)
                else -> NavFrame.buildNav(result, altM, cumM, ascM, cpsStr)
            }
            writeFrame(frame)

            status = if (fix && result != null) {
                "已出发 ${cumM}m | 爬升 ${ascM}m | 剩余 ${result.remainM}m"
            } else {
                "已出发 | 等待 GPS…"
            }
        }

        @Deprecated("Deprecated in Java")
        override fun onStatusChanged(provider: String?, status: Int, extras: Bundle?) {}

        override fun onProviderEnabled(provider: String) {}

        override fun onProviderDisabled(provider: String) {}
    }

    override fun onCreate() {
        super.onCreate()
        createChannel()
        val bt = getSystemService(Context.BLUETOOTH_SERVICE) as BluetoothManager
        scanner = bt.adapter?.bluetoothLeScanner
        locationManager = getSystemService(Context.LOCATION_SERVICE) as LocationManager
        handlerThread = HandlerThread("nav-bg").also { it.start() }
        bgLooper = handlerThread?.looper
    }

    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
        startForeground(
            NOTIFICATION_ID,
            buildNotification(),
            ServiceInfo.FOREGROUND_SERVICE_TYPE_LOCATION or
                ServiceInfo.FOREGROUND_SERVICE_TYPE_CONNECTED_DEVICE
        )
        when (intent?.action) {
            ACTION_STOP -> {
                stopHeartbeat()
                stopSelf()
                return START_NOT_STICKY
            }
            ACTION_START -> {
                val uriStr = intent.getStringExtra(EXTRA_GPX_URI)
                direction = if (intent.getBooleanExtra(EXTRA_REVERSE, false)) {
                    NavEngine.Direction.REVERSE
                } else {
                    NavEngine.Direction.FORWARD
                }
                forceStart = intent.getBooleanExtra(EXTRA_FORCE_START, false)
                armed = false
                inRangeCount = 0
                if (uriStr != null) loadTrack(Uri.parse(uriStr))
                startScan()
                startLocation()
                startHeartbeat()
            }
        }
        return START_STICKY
    }

    override fun onDestroy() {
        stopHeartbeat()
        stopScan()
        stopLocation()
        gatt?.disconnect()
        gatt?.close()
        gatt = null
        handlerThread?.quitSafely()
        handlerThread = null
        super.onDestroy()
    }

    override fun onBind(intent: Intent?): IBinder? = null

    // ---- 内部实现 ----

    private fun loadTrack(uri: Uri) {
        try {
            contentResolver.openInputStream(uri)?.use { input ->
                val data = GpxParser.parse(input)
                track = data.track
                waypoints = data.waypoints
                totalAscentM = NavEngine.totalAscent(track).toInt()
                totalM = if (track.size >= 2) GeoMath.trackLength(track, 0, track.size - 1).toInt() else 0
                startPoint = NavEngine.startPoint(track, direction)
                checkpoints = NavEngine.planCheckpoints(track, waypoints, direction)
                cpsStr = if (checkpoints.isEmpty() || totalM <= 0) {
                    ""
                } else {
                    checkpoints.take(8).joinToString(",") {
                        ((it.routeDistM.toLong() * 1000L) / totalM).toString()
                    }
                }
                armed = false
                inRangeCount = 0
                accumulator.reset(System.currentTimeMillis())
                Log.i(TAG, "轨迹加载: ${track.size} 点(trk=${data.trkptCount}, rte=${data.rteptCount}), waypoints=${data.waypoints.size}(wpt=${data.wptCount}, rte命名=${data.namedRteptCount}), CP=${checkpoints.size}, 总爬升=${totalAscentM}m, 方向=$direction, cps=$cpsStr")
                trackInfo = "轨迹 ${track.size}点 | CP ${checkpoints.size}个 | 爬升${totalAscentM}m"
                if (track.isEmpty()) {
                    status = "GPX 已读取但轨迹为 0 点 (文件格式可能不对)"
                } else {
                    status = "轨迹 ${track.size}点 / ${checkpoints.size}个CP / ${totalAscentM}m爬升"
                }
            } ?: run {
                status = "无法打开 GPX 文件"
            }
        } catch (e: Exception) {
            Log.e(TAG, "GPX 解析失败", e)
            status = "GPX 解析失败: ${e.javaClass.simpleName}"
        }
    }

    private fun hasPermission(perm: String): Boolean =
        checkSelfPermission(perm) == PackageManager.PERMISSION_GRANTED

    private fun startScan() {
        if (scanning || scanner == null) return
        if (!hasPermission(android.Manifest.permission.BLUETOOTH_SCAN)) {
            Log.w(TAG, "缺少 BLUETOOTH_SCAN 权限")
            return
        }
        scanning = true
        status = "开始扫描…"
        try {
            val settings = ScanSettings.Builder()
                .setScanMode(ScanSettings.SCAN_MODE_LOW_LATENCY)
                .build()
            scanner?.startScan(null, settings, scanCallback)
        } catch (e: SecurityException) {
            Log.w(TAG, "扫描启动失败", e)
            scanning = false
        }
    }

    private fun stopScan() {
        if (!scanning) return
        scanning = false
        try {
            scanner?.stopScan(scanCallback)
        } catch (e: SecurityException) {
            // 忽略
        }
    }

    private fun connect(address: String) {
        if (!hasPermission(android.Manifest.permission.BLUETOOTH_CONNECT)) {
            Log.w(TAG, "缺少 BLUETOOTH_CONNECT 权限")
            return
        }
        val adapter = (getSystemService(Context.BLUETOOTH_SERVICE) as BluetoothManager).adapter
        val device = adapter?.getRemoteDevice(address) ?: return
        gatt = device.connectGatt(this, false, gattCallback)
    }

    private fun scheduleRescan() {
        mainHandler.postDelayed({ if (gatt == null) startScan() }, 2000)
    }

    private fun startLocation() {
        if (!hasPermission(android.Manifest.permission.ACCESS_FINE_LOCATION)) {
            Log.w(TAG, "缺少定位权限")
            return
        }
        val lm = locationManager ?: return
        val looper = bgLooper ?: Looper.getMainLooper()
        try {
            lm.requestLocationUpdates(
                LocationManager.GPS_PROVIDER,
                2000L,
                2.0f,
                locationListener,
                looper
            )
        } catch (e: SecurityException) {
            Log.w(TAG, "定位启动失败", e)
        }
    }

    private fun stopLocation() {
        try {
            locationManager?.removeUpdates(locationListener)
        } catch (e: SecurityException) {
            // 忽略
        }
    }

    private fun writeFrame(frame: ByteArray) {
        val ch = navChar ?: return
        if (!hasPermission(android.Manifest.permission.BLUETOOTH_CONNECT)) return
        ch.writeType = BluetoothGattCharacteristic.WRITE_TYPE_DEFAULT
        ch.value = frame
        try {
            gatt?.writeCharacteristic(ch)
            lastFrameAt = System.currentTimeMillis()
        } catch (e: SecurityException) {
            Log.w(TAG, "写入失败", e)
        }
    }

    private fun startHeartbeat() {
        if (heartbeatOn) return
        heartbeatOn = true
        lastFrameAt = 0L
        mainHandler.post(heartbeatRunnable)
    }

    private fun stopHeartbeat() {
        heartbeatOn = false
        mainHandler.removeCallbacks(heartbeatRunnable)
    }

    private fun createChannel() {
        val nm = getSystemService(Context.NOTIFICATION_SERVICE) as NotificationManager
        nm.createNotificationChannel(
            NotificationChannel(CHANNEL_ID, "导航副屏", NotificationManager.IMPORTANCE_LOW)
        )
    }

    private fun buildNotification(): Notification {
        val pi = PendingIntent.getActivity(
            this, 0,
            Intent(this, MainActivity::class.java),
            PendingIntent.FLAG_IMMUTABLE
        )
        return Notification.Builder(this, CHANNEL_ID)
            .setContentTitle("导航副屏")
            .setContentText("正在向 AI Passport 推送导航")
            .setSmallIcon(android.R.drawable.ic_menu_compass)
            .setContentIntent(pi)
            .build()
    }
}
