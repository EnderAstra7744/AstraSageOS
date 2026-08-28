package com.astrasage.os

import com.astrasage.os.desktop.DesktopManager
import com.astrasage.os.desktop.DesktopEnvironment
import com.astrasage.os.desktop.GridCell
import com.astrasage.os.desktop.GridOverlay
import com.astrasage.os.desktop.TaskbarStyle

import android.Manifest
import android.annotation.SuppressLint
import android.app.Activity
import android.content.ComponentName
import android.content.Intent
import android.content.pm.PackageManager
import android.graphics.BitmapFactory
import android.net.Uri
import android.os.Build
import android.os.Bundle
import android.os.Environment
import android.os.Handler
import android.os.Looper
import android.os.StatFs
import android.os.VibrationEffect
import android.os.Vibrator
import android.os.VibratorManager
import android.provider.Settings
import android.view.Gravity
import android.view.LayoutInflater
import android.view.MotionEvent
import android.view.View
import android.view.ViewConfiguration
import android.view.ViewGroup
import android.widget.FrameLayout
import android.widget.ImageView
import android.widget.LinearLayout
import android.widget.TextView
import android.widget.Toast
import androidx.appcompat.app.AppCompatActivity
import androidx.core.app.ActivityCompat
import androidx.core.content.ContextCompat
import androidx.core.view.isVisible
import androidx.recyclerview.widget.LinearLayoutManager
import androidx.recyclerview.widget.RecyclerView
import org.json.JSONObject
import java.io.File
import java.text.SimpleDateFormat
import java.util.Calendar
import java.util.Date
import java.util.Locale

class MainActivity : AppCompatActivity() {

    private lateinit var desktop: FrameLayout
    private lateinit var windowsHost: FrameLayout
    private lateinit var selection: SelectionOverlay
    private lateinit var startMenu: View
    private lateinit var taskbarApps: LinearLayout
    private lateinit var clockView: TextView
    private lateinit var wallpaper: ImageView
    private lateinit var welcomeOverlay: View
    private var gridOverlay: GridOverlay? = null
    private var cellW = 1f
    private var cellH = 1f
    private var gridOriginX = 12f
    private var gridOriginY = 12f

    private var allApps: List<AppInfo> = emptyList()
    private val iconViews = mutableMapOf<String, View>()
    private val selectedKeys = mutableSetOf<String>()
    private var lastTapTime = 0L
    private var lastTapKey = ""
    private val openWindows = linkedMapOf<String, WindowState>()
    private var zCounter = 10

    private var bandStartX = 0f
    private var bandStartY = 0f
    private var banding = false

    private val clockHandler = Handler(Looper.getMainLooper())
    private val clockTick = object : Runnable {
        override fun run() {
            updateClock()
            clockHandler.postDelayed(this, 1000)
        }
    }

    data class WindowState(
        val id: String,
        val title: String,
        val view: View,
        var minimized: Boolean = false,
        var maximized: Boolean = false,
        var normalX: Float = 24f,
        var normalY: Float = 80f,
        var normalW: Int = 0,
        var normalH: Int = 0
    )

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_main)
        DesktopManager.init(this)
        DesktopManager.setListener { de ->
            applyDesktopEnvironment(de)
            layoutDesktop()
            refreshTaskbar()
        }

        desktop = findViewById(R.id.desktop)
        windowsHost = findViewById(R.id.windowsHost)
        selection = findViewById(R.id.selectionOverlay)
        startMenu = findViewById(R.id.startMenu)
        taskbarApps = findViewById(R.id.taskbarApps)
        clockView = findViewById(R.id.clockView)
        wallpaper = findViewById(R.id.wallpaper)
        welcomeOverlay = findViewById(R.id.welcomeOverlay)

        applyCustomWallpaper()

        findViewById<View>(R.id.btnCenterLogo)?.setOnClickListener { openDeSelector() }
        findViewById<View>(R.id.btnStart).setOnClickListener {
            toggleStartMenu()
        }
        findViewById<View>(R.id.btnStart).setOnLongClickListener {
            openDeSelector()
            true
        }
        // Dim background closes start
        findViewById<View>(R.id.startMenu)?.setOnClickListener { hideStart() }
        setupStartMenu()
        clockView.setOnClickListener { openInternal("calendar", "Takvim / Saat") }

        setupRubberBand()
        setupWelcome()

        allApps = AppRepository.loadLauncherApps(packageManager)
        applyDesktopEnvironment(DesktopManager.current())
        layoutDesktop()
        refreshTaskbar()

        if (!Prefs.isWelcomeDone(this)) {
            welcomeOverlay.isVisible = true
        }

        clockHandler.post(clockTick)

        // Boot splash
        val splash = findViewById<View>(R.id.splashRoot)
        splash?.isVisible = true
        splash?.postDelayed({
            splash.animate().alpha(0f).setDuration(400).withEndAction {
                splash.isVisible = false
                splash.alpha = 1f
            }.start()
        }, 1200)
    }

    override fun onDestroy() {
        super.onDestroy()
        clockHandler.removeCallbacks(clockTick)
    }

    override fun onResume() {
        super.onResume()
        if (Prefs.isWelcomeDone(this)) layoutDesktop()
        updatePermStatus()
    }

    private fun hideStart() { startMenu.isVisible = false }

    private fun updateClock() {
        val now = Date()
        val lang = if (Prefs.getLang(this) == "en") Locale.US else Locale("tr", "TR")
        val t = SimpleDateFormat("HH:mm", lang).format(now)
        val d = SimpleDateFormat("d MMM", lang).format(now)
        clockView.text = "$t\n$d"
    }

    // ——— Welcome ———
    private var themeDark = true
    private var fontScale = 1.0f

    private fun setupWelcome() {
        themeDark = Prefs.isDarkTheme(this)
        fontScale = Prefs.getFontScale(this)
        val btnDark = findViewById<TextView>(R.id.btnThemeDark)
        val btnLight = findViewById<TextView>(R.id.btnThemeLight)
        val btnS = findViewById<TextView>(R.id.btnFontS)
        val btnM = findViewById<TextView>(R.id.btnFontM)
        val btnL = findViewById<TextView>(R.id.btnFontL)

        fun refreshThemeBtns() {
            btnDark.setBackgroundResource(if (themeDark) R.drawable.bg_choice_sel else R.drawable.bg_choice)
            btnLight.setBackgroundResource(if (!themeDark) R.drawable.bg_choice_sel else R.drawable.bg_choice)
            wallpaper.alpha = if (themeDark) 1f else 0.85f
        }
        fun refreshFontBtns() {
            btnS.setBackgroundResource(if (fontScale < 0.95f) R.drawable.bg_choice_sel else R.drawable.bg_choice)
            btnM.setBackgroundResource(if (fontScale in 0.95f..1.1f) R.drawable.bg_choice_sel else R.drawable.bg_choice)
            btnL.setBackgroundResource(if (fontScale > 1.1f) R.drawable.bg_choice_sel else R.drawable.bg_choice)
        }
        refreshThemeBtns()
        refreshFontBtns()

        btnDark.setOnClickListener { themeDark = true; Prefs.setDarkTheme(this, true); refreshThemeBtns() }
        btnLight.setOnClickListener { themeDark = false; Prefs.setDarkTheme(this, false); refreshThemeBtns() }
        btnS.setOnClickListener { fontScale = 0.85f; Prefs.setFontScale(this, fontScale); refreshFontBtns() }
        btnM.setOnClickListener { fontScale = 1.0f; Prefs.setFontScale(this, fontScale); refreshFontBtns() }
        btnL.setOnClickListener { fontScale = 1.2f; Prefs.setFontScale(this, fontScale); refreshFontBtns() }

        findViewById<View>(R.id.btnPickWallpaper).setOnClickListener {
            val i = Intent(Intent.ACTION_GET_CONTENT).setType("image/*")
            startActivityForResult(Intent.createChooser(i, "Arka plan"), 2001)
        }
        findViewById<View>(R.id.btnDefaultWall).setOnClickListener {
            getSharedPreferences("astrasage_os", MODE_PRIVATE).edit().remove("custom_wall").apply()
            wallpaper.setImageResource(R.drawable.wallpaper)
            Toast.makeText(this, "Varsayılan arka plan", Toast.LENGTH_SHORT).show()
        }
        findViewById<View>(R.id.btnGrantStorage).setOnClickListener { requestStorage() }
        findViewById<View>(R.id.btnPickDesktopApps)?.setOnClickListener { openDesktopAppsPicker() }
        findViewById<View>(R.id.btnWelcomeDone).setOnClickListener {
            Prefs.setWelcomeDone(this, true)
            welcomeOverlay.isVisible = false
            layoutDesktop()
            Toast.makeText(this, "Hoş geldin, ${Prefs.getUser(this)}!", Toast.LENGTH_SHORT).show()
        }
        updatePermStatus()
    }

    private fun updatePermStatus() {
        val tv = findViewById<TextView>(R.id.permStatus) ?: return
        val ok = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
            Environment.isExternalStorageManager()
        } else {
            ContextCompat.checkSelfPermission(this, Manifest.permission.READ_EXTERNAL_STORAGE) ==
                PackageManager.PERMISSION_GRANTED
        }
        tv.text = if (ok) "✓ Dosya izni verildi" else "Dosya izni henüz yok — yukarıdan ver"
        tv.setTextColor(ContextCompat.getColor(this, if (ok) R.color.accent else R.color.text_dim))
    }

    private fun requestStorage() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
            if (!Environment.isExternalStorageManager()) {
                try {
                    val i = Intent(Settings.ACTION_MANAGE_APP_ALL_FILES_ACCESS_PERMISSION)
                    i.data = Uri.parse("package:$packageName")
                    startActivity(i)
                } catch (_: Exception) {
                    startActivity(Intent(Settings.ACTION_MANAGE_ALL_FILES_ACCESS_PERMISSION))
                }
            } else Toast.makeText(this, "İzin zaten var", Toast.LENGTH_SHORT).show()
        } else {
            ActivityCompat.requestPermissions(this, arrayOf(Manifest.permission.READ_EXTERNAL_STORAGE), 100)
        }
    }

    @Deprecated("Deprecated in Java")
    override fun onActivityResult(requestCode: Int, resultCode: Int, data: Intent?) {
        super.onActivityResult(requestCode, resultCode, data)
        if (requestCode == 2001 && resultCode == Activity.RESULT_OK) {
            val uri = data?.data ?: return
            try {
                contentResolver.openInputStream(uri)?.use { input ->
                    val bytes = input.readBytes()
                    val b64 = android.util.Base64.encodeToString(bytes, android.util.Base64.DEFAULT)
                    // store path via copy to files dir
                    val out = File(filesDir, "custom_wallpaper.jpg")
                    out.writeBytes(bytes)
                    getSharedPreferences("astrasage_os", MODE_PRIVATE).edit()
                        .putString("custom_wall", out.absolutePath).apply()
                    wallpaper.setImageBitmap(BitmapFactory.decodeFile(out.absolutePath))
                    Toast.makeText(this, "Arka plan ayarlandı", Toast.LENGTH_SHORT).show()
                }
            } catch (e: Exception) {
                Toast.makeText(this, "Arka plan yüklenemedi", Toast.LENGTH_SHORT).show()
            }
        }
    }

    private fun applyCustomWallpaper() {
        val path = getSharedPreferences("astrasage_os", MODE_PRIVATE).getString("custom_wall", null)
        if (path != null && File(path).exists()) {
            wallpaper.setImageBitmap(BitmapFactory.decodeFile(path))
        }
    }

    // ——— Desktop icons ———
    private fun layoutDesktop() {
        desktop.removeAllViews()
        iconViews.clear()
        selectedKeys.clear()

        val de = DesktopManager.current()
        val gm = DesktopManager.grid()
        // Snapshot preferred cells then clear so stale keys don't block
        val preferredCells = gm.occupiedCells().toMap()
        gm.clear()
        // restore preferred into a side map used by placeIcon via temporary re-fill
        preferredCells.forEach { (k, c) -> gm.place(k, c) }
        // We'll re-place only icons we create; remove all first
        gm.clear()
        preferredCells.forEach { (k, c) -> /* keep for lookup */ }
        // Store on desktop tag for placeIcon
        desktop.tag = preferredCells

        val density = resources.displayMetrics.density
        val scale = (Prefs.getIconScale(this) * (if (de.largeTouch) 1.1f else 1f)).coerceIn(0.7f, 1.5f)
        val showNames = Prefs.showIconNames(this)
        // Cell wide enough for 2-line truncated names without spilling into next icon
        val iconW = (80 * density * scale).toInt().coerceIn((64 * density).toInt(), (110 * density).toInt())
        val iconH = ((if (showNames) 100 else 70) * density * scale).toInt()
            .coerceIn((56 * density).toInt(), (130 * density).toInt())
        val topPad = (10 * density).toInt()
        val leftPad = (12 * density).toInt()
        val gapX = (14 * density).toInt()
        val gapY = (14 * density).toInt()
        val usableW = ((desktop.width.takeIf { it > 0 } ?: resources.displayMetrics.widthPixels) - leftPad - 8).toFloat()
        val fitCols = maxOf(DesktopManager.current().gridCols, (usableW / (iconW + gapX)).toInt().coerceAtLeast(3))
        gm.cols = fitCols
        if (gm.rows < DesktopManager.current().gridRows) gm.rows = DesktopManager.current().gridRows
        val cols = fitCols
        val positions = loadPositions()
        val recycle = Prefs.getRecycle(this)
        val hidden = Prefs.getHiddenApps(this)

        var index = 0

        // System icons first
        fun sys(key: String, label: String, iconRes: Int, open: () -> Unit) {
            if (recycle.contains(key)) return
            val view = inflateResIcon(label, iconRes)
            placeIcon(view, key, index, positions, leftPad, topPad, iconW, iconH, gapX, gapY, cols)
            bindIconTouch(view, key, onOpen = open, onDelete = {
                Prefs.moveToRecycle(this, key)
                layoutDesktop()
                Toast.makeText(this, "Çöp kutusuna taşındı (masaüstünden)", Toast.LENGTH_SHORT).show()
            })
            index++
        }

        // Order matches default screen: AST → PC → Docs → DL → Trash → Cal → Net → Control → Options
        if (!recycle.contains("sys:ast")) {
            val astView = LayoutInflater.from(this).inflate(R.layout.item_desktop_icon, desktop, false)
            astView.findViewById<ImageView>(R.id.appIcon).setImageResource(R.drawable.ast_icon)
            styleIconLabel(astView.findViewById(R.id.appLabel), "AST")
            placeIcon(astView, "sys:ast", index, positions, leftPad, topPad, iconW, iconH, gapX, gapY, cols)
            bindIconTouch(astView, "sys:ast", onOpen = { openInternal("ast", "AST Terminal") }, onDelete = {
                Prefs.moveToRecycle(this, "sys:ast"); layoutDesktop()
            })
            index++
        }
        sys("sys:thispc", "Bu Bilgisayar", R.drawable.ic_thispc) { openInternal("thispc", "Bu Bilgisayar") }
        sys("sys:docs", "Belgeler", R.drawable.ic_docs) {
            Prefs.addDesktopPin(this, RealFs.home().absolutePath)
            openInternal("files", "Dosya Gezgini")
        }
        sys("sys:downloads", "İndirilenler", R.drawable.ic_downloads) {
            val dl = android.os.Environment.getExternalStoragePublicDirectory(
                android.os.Environment.DIRECTORY_DOWNLOADS
            )
            if (dl != null) Prefs.addDesktopPin(this, dl.absolutePath)
            startActivity(Intent(this, FilesActivity::class.java))
        }
        sys("sys:trash", "Çöp Kutusu", R.drawable.ic_trash) { openInternal("trash", "Çöp Kutusu") }
        sys("sys:calendar", "Takvim", R.drawable.ic_calendar) { openInternal("calendar", "Takvim / Saat") }
        sys("sys:network", "Ağ", R.drawable.ic_network) {
            try {
                startActivity(Intent(android.provider.Settings.ACTION_WIFI_SETTINGS))
            } catch (_: Exception) {
                Toast.makeText(this, "Ağ ayarları açılamadı", Toast.LENGTH_SHORT).show()
            }
        }
        sys("sys:control", "Denetim Masası", R.drawable.ic_control) {
            try {
                startActivity(Intent(android.provider.Settings.ACTION_SETTINGS))
            } catch (_: Exception) {
                Toast.makeText(this, "Ayarlar açılamadı", Toast.LENGTH_SHORT).show()
            }
        }
        sys("sys:options", "Seçenekler", R.drawable.ic_options) { openOptionsPanel() }

        // User-selected desktop apps (welcome / options)
        val enabledPkgs = Prefs.getDesktopAppsEnabled(this)
        val configured = Prefs.isDesktopAppsConfigured(this)
        val essential = if (configured) {
            allApps.filter { it.packageName in enabledPkgs }
        } else essentialApps(allApps)
        essential.forEach { app ->
            val pa = "${app.packageName}/${app.activityName}"
            if (hidden.contains(pa)) return@forEach
            val key = "app:$pa"
            if (recycle.contains(key)) return@forEach
            val view = inflateAppIcon(app)
            placeIcon(view, key, index, positions, leftPad, topPad, iconW, iconH, gapX, gapY, cols)
            bindIconTouch(view, key, onOpen = {
                if (isStorePackage(app.packageName)) openPlayStore()
                else openApp(app)
            }, onDelete = {
                Prefs.hideApp(this, pa)
                layoutDesktop()
                Toast.makeText(this, "Masaüstünden kaldırıldı (uygulama silinmedi)", Toast.LENGTH_SHORT).show()
            })
            index++
        }
        // AstraStore shortcut (opens real Play Store / market)
        if (!recycle.contains("sys:store")) {
            val view = inflateResIcon("AstraStore", R.drawable.ic_store)
            // Prefer image: use AS style - keep emoji for store
            placeIcon(view, "sys:store", index, positions, leftPad, topPad, iconW, iconH, gapX, gapY, cols)
            bindIconTouch(view, "sys:store", onOpen = { openPlayStore() }, onDelete = {
                Prefs.moveToRecycle(this, "sys:store")
                layoutDesktop()
            })
            index++
        }

        // User-pinned apps from drawer
        val pinned = Prefs.getPinnedApps(this)
        pinned.forEach { pa ->
            if (hidden.contains(pa)) return@forEach
            val key = "app:$pa"
            if (recycle.contains(key)) return@forEach
            // skip if already shown as essential
            val app = allApps.find { "${it.packageName}/${it.activityName}" == pa } ?: return@forEach
            val view = inflateAppIcon(app)
            placeIcon(view, key, index, positions, leftPad, topPad, iconW, iconH, gapX, gapY, cols)
            bindIconTouch(view, key, onOpen = { openApp(app) }, onDelete = null)
            index++
        }

        // File pins
        Prefs.getDesktopPins(this).forEach { path ->
            val f = File(path)
            if (!f.exists()) return@forEach
            val key = "file:$path"
            if (recycle.contains(key)) return@forEach
            val view = inflateEmojiIcon(f.name, if (f.isDirectory) "📁" else "📄")
            placeIcon(view, key, index, positions, leftPad, topPad, iconW, iconH, gapX, gapY, cols)
            bindIconTouch(view, key, onOpen = {
                if (f.isDirectory) openInternal("files", "Dosya Gezgini")
                else Toast.makeText(this, f.absolutePath, Toast.LENGTH_SHORT).show()
            }, onDelete = {
                Prefs.moveToRecycle(this, key)
                layoutDesktop()
                Toast.makeText(this, "Çöp kutusuna (masaüstü)", Toast.LENGTH_SHORT).show()
            })
            index++
        }
        DesktopManager.saveLayout(this)
        ensureGridOverlay()
    }


    private fun ensureGridOverlay() {
        if (gridOverlay == null) {
            val go = GridOverlay(this)
            desktop.addView(
                go, 0,
                FrameLayout.LayoutParams(
                    FrameLayout.LayoutParams.MATCH_PARENT,
                    FrameLayout.LayoutParams.MATCH_PARENT
                )
            )
            gridOverlay = go
        }
        val go = gridOverlay ?: return
        val gm = DesktopManager.grid()
        go.cols = gm.cols
        go.rows = gm.rows
        go.originX = gridOriginX
        go.originY = gridOriginY
        go.cellW = cellW
        go.cellH = cellH
        go.show = false
        go.invalidate()
    }

    private fun styleIconLabel(label: TextView, text: String) {
        // Shorten very long names for display
        val display = if (text.length > 18) text.take(16) + "…" else text
        label.text = display
        val fs = Prefs.getFontScale(this).coerceIn(0.75f, 1.4f)
        label.textSize = (9.5f * fs)
        label.maxLines = 2
        label.ellipsize = android.text.TextUtils.TruncateAt.END
        label.visibility = if (Prefs.showIconNames(this)) View.VISIBLE else View.GONE
    }

    private fun inflateAppIcon(app: AppInfo): View {
        val view = LayoutInflater.from(this).inflate(R.layout.item_desktop_icon, desktop, false)
        view.findViewById<ImageView>(R.id.appIcon).setImageDrawable(app.icon)
        styleIconLabel(view.findViewById(R.id.appLabel), app.label)
        return view
    }

    private fun inflateResIcon(label: String, iconRes: Int): View {
        val view = LayoutInflater.from(this).inflate(R.layout.item_desktop_icon, desktop, false)
        view.findViewById<ImageView>(R.id.appIcon).setImageResource(iconRes)
        styleIconLabel(view.findViewById(R.id.appLabel), label)
        return view
    }

    private fun inflateEmojiIcon(label: String, emoji: String): View {
        val view = LayoutInflater.from(this).inflate(R.layout.item_desktop_icon, desktop, false)
        val icon = view.findViewById<ImageView>(R.id.appIcon)
        icon.visibility = View.GONE
        val plate = view.findViewById<View>(R.id.iconBg).parent as FrameLayout
        val scale = Prefs.getIconScale(this)
        plate.addView(TextView(this).apply {
            text = emoji
            textSize = 24f * scale
            gravity = Gravity.CENTER
            layoutParams = FrameLayout.LayoutParams(
                FrameLayout.LayoutParams.MATCH_PARENT,
                FrameLayout.LayoutParams.MATCH_PARENT
            )
        })
        styleIconLabel(view.findViewById(R.id.appLabel), label)
        return view
    }

    private fun placeIcon(
        view: View, key: String, index: Int, positions: JSONObject,
        leftPad: Int, topPad: Int, iconW: Int, iconH: Int, gapX: Int, gapY: Int, cols: Int
    ) {
        val gm = DesktopManager.grid()
        cellW = (iconW + gapX).toFloat()
        cellH = (iconH + gapY).toFloat()
        gridOriginX = leftPad.toFloat()
        gridOriginY = topPad.toFloat()

        @Suppress("UNCHECKED_CAST")
        val prefMap = desktop.tag as? Map<String, GridCell>
        val preferred = prefMap?.get(key)
        val cell = gm.placeOrNextFree(key, preferred ?: GridCell(index % maxOf(gm.cols, 1), index / maxOf(gm.cols, 1)))
        val x = gm.pixelX(cell, cellW, gridOriginX)
        val y = gm.pixelY(cell, cellH, gridOriginY)
        view.x = x
        view.y = y
        desktop.addView(view, FrameLayout.LayoutParams(iconW, iconH))
        iconViews[key] = view
        updateSelectedBg(view, key)
    }

    @SuppressLint("ClickableViewAccessibility")
    private fun bindIconTouch(view: View, key: String, onOpen: () -> Unit, onDelete: (() -> Unit)? = null) {
        val touchSlop = ViewConfiguration.get(this).scaledTouchSlop
        val longPressTimeout = ViewConfiguration.getLongPressTimeout().toLong()
        var downX = 0f; var downY = 0f; var startX = 0f; var startY = 0f
        var dragging = false; var longPressed = false; var moved = false
        var runnable: Runnable? = null

        view.setOnTouchListener { v, event ->
            when (event.actionMasked) {
                MotionEvent.ACTION_DOWN -> {
                    downX = event.rawX; downY = event.rawY
                    startX = v.x; startY = v.y
                    dragging = false; longPressed = false; moved = false
                    runnable = Runnable {
                        longPressed = true; dragging = true
                        v.animate().scaleX(1.1f).scaleY(1.1f).setDuration(80).start()
                        v.elevation = 28f
                        vibrate()
                        // Highlight trash when dragging starts
                        iconViews["sys:trash"]?.animate()?.scaleX(1.15f)?.scaleY(1.15f)?.setDuration(100)?.start()
                    }
                    v.postDelayed(runnable!!, longPressTimeout)
                    true
                }
                MotionEvent.ACTION_MOVE -> {
                    val dx = event.rawX - downX; val dy = event.rawY - downY
                    if (!dragging && (kotlin.math.abs(dx) > touchSlop || kotlin.math.abs(dy) > touchSlop)) {
                        runnable?.let { v.removeCallbacks(it) }
                        if (!longPressed) return@setOnTouchListener true
                    }
                    if (dragging) {
                        moved = true
                        val maxX = (desktop.width - v.width).toFloat().coerceAtLeast(0f)
                        val maxY = (desktop.height - v.height).toFloat().coerceAtLeast(0f)
                        var nx = (startX + dx).coerceIn(0f, maxX)
                        var ny = (startY + dy).coerceIn(0f, maxY)
                        v.x = nx; v.y = ny
                        // Grid guide
                        gridOverlay?.let { go ->
                            go.show = true
                            val col = ((nx + v.width / 2f - gridOriginX) / cellW).toInt()
                            val row = ((ny + v.height / 2f - gridOriginY) / cellH).toInt()
                            go.setHighlight(col, row)
                        }
                        val over = key != "sys:trash" && isOverTrash(v)
                        iconViews["sys:trash"]?.alpha = if (over) 0.55f else 1f
                    }
                    true
                }
                MotionEvent.ACTION_UP, MotionEvent.ACTION_CANCEL -> {
                    runnable?.let { v.removeCallbacks(it) }
                    iconViews["sys:trash"]?.animate()?.scaleX(1f)?.scaleY(1f)?.alpha(1f)?.setDuration(100)?.start()
                    if (dragging) {
                        v.animate().scaleX(1f).scaleY(1f).setDuration(80).start()
                        v.elevation = 0f
                        dragging = false
                        // Drop on trash → remove from desktop only
                        if (moved && key != "sys:trash" && isOverTrash(v)) {
                            gridOverlay?.show = false
                            gridOverlay?.invalidate()
                            DesktopManager.grid().remove(key)
                            sendToTrash(key)
                            return@setOnTouchListener true
                        }
                        // GRID SNAP — never free-pixel; no overlap
                        val gm = DesktopManager.grid()
                        val cx = v.x + v.width / 2f
                        val cy = v.y + v.height / 2f
                        val cell = gm.snapToNearest(key, cx, cy, cellW, cellH, gridOriginX, gridOriginY)
                        v.x = gm.pixelX(cell, cellW, gridOriginX)
                        v.y = gm.pixelY(cell, cellH, gridOriginY)
                        DesktopManager.saveLayout(this@MainActivity)
                        gridOverlay?.show = false
                        gridOverlay?.invalidate()
                    } else if (!moved) {
                        val now = System.currentTimeMillis()
                        if (key == lastTapKey && now - lastTapTime < 350) {
                            // Double tap → Windows-style context menu
                            showDesktopContextMenu(v, key, onOpen)
                            lastTapTime = 0
                            lastTapKey = ""
                        } else {
                            lastTapTime = now
                            lastTapKey = key
                            // single tap delay slightly so double-tap can cancel open
                            v.postDelayed({
                                if (lastTapKey == key && System.currentTimeMillis() - lastTapTime >= 340) {
                                    if (selectedKeys.isNotEmpty()) toggleSelect(key) else onOpen()
                                    lastTapKey = ""
                                }
                            }, 360)
                        }
                    }
                    true
                }
                else -> false
            }
        }
    }

    private fun showDesktopContextMenu(anchor: View, key: String, onOpen: () -> Unit) {
        val popup = android.widget.PopupMenu(this, anchor, Gravity.END)
        popup.menu.add(0, 1, 0, "Aç")
        popup.menu.add(0, 2, 1, "⚙ Seçenekler")
        popup.menu.add(0, 3, 2, "Yeni klasör")
        popup.menu.add(0, 4, 3, "Yeni dosya")
        if (key != "sys:trash") popup.menu.add(0, 5, 4, "Çöp kutusuna taşı")
        try {
            val fieldMPopup = popup.javaClass.getDeclaredField("mPopup")
            fieldMPopup.isAccessible = true
            val mPopup = fieldMPopup.get(popup)
            mPopup?.javaClass?.getDeclaredMethod("setForceShowIcon", Boolean::class.java)?.invoke(mPopup, true)
        } catch (_: Exception) {}
        popup.setOnMenuItemClickListener { item ->
            when (item.itemId) {
                1 -> onOpen()
                2 -> openOptionsPanel()
                3 -> createDesktopFolder()
                4 -> createDesktopFile()
                5 -> sendToTrash(key)
            }
            true
        }
        popup.show()
    }


    private fun openDesktopAppsPicker() {
        val apps = allApps.ifEmpty { AppRepository.loadLauncherApps(packageManager) }
        val labels = apps.map { it.label }.toTypedArray()
        val pkgs = apps.map { it.packageName }.toTypedArray()
        val enabled = Prefs.getDesktopAppsEnabled(this).toMutableSet()
        val checked = BooleanArray(apps.size) { i ->
            if (!Prefs.isDesktopAppsConfigured(this)) {
                // default: essentials only
                pkgs[i] in essentialApps(apps).map { it.packageName }.toSet()
            } else pkgs[i] in enabled
        }
        android.app.AlertDialog.Builder(this)
            .setTitle("Masaüstünde göster")
            .setMultiChoiceItems(labels, checked) { _, which, isChecked ->
                checked[which] = isChecked
            }
            .setPositiveButton("Tamam") { _, _ ->
                val sel = mutableSetOf<String>()
                checked.forEachIndexed { i, on -> if (on) sel.add(pkgs[i]) }
                Prefs.setDesktopAppsEnabled(this, sel)
                layoutDesktop()
                Toast.makeText(this, "${sel.size} uygulama seçildi", Toast.LENGTH_SHORT).show()
            }
            .setNeutralButton("Hiçbiri") { _, _ ->
                Prefs.setDesktopAppsEnabled(this, emptySet())
                layoutDesktop()
            }
            .setNegativeButton("İptal", null)
            .show()
    }

    private fun openOptionsPanel() {
        openInternal("options", "Seçenekler")
    }

    private fun createDesktopFolder() {
        val name = "Yeni Klasör ${System.currentTimeMillis() % 1000}"
        val dir = File(RealFs.home(), name)
        if (dir.mkdirs()) {
            Prefs.addDesktopPin(this, dir.absolutePath)
            Toast.makeText(this, "Klasör: $name", Toast.LENGTH_SHORT).show()
            layoutDesktop()
        } else {
            Toast.makeText(this, "Klasör oluşturulamadı (izin?)", Toast.LENGTH_SHORT).show()
        }
    }

    private fun createDesktopFile() {
        val name = "not ${System.currentTimeMillis() % 1000}.txt"
        val f = File(RealFs.home(), name)
        try {
            f.writeText("AstraSage OS\n")
            Prefs.addDesktopPin(this, f.absolutePath)
            Toast.makeText(this, "Dosya: $name", Toast.LENGTH_SHORT).show()
            layoutDesktop()
        } catch (e: Exception) {
            Toast.makeText(this, "Dosya oluşturulamadı: ${e.message}", Toast.LENGTH_SHORT).show()
        }
    }

    private fun isOverTrash(dragged: View): Boolean {
        val trash = iconViews["sys:trash"] ?: return false
        val cx = dragged.x + dragged.width / 2f
        val cy = dragged.y + dragged.height / 2f
        return cx >= trash.x && cx <= trash.x + trash.width &&
            cy >= trash.y && cy <= trash.y + trash.height
    }

    private fun sendToTrash(key: String) {
        when {
            key.startsWith("app:") -> {
                Prefs.hideApp(this, key.removePrefix("app:"))
                Prefs.unpinApp(this, key.removePrefix("app:"))
            }
            key.startsWith("file:") -> Prefs.moveToRecycle(this, key)
            key.startsWith("sys:") -> Prefs.moveToRecycle(this, key)
            else -> Prefs.moveToRecycle(this, key)
        }
        Toast.makeText(this, "Çöp kutusuna taşındı (telefonda silinmedi)", Toast.LENGTH_SHORT).show()
        layoutDesktop()
    }

    private fun toggleSelect(key: String) {
        if (selectedKeys.contains(key)) selectedKeys.remove(key) else selectedKeys.add(key)
        iconViews[key]?.let { updateSelectedBg(it, key) }
    }

    private fun updateSelectedBg(view: View, key: String) {
        val plate = view.findViewById<View>(R.id.iconBg)
        plate.setBackgroundResource(
            if (selectedKeys.contains(key)) R.drawable.bg_icon_selected else R.drawable.bg_icon_plate
        )
    }

    private fun clearSelection() {
        val old = selectedKeys.toList()
        selectedKeys.clear()
        old.forEach { k -> iconViews[k]?.let { updateSelectedBg(it, k) } }
    }

    @SuppressLint("ClickableViewAccessibility")
    private fun setupRubberBand() {
        desktop.setOnTouchListener { _, event ->
            when (event.actionMasked) {
                MotionEvent.ACTION_DOWN -> {
                    hideStart()
                    bandStartX = event.x; bandStartY = event.y; banding = true
                    clearSelection()
                    selection.setRect(bandStartX, bandStartY, bandStartX, bandStartY)
                    true
                }
                MotionEvent.ACTION_MOVE -> {
                    if (!banding) return@setOnTouchListener false
                    selection.setRect(bandStartX, bandStartY, event.x, event.y)
                    selectIconsInRect(selection.selectionRect())
                    true
                }
                MotionEvent.ACTION_UP, MotionEvent.ACTION_CANCEL -> {
                    if (banding) {
                        val w = kotlin.math.abs(event.x - bandStartX)
                        val h = kotlin.math.abs(event.y - bandStartY)
                        banding = false
                        selection.clear()
                        // short tap on empty desktop → context menu
                        if (w < 12 && h < 12) {
                            showEmptyDesktopMenu(event.x, event.y)
                        }
                    }
                    true
                }
                else -> false
            }
        }
    }

    private fun showEmptyDesktopMenu(x: Float, y: Float) {
        val anchor = View(this).apply {
            layoutParams = FrameLayout.LayoutParams(1, 1)
            this.x = x; this.y = y
        }
        desktop.addView(anchor)
        val popup = android.widget.PopupMenu(this, anchor)
        popup.menu.add(0, 1, 0, "Yeni klasör")
        popup.menu.add(0, 2, 1, "Yeni dosya")
        popup.menu.add(0, 3, 2, "⚙ Seçenekler")
        popup.menu.add(0, 4, 3, "Yenile")
        popup.setOnMenuItemClickListener { item ->
            when (item.itemId) {
                1 -> createDesktopFolder()
                2 -> createDesktopFile()
                3 -> openOptionsPanel()
                4 -> layoutDesktop()
            }
            true
        }
        popup.setOnDismissListener { desktop.removeView(anchor) }
        popup.show()
    }

    private fun selectIconsInRect(r: android.graphics.RectF) {
        selectedKeys.clear()
        iconViews.forEach { (key, view) ->
            val left = view.x; val top = view.y
            val right = left + view.width; val bottom = top + view.height
            val hit = r.left < right && r.right > left && r.top < bottom && r.bottom > top
            if (hit) selectedKeys.add(key)
            updateSelectedBg(view, key)
        }
    }

    // ——— Window system ———
    private fun openInternal(id: String, title: String) {
        hideStart()
        openWindows[id]?.let { w ->
            if (w.minimized) restoreWindow(id)
            else bringFront(id)
            return
        }

        val density = resources.displayMetrics.density
        val panel = LayoutInflater.from(this).inflate(R.layout.panel_window, windowsHost, false)
        panel.findViewById<TextView>(R.id.panelTitle).text = title
        val content = panel.findViewById<FrameLayout>(R.id.panelContent)

        when (id) {
            "ast" -> content.addView(
                LayoutInflater.from(this).inflate(R.layout.activity_terminal, content, false).also {
                    // Wire simple terminal in-panel
                    wireEmbeddedTerminal(it)
                }
            )
            "files" -> {
                // Launch full files activity is better for permissions; also panel tip
                startActivity(Intent(this, FilesActivity::class.java))
                return
            }
            "thispc" -> content.addView(
                LayoutInflater.from(this).inflate(R.layout.panel_thispc, content, false).also { fillThisPc(it) }
            )
            "trash" -> content.addView(
                LayoutInflater.from(this).inflate(R.layout.panel_trash, content, false).also { fillTrash(it) }
            )
            "calendar" -> content.addView(
                LayoutInflater.from(this).inflate(R.layout.panel_calendar, content, false).also { fillCalendar(it) }
            )
            "options" -> content.addView(
                LayoutInflater.from(this).inflate(R.layout.panel_options, content, false).also { fillOptions(it) }
            )
            "deselector" -> content.addView(
                LayoutInflater.from(this).inflate(R.layout.panel_de_selector, content, false).also { fillDeSelector(it) }
            )
            "notepad" -> content.addView(
                LayoutInflater.from(this).inflate(R.layout.panel_notepad, content, false).also { fillNotepad(it) }
            )
            "paint" -> content.addView(
                LayoutInflater.from(this).inflate(R.layout.panel_paint, content, false).also { fillPaint(it) }
            )
            "weather" -> content.addView(
                LayoutInflater.from(this).inflate(R.layout.panel_weather, content, false).also { fillWeather(it) }
            )
            "music" -> content.addView(
                LayoutInflater.from(this).inflate(R.layout.panel_music, content, false).also { fillMusic(it) }
            )
            "disk" -> content.addView(
                LayoutInflater.from(this).inflate(R.layout.panel_disk, content, false).also { fillDisk(it) }
            )
        }

        val w = (resources.displayMetrics.widthPixels * 0.92f).toInt()
        val h = (resources.displayMetrics.heightPixels * 0.62f).toInt()
        val lp = FrameLayout.LayoutParams(w, h)
        panel.x = 16 * density
        panel.y = 48 * density
        windowsHost.addView(panel, lp)

        val state = WindowState(id, title, panel, normalW = w, normalH = h, normalX = panel.x, normalY = panel.y)
        openWindows[id] = state

        panel.findViewById<View>(R.id.btnClose).setOnClickListener { closeWindow(id) }
        panel.findViewById<View>(R.id.btnMinimize).setOnClickListener { minimizeWindow(id) }
        panel.findViewById<View>(R.id.btnMaximize).setOnClickListener { toggleMaximize(id) }
        setupPanelDrag(panel, state)

        bringFront(id)
        refreshTaskbar()
    }

    @SuppressLint("ClickableViewAccessibility")
    private fun setupPanelDrag(panel: View, state: WindowState) {
        val bar = panel.findViewById<View>(R.id.panelTitleBar)
        var dx = 0f; var dy = 0f; var dragging = false
        bar.setOnTouchListener { _, e ->
            when (e.actionMasked) {
                MotionEvent.ACTION_DOWN -> {
                    dx = e.rawX - panel.x; dy = e.rawY - panel.y; dragging = true
                    bringFront(state.id)
                    true
                }
                MotionEvent.ACTION_MOVE -> {
                    if (!dragging || state.maximized) return@setOnTouchListener true
                    panel.x = e.rawX - dx
                    panel.y = (e.rawY - dy).coerceAtLeast(0f)
                    true
                }
                MotionEvent.ACTION_UP -> {
                    dragging = false
                    if (!state.maximized) snapPanelToEdge(panel, state)
                    state.normalX = panel.x; state.normalY = panel.y
                    true
                }
                else -> false
            }
        }
        // Corner resize + two-finger pinch (like trackpad / mouse wheel zoom on desktop)
        var startDist = 0f
        var startW = 0
        var startH = 0
        var cornerResize = false
        var cStartX = 0f; var cStartY = 0f; var cW = 0; var cH = 0
        panel.setOnTouchListener { v, e ->
            when (e.actionMasked) {
                MotionEvent.ACTION_DOWN -> {
                    val edge = 56 * resources.displayMetrics.density
                    if (e.x > v.width - edge && e.y > v.height - edge && !state.maximized) {
                        cornerResize = true
                        cStartX = e.rawX; cStartY = e.rawY
                        cW = v.width; cH = v.height
                        bringFront(state.id)
                        return@setOnTouchListener true
                    }
                    false
                }
                MotionEvent.ACTION_POINTER_DOWN -> {
                    if (e.pointerCount >= 2 && !state.maximized) {
                        val x0 = e.getX(0); val y0 = e.getY(0)
                        val x1 = e.getX(1); val y1 = e.getY(1)
                        startDist = kotlin.math.hypot((x1 - x0).toDouble(), (y1 - y0).toDouble()).toFloat()
                        startW = v.width; startH = v.height
                        bringFront(state.id)
                    }
                    true
                }
                MotionEvent.ACTION_MOVE -> {
                    if (cornerResize) {
                        val minW = (200 * resources.displayMetrics.density).toInt()
                        val minH = (140 * resources.displayMetrics.density).toInt()
                        val nw = (cW + (e.rawX - cStartX)).toInt().coerceIn(minW, windowsHost.width)
                        val nh = (cH + (e.rawY - cStartY)).toInt().coerceIn(minH, windowsHost.height)
                        v.layoutParams = FrameLayout.LayoutParams(nw, nh)
                        v.requestLayout()
                        state.normalW = nw; state.normalH = nh
                        return@setOnTouchListener true
                    }
                    if (e.pointerCount >= 2 && startDist > 0f && !state.maximized) {
                        val x0 = e.getX(0); val y0 = e.getY(0)
                        val x1 = e.getX(1); val y1 = e.getY(1)
                        val dist = kotlin.math.hypot((x1 - x0).toDouble(), (y1 - y0).toDouble()).toFloat()
                        val scale = (dist / startDist).coerceIn(0.5f, 2.5f)
                        val minW = (200 * resources.displayMetrics.density).toInt()
                        val minH = (160 * resources.displayMetrics.density).toInt()
                        val nw = (startW * scale).toInt().coerceIn(minW, windowsHost.width)
                        val nh = (startH * scale).toInt().coerceIn(minH, windowsHost.height)
                        v.layoutParams = FrameLayout.LayoutParams(nw, nh)
                        v.x = v.x.coerceIn(0f, (windowsHost.width - nw).toFloat().coerceAtLeast(0f))
                        v.y = v.y.coerceIn(0f, (windowsHost.height - nh).toFloat().coerceAtLeast(0f))
                        v.requestLayout()
                        state.normalW = nw; state.normalH = nh
                        state.normalX = v.x; state.normalY = v.y
                    }
                    true
                }
                MotionEvent.ACTION_POINTER_UP, MotionEvent.ACTION_UP -> {
                    startDist = 0f
                    true
                }
                else -> false
            }
        }
    }


    private fun snapPanelToEdge(panel: View, state: WindowState) {
        val hostW = windowsHost.width.toFloat().coerceAtLeast(1f)
        val hostH = windowsHost.height.toFloat().coerceAtLeast(1f)
        val threshold = 56 * resources.displayMetrics.density
        when {
            panel.x < threshold -> {
                panel.x = 0f; panel.y = 0f
                panel.layoutParams = FrameLayout.LayoutParams((hostW / 2).toInt(), hostH.toInt())
                state.normalW = (hostW / 2).toInt(); state.normalH = hostH.toInt()
            }
            panel.x + panel.width > hostW - threshold -> {
                panel.x = hostW / 2; panel.y = 0f
                panel.layoutParams = FrameLayout.LayoutParams((hostW / 2).toInt(), hostH.toInt())
                state.normalW = (hostW / 2).toInt(); state.normalH = hostH.toInt()
            }
            panel.y < threshold -> {
                panel.x = 0f; panel.y = 0f
                panel.layoutParams = FrameLayout.LayoutParams(hostW.toInt(), (hostH / 2).toInt())
                state.normalW = hostW.toInt(); state.normalH = (hostH / 2).toInt()
            }
            panel.y + panel.height > hostH - threshold -> {
                panel.x = 0f; panel.y = hostH / 2
                panel.layoutParams = FrameLayout.LayoutParams(hostW.toInt(), (hostH / 2).toInt())
                state.normalW = hostW.toInt(); state.normalH = (hostH / 2).toInt()
            }
        }
        panel.requestLayout()
        state.normalX = panel.x; state.normalY = panel.y
    }

    private fun bringFront(id: String) {
        val w = openWindows[id] ?: return
        zCounter++
        w.view.elevation = zCounter.toFloat()
        w.view.isVisible = true
        w.minimized = false
        refreshTaskbar()
    }

    private fun closeWindow(id: String) {
        openWindows.remove(id)?.view?.let { windowsHost.removeView(it) }
        refreshTaskbar()
    }

    private fun minimizeWindow(id: String) {
        val w = openWindows[id] ?: return
        w.minimized = true
        w.view.isVisible = false
        refreshTaskbar()
    }

    private fun restoreWindow(id: String) {
        val w = openWindows[id] ?: return
        w.minimized = false
        w.view.isVisible = true
        bringFront(id)
    }

    private fun toggleMaximize(id: String) {
        val w = openWindows[id] ?: return
        val panel = w.view
        if (w.maximized) {
            w.maximized = false
            panel.x = w.normalX
            panel.y = w.normalY
            panel.layoutParams = FrameLayout.LayoutParams(w.normalW, w.normalH)
        } else {
            w.normalX = panel.x; w.normalY = panel.y
            w.normalW = panel.width; w.normalH = panel.height
            w.maximized = true
            panel.x = 0f; panel.y = 0f
            panel.layoutParams = FrameLayout.LayoutParams(
                windowsHost.width,
                windowsHost.height
            )
        }
        panel.requestLayout()
        bringFront(id)
    }

    private fun refreshTaskbar() {
        taskbarApps.removeAllViews()
        val d = resources.displayMetrics.density
        val dockSize = (36 * d).toInt()
        fun addDockIconOnly(content: (LinearLayout) -> Unit, onClick: () -> Unit) {
            val wrap = LinearLayout(this).apply {
                orientation = LinearLayout.VERTICAL
                gravity = android.view.Gravity.CENTER
                setPadding((6 * d).toInt(), (4 * d).toInt(), (6 * d).toInt(), (4 * d).toInt())
                setOnClickListener { onClick() }
                setBackgroundResource(R.drawable.bg_dock_item)
                val lp = LinearLayout.LayoutParams(dockSize + (12 * d).toInt(), LinearLayout.LayoutParams.MATCH_PARENT)
                lp.marginEnd = (5 * d).toInt()
                layoutParams = lp
            }
            content(wrap)
            taskbarApps.addView(wrap)
        }
        addDockIconOnly({ w ->
            w.addView(ImageView(this).apply {
                setImageResource(R.drawable.ast_icon)
                layoutParams = LinearLayout.LayoutParams(dockSize, dockSize)
                scaleType = ImageView.ScaleType.FIT_CENTER
            })
        }) { openInternal("ast", "AST Terminal") }
        addDockIconOnly({ w ->
            w.addView(ImageView(this).apply {
                setImageResource(R.drawable.ic_folder)
                layoutParams = LinearLayout.LayoutParams(dockSize, dockSize)
                scaleType = ImageView.ScaleType.FIT_CENTER
            })
        }) { openInternal("files", "Dosya Gezgini") }
        addDockIconOnly({ w ->
            w.addView(ImageView(this).apply {
                setImageResource(R.drawable.ic_store)
                layoutParams = LinearLayout.LayoutParams(dockSize, dockSize)
                scaleType = ImageView.ScaleType.FIT_CENTER
            })
        }) { openPlayStore() }
        openWindows.values.forEach { win ->
            addDockIconOnly({ w ->
                w.addView(TextView(this).apply {
                    text = if (win.minimized) "▢" else "▣"
                    textSize = 18f
                    gravity = android.view.Gravity.CENTER
                    setTextColor(0xFFB8FF1A.toInt())
                    layoutParams = LinearLayout.LayoutParams(dockSize, dockSize)
                })
            }) {
                if (win.minimized) restoreWindow(win.id) else bringFront(win.id)
            }
        }
        addDockIconOnly({ w ->
            w.addView(ImageView(this).apply {
                setImageResource(R.drawable.ic_apps)
                layoutParams = LinearLayout.LayoutParams(dockSize, dockSize)
                scaleType = ImageView.ScaleType.FIT_CENTER
            })
        }) { openAppDrawer() }
    }

    private var drawerOverlay: View? = null

    private fun openAppDrawer() {
        hideStart()
        if (drawerOverlay != null) {
            closeAppDrawer()
            return
        }
        val overlay = LayoutInflater.from(this).inflate(R.layout.panel_app_drawer, windowsHost, false)
        windowsHost.addView(
            overlay,
            FrameLayout.LayoutParams(
                FrameLayout.LayoutParams.MATCH_PARENT,
                FrameLayout.LayoutParams.MATCH_PARENT
            )
        )
        drawerOverlay = overlay
        overlay.elevation = 50f
        overlay.findViewById<View>(R.id.btnCloseDrawer).setOnClickListener { closeAppDrawer() }

        val grid = overlay.findViewById<RecyclerView>(R.id.drawerGrid)
        val span = if (resources.configuration.orientation ==
            android.content.res.Configuration.ORIENTATION_LANDSCAPE) 8 else 5
        grid.layoutManager = androidx.recyclerview.widget.GridLayoutManager(this, span)
        // Smooth scroll / no nested touch fights
        grid.itemAnimator = null
        grid.setHasFixedSize(true)

        val apps = allApps
        grid.adapter = object : RecyclerView.Adapter<RecyclerView.ViewHolder>() {
            override fun getItemCount() = apps.size
            override fun onCreateViewHolder(parent: android.view.ViewGroup, viewType: Int): RecyclerView.ViewHolder {
                val v = LayoutInflater.from(parent.context)
                    .inflate(R.layout.item_drawer_app, parent, false)
                return object : RecyclerView.ViewHolder(v) {}
            }
            override fun onBindViewHolder(holder: RecyclerView.ViewHolder, position: Int) {
                val app = apps[position]
                val v = holder.itemView
                v.findViewById<ImageView>(R.id.drawerIcon).setImageDrawable(app.icon)
                v.findViewById<TextView>(R.id.drawerLabel).text = app.label
                v.setOnClickListener {
                    if (!drawerDragging) openApp(app)
                }
                // Long-press: pin OR start drag — no conflicting OnLongClickListener + OnTouch
                bindDrawerDrag(v, app, grid)
            }
        }
    }

    private var drawerDragging = false

    private fun closeAppDrawer() {
        drawerOverlay?.let { windowsHost.removeView(it) }
        drawerOverlay = null
    }

    @SuppressLint("ClickableViewAccessibility")
    private fun bindDrawerDrag(itemView: View, app: AppInfo, grid: RecyclerView) {
        val touchSlop = ViewConfiguration.get(this).scaledTouchSlop
        val longMs = ViewConfiguration.getLongPressTimeout().toLong()
        var downRawX = 0f
        var downRawY = 0f
        var dragging = false
        var ghost: ImageView? = null
        var longRunnable: Runnable? = null

        itemView.setOnTouchListener { v, e ->
            when (e.actionMasked) {
                MotionEvent.ACTION_DOWN -> {
                    downRawX = e.rawX
                    downRawY = e.rawY
                    dragging = false
                    drawerDragging = false
                    longRunnable = Runnable {
                        // Enter drag mode
                        dragging = true
                        drawerDragging = true
                        grid.requestDisallowInterceptTouchEvent(true)
                        vibrate()
                        val loc = IntArray(2)
                        windowsHost.getLocationOnScreen(loc)
                        ghost = ImageView(this@MainActivity).apply {
                            setImageDrawable(app.icon)
                            alpha = 0.9f
                            elevation = 80f
                            layoutParams = FrameLayout.LayoutParams(96, 96)
                            x = e.rawX - loc[0] - 48
                            y = e.rawY - loc[1] - 48
                        }
                        windowsHost.addView(ghost)
                    }
                    v.postDelayed(longRunnable!!, longMs)
                    // false → RecyclerView can scroll; long-press still scheduled
                    false
                }
                MotionEvent.ACTION_MOVE -> {
                    val dx = e.rawX - downRawX
                    val dy = e.rawY - downRawY
                    if (!dragging && (kotlin.math.abs(dx) > touchSlop || kotlin.math.abs(dy) > touchSlop)) {
                        longRunnable?.let { v.removeCallbacks(it) }
                        longRunnable = null
                    }
                    if (dragging) {
                        val loc = IntArray(2)
                        windowsHost.getLocationOnScreen(loc)
                        ghost?.let { g ->
                            g.x = e.rawX - loc[0] - 48
                            g.y = e.rawY - loc[1] - 48
                        }
                        return@setOnTouchListener true
                    }
                    false
                }
                MotionEvent.ACTION_UP, MotionEvent.ACTION_CANCEL -> {
                    longRunnable?.let { v.removeCallbacks(it) }
                    longRunnable = null
                    grid.requestDisallowInterceptTouchEvent(false)
                    if (dragging) {
                        val pa = "${app.packageName}/${app.activityName}"
                        Prefs.pinApp(this@MainActivity, pa)
                        val key = "app:$pa"
                        val loc = IntArray(2)
                        desktop.getLocationOnScreen(loc)
                        val localX = e.rawX - loc[0]
                        val localY = e.rawY - loc[1]
                        val gm = DesktopManager.grid()
                        gm.snapToNearest(
                            key, localX, localY,
                            cellW.coerceAtLeast(1f), cellH.coerceAtLeast(1f),
                            gridOriginX, gridOriginY
                        )
                        DesktopManager.saveLayout(this@MainActivity)
                        ghost?.let { windowsHost.removeView(it) }
                        ghost = null
                        dragging = false
                        drawerDragging = false
                        closeAppDrawer()
                        // Defer layout to next frame — avoids freeze on UI thread burst
                        desktop.post {
                            layoutDesktop()
                            Toast.makeText(this@MainActivity, "${app.label} eklendi", Toast.LENGTH_SHORT).show()
                        }
                        return@setOnTouchListener true
                    }
                    drawerDragging = false
                    false
                }
                else -> false
            }
        }
    }



    private fun fillThisPc(root: View) {
        val user = Prefs.getUser(this)
        root.findViewById<TextView>(R.id.pcSub).text = "AstraSage OS · $user"
        root.findViewById<TextView>(R.id.rowUser).text = "👤 Kullanıcı: $user"
        root.findViewById<TextView>(R.id.rowDevice).text =
            "📱 Cihaz: ${Build.MANUFACTURER} ${Build.MODEL}"
        root.findViewById<TextView>(R.id.rowAndroid).text =
            "🤖 Android ${Build.VERSION.RELEASE} (API ${Build.VERSION.SDK_INT})"
        var disk = "—"
        try {
            val st = StatFs(Environment.getExternalStorageDirectory().absolutePath)
            disk = "${RealFs.formatSize(st.availableBytes)} boş / ${RealFs.formatSize(st.totalBytes)}"
        } catch (_: Exception) {
        }
        root.findViewById<TextView>(R.id.rowDisk).text = "💾 Depolama: $disk"
        root.findViewById<TextView>(R.id.rowApps).text = "📦 Uygulama: ${allApps.size}"
        root.findViewById<TextView>(R.id.rowStoragePath).text =
            "📂 Kök: ${RealFs.root().absolutePath}"
        root.findViewById<View>(R.id.btnOpenFiles).setOnClickListener {
            startActivity(Intent(this, FilesActivity::class.java))
        }
    }

    private fun fillTrash(root: View) {
        val list = root.findViewById<RecyclerView>(R.id.trashList)
        list.layoutManager = LinearLayoutManager(this)
        val items = Prefs.getRecycle(this).toList()
        list.adapter = object : RecyclerView.Adapter<RecyclerView.ViewHolder>() {
            override fun getItemCount() = items.size
            override fun onCreateViewHolder(parent: ViewGroup, viewType: Int): RecyclerView.ViewHolder {
                val tv = TextView(parent.context).apply {
                    setPadding(16, 20, 16, 20)
                    setTextColor(ContextCompat.getColor(context, R.color.text))
                    textSize = 13f
                    setBackgroundResource(R.drawable.bg_choice)
                    val lp = RecyclerView.LayoutParams(
                        ViewGroup.LayoutParams.MATCH_PARENT,
                        ViewGroup.LayoutParams.WRAP_CONTENT
                    )
                    (lp as ViewGroup.MarginLayoutParams).bottomMargin = 8
                    layoutParams = lp
                }
                return object : RecyclerView.ViewHolder(tv) {}
            }
            override fun onBindViewHolder(holder: RecyclerView.ViewHolder, position: Int) {
                val key = items[position]
                val tv = holder.itemView as TextView
                tv.text = "♻️ ${key.removePrefix("app:").removePrefix("file:").removePrefix("sys:")}  (geri al)"
                tv.setOnClickListener {
                    Prefs.restoreFromRecycle(this@MainActivity, key)
                    if (key.startsWith("app:")) {
                        val h = Prefs.getHiddenApps(this@MainActivity).toMutableSet()
                        h.remove(key.removePrefix("app:"))
                        Prefs.prefs(this@MainActivity).edit().putStringSet("hidden_apps", h).apply()
                    }
                    layoutDesktop()
                    fillTrash(root)
                    Toast.makeText(this@MainActivity, "Geri yüklendi", Toast.LENGTH_SHORT).show()
                }
            }
        }
        root.findViewById<View>(R.id.btnEmptyTrash).setOnClickListener {
            Prefs.emptyRecycle(this)
            // keep hidden apps hidden after empty? yes - empty only clears recycle list display permanently
            layoutDesktop()
            fillTrash(root)
            Toast.makeText(this, "Çöp boşaltıldı", Toast.LENGTH_SHORT).show()
        }
    }

    private fun fillCalendar(root: View) {
        val lang = if (Prefs.getLang(this) == "en") Locale.US else Locale("tr", "TR")
        val clock = root.findViewById<TextView>(R.id.calClock)
        val date = root.findViewById<TextView>(R.id.calDate)
        val grid = root.findViewById<TextView>(R.id.calGrid)
        fun refresh() {
            val now = Date()
            clock.text = SimpleDateFormat("HH:mm:ss", lang).format(now)
            date.text = SimpleDateFormat("EEEE, d MMMM yyyy", lang).format(now)
        }
        refresh()
        root.post(object : Runnable {
            override fun run() {
                if (root.isAttachedToWindow) {
                    refresh()
                    root.postDelayed(this, 1000)
                }
            }
        })
        val cal = Calendar.getInstance()
        val year = cal.get(Calendar.YEAR)
        val month = cal.get(Calendar.MONTH)
        val today = cal.get(Calendar.DAY_OF_MONTH)
        cal.set(Calendar.DAY_OF_MONTH, 1)
        val firstDow = (cal.get(Calendar.DAY_OF_WEEK) + 5) % 7 // Mon=0
        val days = cal.getActualMaximum(Calendar.DAY_OF_MONTH)
        val sb = StringBuilder("Pt Sa Ça Pe Cu Ct Pa\n")
        repeat(firstDow) { sb.append("   ") }
        for (d in 1..days) {
            if (d == today) sb.append(String.format("[%2d]", d))
            else sb.append(String.format(" %2d ", d))
            if ((firstDow + d) % 7 == 0) sb.append("\n")
        }
        grid.text = sb.toString()
    }

    private fun wireEmbeddedTerminal(root: View) {
        val output = root.findViewById<TextView>(R.id.output)
        val input = root.findViewById<android.widget.EditText>(R.id.input)
        val scroll = root.findViewById<android.widget.ScrollView>(R.id.scroll)
        root.findViewById<View>(R.id.btnClose)?.isVisible = false
        val engine = AstEngine(this) { s ->
            when {
                s.startsWith("\u0000CLEAR") -> output.text = ""
                s.startsWith("\u0000EXIT") -> closeWindow("ast")
                else -> {
                    output.append(s)
                    scroll.post { scroll.fullScroll(View.FOCUS_DOWN) }
                }
            }
        }
        output.text = ""
        engine.banner()
        output.append(engine.prompt())
        input.setOnEditorActionListener { _, _, _ ->
            val cmd = input.text?.toString().orEmpty()
            input.setText("")
            output.append(cmd + "\n")
            engine.run(cmd)
            if (!cmd.trim().equals("exit", true)) {
                output.append(engine.prompt())
            }
            true
        }
    }


    /** Default desktop apps — screen order (gallery, chrome, files, settings, phone, clock) */
    private val ESSENTIAL_PACKAGES = listOf(
        "com.google.android.apps.photos", "com.sec.android.gallery3d", "com.samsung.android.gallery3d",
        "com.android.chrome", "com.chrome.beta", "com.sec.android.app.sbrowser",
        "com.google.android.apps.nbu.files", "com.sec.android.app.myfiles",
        "com.android.documentsui", "com.google.android.documentsui",
        "com.android.settings",
        "com.google.android.dialer", "com.samsung.android.dialer", "com.android.dialer",
        "com.google.android.deskclock", "com.sec.android.app.clockpackage",
        "com.samsung.android.app.clockpackage"
    )

    private fun isStorePackage(pkg: String) =
        pkg == "com.android.vending" || pkg.contains("appmarket") || pkg.contains("store") && pkg.contains("huawei")

    private fun essentialApps(all: List<AppInfo>): List<AppInfo> {
        val out = mutableListOf<AppInfo>()
        val seen = mutableSetOf<String>()
        for (pkg in ESSENTIAL_PACKAGES) {
            val match = all.firstOrNull { it.packageName == pkg } ?: continue
            if (match.packageName in seen) continue
            seen.add(match.packageName)
            out.add(match)
        }
        // fallback chrome if missing
        if (out.none { it.packageName.contains("chrome") }) {
            all.firstOrNull { it.label.contains("Chrome", true) }?.let {
                if (it.packageName !in seen) out.add(1.coerceAtMost(out.size), it)
            }
        }
        return out
    }

    private fun openPlayStore() {
        hideStart()
        // Open the device's installed store app (Play Store / Galaxy Store fallback)
        val storePkgs = listOf(
            "com.android.vending",
            "com.sec.android.app.samsungapps",
            "com.huawei.appmarket",
            "com.xiaomi.mipicks"
        )
        for (pkg in storePkgs) {
            try {
                val launch = packageManager.getLaunchIntentForPackage(pkg)
                if (launch != null) {
                    launch.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)
                    startActivity(launch)
                    return
                }
            } catch (_: Exception) {}
        }
        try {
            startActivity(Intent(Intent.ACTION_VIEW, android.net.Uri.parse("https://play.google.com/store")).apply {
                addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)
            })
        } catch (_: Exception) {
            Toast.makeText(this, "Mağaza açılamadı", Toast.LENGTH_SHORT).show()
        }
    }


    private fun fillOptions(root: View) {
        val iconSeek = root.findViewById<android.widget.SeekBar>(R.id.optIconSeek)
        val fontSeek = root.findViewById<android.widget.SeekBar>(R.id.optFontSeek)
        val iconVal = root.findViewById<TextView>(R.id.optIconValue)
        val fontVal = root.findViewById<TextView>(R.id.optFontValue)
        val namesOn = root.findViewById<TextView>(R.id.optNamesOn)
        val namesOff = root.findViewById<TextView>(R.id.optNamesOff)

        // map scale 0.65..1.6 -> progress 0..100
        fun scaleToProg(s: Float) = (((s - 0.65f) / (1.6f - 0.65f)) * 100).toInt().coerceIn(0, 100)
        fun progToScale(p: Int) = 0.65f + (p / 100f) * (1.6f - 0.65f)

        iconSeek.progress = scaleToProg(Prefs.getIconScale(this))
        fontSeek.progress = scaleToProg(Prefs.getFontScale(this).coerceIn(0.65f, 1.6f))
        iconVal.text = "${(Prefs.getIconScale(this) * 100).toInt()}%"
        fontVal.text = "${(Prefs.getFontScale(this) * 100).toInt()}%"

        fun refreshNamesBtns() {
            val on = Prefs.showIconNames(this)
            namesOn.setBackgroundResource(if (on) R.drawable.bg_choice_sel else R.drawable.bg_choice)
            namesOff.setBackgroundResource(if (!on) R.drawable.bg_choice_sel else R.drawable.bg_choice)
        }
        refreshNamesBtns()

        iconSeek.setOnSeekBarChangeListener(object : android.widget.SeekBar.OnSeekBarChangeListener {
            override fun onProgressChanged(seekBar: android.widget.SeekBar?, progress: Int, fromUser: Boolean) {
                val s = progToScale(progress)
                iconVal.text = "${(s * 100).toInt()}%"
                if (fromUser) {
                    Prefs.setIconScale(this@MainActivity, s)
                    layoutDesktop()
                }
            }
            override fun onStartTrackingTouch(seekBar: android.widget.SeekBar?) {}
            override fun onStopTrackingTouch(seekBar: android.widget.SeekBar?) {}
        })
        fontSeek.setOnSeekBarChangeListener(object : android.widget.SeekBar.OnSeekBarChangeListener {
            override fun onProgressChanged(seekBar: android.widget.SeekBar?, progress: Int, fromUser: Boolean) {
                val s = progToScale(progress)
                fontVal.text = "${(s * 100).toInt()}%"
                if (fromUser) {
                    Prefs.setFontScale(this@MainActivity, s)
                    layoutDesktop()
                }
            }
            override fun onStartTrackingTouch(seekBar: android.widget.SeekBar?) {}
            override fun onStopTrackingTouch(seekBar: android.widget.SeekBar?) {}
        })
        namesOn.setOnClickListener {
            Prefs.setShowIconNames(this, true); refreshNamesBtns(); layoutDesktop()
        }
        namesOff.setOnClickListener {
            Prefs.setShowIconNames(this, false); refreshNamesBtns(); layoutDesktop()
        }
        root.findViewById<View>(R.id.optWallPick).setOnClickListener {
            val i = Intent(Intent.ACTION_GET_CONTENT).setType("image/*")
            startActivityForResult(Intent.createChooser(i, "Arka plan"), 2001)
        }
        root.findViewById<View>(R.id.optWallDefault).setOnClickListener {
            getSharedPreferences("astrasage_os", MODE_PRIVATE).edit().remove("custom_wall").apply()
            wallpaper.setImageResource(R.drawable.wallpaper)
            Toast.makeText(this, "Varsayılan arka plan", Toast.LENGTH_SHORT).show()
        }
        root.findViewById<View>(R.id.optAppsPick)?.setOnClickListener { openDesktopAppsPicker() }
        root.findViewById<View>(R.id.optApply).setOnClickListener {
            layoutDesktop()
            Toast.makeText(this, "Uygulandı", Toast.LENGTH_SHORT).show()
            closeWindow("options")
        }
    }


    private fun applyDesktopEnvironment(de: DesktopEnvironment) {
        val d = resources.displayMetrics.density
        val barH = (de.taskbarHeightDp * d).toInt()
        findViewById<View>(R.id.bottomBar)?.layoutParams?.let {
            it.height = barH
            findViewById<View>(R.id.bottomBar)?.layoutParams = it
        }
        when (de.taskbarStyle) {
            TaskbarStyle.CLASSIC_BAR -> findViewById<View>(R.id.bottomBar)?.setBackgroundColor(0xE0222222.toInt())
            TaskbarStyle.MINIMAL_STRIP -> findViewById<View>(R.id.bottomBar)?.setBackgroundColor(0xCC111111.toInt())
            TaskbarStyle.FLOW_BOTTOM -> findViewById<View>(R.id.bottomBar)?.setBackgroundColor(0xF01A1A1A.toInt())
            else -> findViewById<View>(R.id.bottomBar)?.setBackgroundResource(R.drawable.bg_taskbar)
        }
    }

    private fun openDeSelector() {
        hideStart()
        openInternal("deselector", "Desktop Environments")
    }

    private fun fillDeSelector(root: View) {
        val list = root.findViewById<LinearLayout>(R.id.deList)
        list.removeAllViews()
        val active = DesktopManager.current()
        DesktopManager.all().forEach { de ->
            val row = LinearLayout(this).apply {
                orientation = LinearLayout.VERTICAL
                setPadding(16, 14, 16, 14)
                setBackgroundResource(
                    if (de.id == active.id) R.drawable.bg_choice_sel else R.drawable.bg_choice
                )
                val lp = LinearLayout.LayoutParams(
                    LinearLayout.LayoutParams.MATCH_PARENT,
                    LinearLayout.LayoutParams.WRAP_CONTENT
                )
                lp.bottomMargin = 10
                layoutParams = lp
                setOnClickListener {
                    DesktopManager.switchTo(this@MainActivity, de.id)
                    closeWindow("deselector")
                    Toast.makeText(this@MainActivity, de.displayName + " yüklendi", Toast.LENGTH_SHORT).show()
                }
            }
            val title = if (de.id == active.id) "${de.emoji}  ${de.displayName}  ✓ ACTIVE" else "${de.emoji}  ${de.displayName}"
            row.addView(TextView(this).apply {
                text = title
                setTextColor(if (de.id == active.id) de.accentArgb else 0xFFEEEEEE.toInt())
                textSize = 15f
                setTypeface(null, android.graphics.Typeface.BOLD)
            })
            row.addView(TextView(this).apply {
                text = de.description
                setTextColor(0x99FFFFFF.toInt())
                textSize = 12f
                setPadding(0, 4, 0, 0)
            })
            row.addView(TextView(this).apply {
                text = "Grid ${de.gridCols}×${de.gridRows}"
                setTextColor(0x66FFFFFF.toInt())
                textSize = 11f
            })
            list.addView(row)
        }
    }


    private fun toggleStartMenu() {
        if (startMenu.isVisible) hideStart() else {
            populateStartMenu()
            startMenu.isVisible = true
        }
    }

    private fun setupStartMenu() {
        findViewById<View>(R.id.btnPowerOff)?.setOnClickListener {
            hideStart()
            android.app.AlertDialog.Builder(this)
                .setTitle("Kapat")
                .setMessage("AstraSage OS kapatılsın mı?")
                .setPositiveButton("Kapat") { _, _ -> finishAffinity() }
                .setNegativeButton("İptal", null)
                .show()
        }
        findViewById<View>(R.id.btnPowerRestart)?.setOnClickListener {
            hideStart()
            Toast.makeText(this, "Yeniden başlatılıyor…", Toast.LENGTH_SHORT).show()
            val i = Intent(this, SetupActivity::class.java)
            i.addFlags(Intent.FLAG_ACTIVITY_CLEAR_TOP or Intent.FLAG_ACTIVITY_NEW_TASK)
            startActivity(i)
            finish()
        }
        findViewById<View>(R.id.btnPowerSleep)?.setOnClickListener {
            hideStart()
            // Sleep = black overlay until tap
            val sleep = View(this).apply {
                setBackgroundColor(0xFF000000.toInt())
                isClickable = true
                setOnClickListener { windowsHost.removeView(this) }
            }
            windowsHost.addView(sleep, FrameLayout.LayoutParams(
                FrameLayout.LayoutParams.MATCH_PARENT,
                FrameLayout.LayoutParams.MATCH_PARENT
            ))
            Toast.makeText(this, "Uyku modu — dokunarak uyan", Toast.LENGTH_SHORT).show()
        }
    }

    private fun populateStartMenu() {
        val panel = findViewById<View>(R.id.startMenuPanel)
        panel?.setOnClickListener { /* consume */ }
        // Drag start menu panel
        panel?.let { enableStartMenuDrag(it) }
        findViewById<TextView>(R.id.startUserLabel)?.text = Prefs.getUser(this).ifBlank { "Kullanıcı" }
        val grid = findViewById<android.widget.GridLayout>(R.id.startPinnedGrid) ?: return
        grid.removeAllViews()
        data class Tile(val label: String, val icon: Int, val action: () -> Unit)
        val tiles = listOf(
            Tile("Not Defteri", R.drawable.ic_docs) { hideStart(); openInternal("notepad", "Not Defteri") },
            Tile("Paint", R.drawable.ic_options) { hideStart(); openInternal("paint", "Paint") },
            Tile("Hava", R.drawable.ic_network) { hideStart(); openInternal("weather", "Hava Durumu") },
            Tile("Müzik", R.drawable.ic_folder) { hideStart(); openInternal("music", "Müzik") },
            Tile("Chrome", R.drawable.ic_network) { hideStart(); openChrome() },
            Tile("AST", R.drawable.ast_icon) { hideStart(); openInternal("ast", "AST Terminal") },
            Tile("Disk", R.drawable.ic_thispc) { hideStart(); openInternal("disk", "Disk") },
            Tile("Dosyalar", R.drawable.ic_folder) { hideStart(); openInternal("files", "Dosya Gezgini") }
        )
        tiles.forEach { tile ->
            val v = LayoutInflater.from(this).inflate(R.layout.item_start_tile, grid, false)
            v.findViewById<android.widget.ImageView>(R.id.tileIcon).setImageResource(tile.icon)
            v.findViewById<TextView>(R.id.tileLabel).text = tile.label
            v.setOnClickListener { tile.action() }
            grid.addView(v)
        }
        val rec = findViewById<LinearLayout>(R.id.startRecommended) ?: return
        rec.removeAllViews()
        listOf(
            "Bu Bilgisayar" to { hideStart(); openInternal("thispc", "Bu Bilgisayar") },
            "Seçenekler" to { hideStart(); openOptionsPanel() },
            "Desktop Ortamı" to { hideStart(); openDeSelector() },
            "Oturumu kapat" to {
                hideStart()
                startActivity(Intent(this, SetupActivity::class.java))
                finish()
            }
        ).forEach { (label, act) ->
            rec.addView(TextView(this).apply {
                text = label
                setTextColor(0xFFEEEEEE.toInt())
                textSize = 13f
                setPadding(12, 14, 12, 14)
                setOnClickListener { act() }
            })
        }
    }

    private fun openChrome() {
        val browsers = listOf("com.android.chrome", "com.chrome.beta", "com.sec.android.app.sbrowser")
        for (pkg in browsers) {
            val launch = packageManager.getLaunchIntentForPackage(pkg)
            if (launch != null) {
                launch.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)
                startActivity(launch)
                return
            }
        }
        try {
            startActivity(Intent(Intent.ACTION_VIEW, android.net.Uri.parse("https://www.google.com")))
        } catch (_: Exception) {
            Toast.makeText(this, "Tarayıcı yok", Toast.LENGTH_SHORT).show()
        }
    }


    private fun fillNotepad(root: View) {
        val editor = root.findViewById<android.widget.EditText>(R.id.noteEditor)
        val status = root.findViewById<TextView>(R.id.noteStatus)
        val file = File(RealFs.home(), "not.txt")
        if (file.exists()) {
            try { editor.setText(file.readText()) } catch (_: Exception) {}
        }
        root.findViewById<View>(R.id.noteNew).setOnClickListener {
            editor.setText(""); status.text = "Yeni not"
        }
        root.findViewById<View>(R.id.noteSave).setOnClickListener {
            try {
                file.writeText(editor.text?.toString().orEmpty())
                status.text = "Kaydedildi: ${file.name}"
                Toast.makeText(this, "Kaydedildi", Toast.LENGTH_SHORT).show()
            } catch (e: Exception) {
                status.text = "Hata: ${e.message}"
            }
        }
    }

    private fun fillPaint(root: View) {
        val canvas = root.findViewById<PaintCanvasView>(R.id.paintCanvas)
        val colors = root.findViewById<LinearLayout>(R.id.paintColors)
        val palette = listOf(
            0xFF000000.toInt(), 0xFFFFFFFF.toInt(), 0xFFFF0000.toInt(), 0xFF00AA00.toInt(),
            0xFF0066FF.toInt(), 0xFFFFFF00.toInt(), 0xFFFF00FF.toInt(), 0xFF00FFFF.toInt(),
            0xFFB8FF1A.toInt(), 0xFFFF9800.toInt(), 0xFF9C27B0.toInt(), 0xFF795548.toInt()
        )
        colors.removeAllViews()
        palette.forEach { c ->
            colors.addView(View(this).apply {
                val d = (28 * resources.displayMetrics.density).toInt()
                layoutParams = LinearLayout.LayoutParams(d, d).also { it.marginEnd = 6 }
                setBackgroundColor(c)
                setOnClickListener { canvas.color = c }
            })
        }
        fun sel(id: Int, tool: PaintCanvasView.Tool) {
            root.findViewById<View>(id).setOnClickListener { canvas.tool = tool }
        }
        sel(R.id.paintBrush, PaintCanvasView.Tool.BRUSH)
        sel(R.id.paintEraser, PaintCanvasView.Tool.ERASER)
        sel(R.id.paintLine, PaintCanvasView.Tool.LINE)
        sel(R.id.paintRect, PaintCanvasView.Tool.RECT)
        sel(R.id.paintCircle, PaintCanvasView.Tool.CIRCLE)
        sel(R.id.paintFill, PaintCanvasView.Tool.FILL)
        root.findViewById<View>(R.id.paintClear).setOnClickListener { canvas.clearCanvas() }
        root.findViewById<View>(R.id.paintUndo).setOnClickListener { canvas.undo() }
        root.findViewById<android.widget.SeekBar>(R.id.paintSize).setOnSeekBarChangeListener(
            object : android.widget.SeekBar.OnSeekBarChangeListener {
                override fun onProgressChanged(s: android.widget.SeekBar?, p: Int, f: Boolean) {
                    canvas.strokeWidth = (p + 2).toFloat()
                }
                override fun onStartTrackingTouch(s: android.widget.SeekBar?) {}
                override fun onStopTrackingTouch(s: android.widget.SeekBar?) {}
            }
        )
    }

    private fun fillWeather(root: View) {
        val city = root.findViewById<TextView>(R.id.weatherCity)
        val temp = root.findViewById<TextView>(R.id.weatherTemp)
        val desc = root.findViewById<TextView>(R.id.weatherDesc)
        val extra = root.findViewById<TextView>(R.id.weatherExtra)
        fun load() {
            city.text = "Yerel (cihaz)"
            desc.text = "Örnek veri — ağ API opsiyonel"
            // Lightweight open-meteo style without key would need network; show device-based mock + tip
            val hour = java.util.Calendar.getInstance().get(java.util.Calendar.HOUR_OF_DAY)
            val t = when (hour) {
                in 6..11 -> 18
                in 12..17 -> 24
                in 18..21 -> 20
                else -> 14
            }
            temp.text = "${t}°"
            extra.text = "Durum: Açık / Parçalı\nNem: %55\nRüzgar: 12 km/s\n\nİpucu: Gerçek konum için cihaz konum izni + Open-Meteo eklenebilir."
        }
        load()
        root.findViewById<View>(R.id.weatherRefresh).setOnClickListener { load() }
    }

    private var musicPlayer: android.media.MediaPlayer? = null
    private var musicTracks: List<File> = emptyList()
    private var musicIndex = 0

    private fun fillMusic(root: View) {
        val list = root.findViewById<RecyclerView>(R.id.musicList)
        val now = root.findViewById<TextView>(R.id.musicNow)
        list.layoutManager = LinearLayoutManager(this)
        musicTracks = scanMusic()
        fun playAt(i: Int) {
            if (musicTracks.isEmpty()) {
                now.text = "Müzik bulunamadı (Music/Download)"
                return
            }
            musicIndex = i.coerceIn(0, musicTracks.lastIndex)
            val f = musicTracks[musicIndex]
            try {
                musicPlayer?.release()
                musicPlayer = android.media.MediaPlayer().apply {
                    setDataSource(f.absolutePath)
                    prepare()
                    start()
                }
                now.text = "▶ ${f.name}"
            } catch (e: Exception) {
                now.text = "Hata: ${e.message}"
            }
        }
        list.adapter = object : RecyclerView.Adapter<RecyclerView.ViewHolder>() {
            override fun getItemCount() = musicTracks.size
            override fun onCreateViewHolder(parent: ViewGroup, viewType: Int): RecyclerView.ViewHolder {
                val tv = TextView(parent.context).apply {
                    setPadding(16, 18, 16, 18)
                    setTextColor(0xFFEEEEEE.toInt())
                    textSize = 13f
                    setBackgroundResource(R.drawable.bg_choice)
                    val lp = RecyclerView.LayoutParams(
                        ViewGroup.LayoutParams.MATCH_PARENT,
                        ViewGroup.LayoutParams.WRAP_CONTENT
                    )
                    (lp as ViewGroup.MarginLayoutParams).bottomMargin = 6
                    layoutParams = lp
                }
                return object : RecyclerView.ViewHolder(tv) {}
            }
            override fun onBindViewHolder(holder: RecyclerView.ViewHolder, position: Int) {
                val tv = holder.itemView as TextView
                tv.text = "🎵 ${musicTracks[position].name}"
                tv.setOnClickListener { playAt(position) }
            }
        }
        root.findViewById<View>(R.id.musicPlay).setOnClickListener {
            val mp = musicPlayer
            if (mp == null) playAt(musicIndex)
            else if (mp.isPlaying) {
                mp.pause(); now.text = "⏸ ${musicTracks.getOrNull(musicIndex)?.name}"
            } else {
                mp.start(); now.text = "▶ ${musicTracks.getOrNull(musicIndex)?.name}"
            }
        }
        root.findViewById<View>(R.id.musicNext).setOnClickListener {
            if (musicTracks.isNotEmpty()) playAt((musicIndex + 1) % musicTracks.size)
        }
        root.findViewById<View>(R.id.musicPrev).setOnClickListener {
            if (musicTracks.isNotEmpty()) playAt((musicIndex - 1 + musicTracks.size) % musicTracks.size)
        }
        if (musicTracks.isEmpty()) now.text = "Müzik yok — Music veya Download klasörüne bakın"
    }

    private fun scanMusic(): List<File> {
        val out = mutableListOf<File>()
        val roots = listOf(
            Environment.getExternalStoragePublicDirectory(Environment.DIRECTORY_MUSIC),
            Environment.getExternalStoragePublicDirectory(Environment.DIRECTORY_DOWNLOADS),
            File(Environment.getExternalStorageDirectory(), "Music"),
            File(Environment.getExternalStorageDirectory(), "Download")
        )
        val exts = setOf("mp3", "m4a", "wav", "ogg", "flac", "aac")
        fun walk(dir: File, depth: Int) {
            if (depth > 3 || !dir.isDirectory) return
            dir.listFiles()?.forEach { f ->
                if (f.isDirectory) walk(f, depth + 1)
                else if (f.extension.lowercase() in exts) out.add(f)
            }
        }
        roots.forEach { if (it != null && it.exists()) walk(it, 0) }
        return out.distinctBy { it.absolutePath }.sortedBy { it.name.lowercase() }.take(200)
    }

    private fun fillDisk(root: View) {
        val info = root.findViewById<TextView>(R.id.diskInfo)
        val bar = root.findViewById<android.widget.ProgressBar>(R.id.diskBar)
        try {
            val path = Environment.getExternalStorageDirectory().absolutePath
            val st = android.os.StatFs(path)
            val total = st.totalBytes
            val free = st.availableBytes
            val used = total - free
            val pct = if (total > 0) ((used * 100) / total).toInt() else 0
            info.text = """
                |Birim     : ${path}
                |Dosya sis.: FUSE / media
                |Kapasite  : ${RealFs.formatSize(total)}
                |Kullanılan: ${RealFs.formatSize(used)}  (%${pct})
                |Boş       : ${RealFs.formatSize(free)}
                |
                |Dahili depolama (gerçek cihaz istatistiği)
            """.trimMargin()
            bar.progress = pct
        } catch (e: Exception) {
            info.text = "Disk bilgisi okunamadı: ${e.message}"
        }
        root.findViewById<View>(R.id.diskOpenFiles).setOnClickListener {
            startActivity(Intent(this, FilesActivity::class.java))
        }
    }


    @SuppressLint("ClickableViewAccessibility")
    private fun enableStartMenuDrag(panel: View) {
        val handle = panel.findViewById<View>(R.id.startDragHandle) ?: panel
        if (handle.getTag(0x51A7) == true) return
        handle.setTag(0x51A7, true)
        var dX = 0f
        var dY = 0f
        handle.setOnTouchListener { _, e ->
            val parent = panel.parent as? View ?: return@setOnTouchListener false
            when (e.actionMasked) {
                MotionEvent.ACTION_DOWN -> {
                    dX = e.rawX - panel.x
                    dY = e.rawY - panel.y
                    true
                }
                MotionEvent.ACTION_MOVE -> {
                    val maxX = (parent.width - panel.width).toFloat().coerceAtLeast(0f)
                    val maxY = (parent.height - panel.height).toFloat().coerceAtLeast(0f)
                    panel.x = (e.rawX - dX).coerceIn(0f, maxX)
                    panel.y = (e.rawY - dY).coerceIn(0f, maxY)
                    true
                }
                MotionEvent.ACTION_UP, MotionEvent.ACTION_CANCEL -> true
                else -> false
            }
        }
    }

    private fun openApp(app: AppInfo) {
        hideStart()
        try {
            startActivity(Intent(Intent.ACTION_MAIN).apply {
                addCategory(Intent.CATEGORY_LAUNCHER)
                component = ComponentName(app.packageName, app.activityName)
                flags = Intent.FLAG_ACTIVITY_NEW_TASK
            })
        } catch (_: Exception) {
            packageManager.getLaunchIntentForPackage(app.packageName)?.let {
                it.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)
                startActivity(it)
            } ?: Toast.makeText(this, R.string.open_failed, Toast.LENGTH_SHORT).show()
        }
    }

    private fun loadPositions(): JSONObject = try {
        JSONObject(Prefs.getIconPositions(this))
    } catch (_: Exception) {
        JSONObject()
    }

    private fun savePos(key: String, x: Float, y: Float) {
        val obj = loadPositions()
        obj.put(key, JSONObject().apply { put("x", x.toInt()); put("y", y.toInt()) })
        Prefs.setIconPositions(this, obj.toString())
    }

    private fun vibrate() {
        try {
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
                getSystemService(VibratorManager::class.java)?.defaultVibrator?.vibrate(
                    VibrationEffect.createOneShot(16, VibrationEffect.DEFAULT_AMPLITUDE)
                )
            } else {
                @Suppress("DEPRECATION")
                (getSystemService(VIBRATOR_SERVICE) as Vibrator).vibrate(
                    VibrationEffect.createOneShot(16, VibrationEffect.DEFAULT_AMPLITUDE)
                )
            }
        } catch (_: Exception) {
        }
    }
}
