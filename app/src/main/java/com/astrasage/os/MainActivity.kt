package com.astrasage.os

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

    private var allApps: List<AppInfo> = emptyList()
    private val iconViews = mutableMapOf<String, View>()
    private val selectedKeys = mutableSetOf<String>()
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

        desktop = findViewById(R.id.desktop)
        windowsHost = findViewById(R.id.windowsHost)
        selection = findViewById(R.id.selectionOverlay)
        startMenu = findViewById(R.id.startMenu)
        taskbarApps = findViewById(R.id.taskbarApps)
        clockView = findViewById(R.id.clockView)
        wallpaper = findViewById(R.id.wallpaper)
        welcomeOverlay = findViewById(R.id.welcomeOverlay)

        applyCustomWallpaper()

        findViewById<View>(R.id.btnStart).setOnClickListener {
            startMenu.isVisible = !startMenu.isVisible
        }
        findViewById<View>(R.id.menuAst).setOnClickListener { hideStart(); openInternal("ast", "AST Terminal") }
        findViewById<View>(R.id.menuFiles).setOnClickListener { hideStart(); openInternal("files", "Dosya Gezgini") }
        findViewById<View>(R.id.menuThisPc).setOnClickListener { hideStart(); openInternal("thispc", "Bu Bilgisayar") }
        findViewById<View>(R.id.menuTrash).setOnClickListener { hideStart(); openInternal("trash", "Çöp Kutusu") }
        findViewById<View>(R.id.menuCalendar).setOnClickListener { hideStart(); openInternal("calendar", "Takvim / Saat") }
        findViewById<View>(R.id.menuLogout).setOnClickListener {
            hideStart()
            startActivity(Intent(this, SetupActivity::class.java))
            finish()
        }
        clockView.setOnClickListener { openInternal("calendar", "Takvim / Saat") }

        setupRubberBand()
        setupWelcome()

        allApps = AppRepository.loadLauncherApps(packageManager)
        layoutDesktop()

        if (!Prefs.isWelcomeDone(this)) {
            welcomeOverlay.isVisible = true
        }

        clockHandler.post(clockTick)
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

        val density = resources.displayMetrics.density
        val iconW = (76 * density).toInt()
        val iconH = (90 * density).toInt()
        val topPad = (24 * density).toInt()
        val leftPad = (8 * density).toInt()
        val gapX = (4 * density).toInt()
        val gapY = (6 * density).toInt()
        val cols = 4
        val positions = loadPositions()
        val recycle = Prefs.getRecycle(this)
        val hidden = Prefs.getHiddenApps(this)

        var index = 0

        // System icons first
        fun sys(key: String, label: String, emoji: String, open: () -> Unit) {
            if (recycle.contains(key)) return
            val view = inflateEmojiIcon(label, emoji)
            placeIcon(view, key, index, positions, leftPad, topPad, iconW, iconH, gapX, gapY, cols)
            bindIconTouch(view, key, onOpen = open, onDelete = {
                Prefs.moveToRecycle(this, key)
                layoutDesktop()
                Toast.makeText(this, "Çöp kutusuna taşındı (masaüstünden)", Toast.LENGTH_SHORT).show()
            })
            index++
        }

        sys("sys:thispc", "Bu Bilgisayar", "💻") { openInternal("thispc", "Bu Bilgisayar") }
        sys("sys:trash", "Çöp Kutusu", "🗑️") { openInternal("trash", "Çöp Kutusu") }
        sys("sys:files", "Dosyalar", "📁") { openInternal("files", "Dosya Gezgini") }
        sys("sys:ast", "AST", ">_") { openInternal("ast", "AST Terminal") }
        sys("sys:calendar", "Takvim", "📅") { openInternal("calendar", "Takvim / Saat") }

        // Apps (not hidden)
        allApps.forEach { app ->
            val pa = "${app.packageName}/${app.activityName}"
            if (hidden.contains(pa)) return@forEach
            val key = "app:$pa"
            if (recycle.contains(key)) return@forEach
            if (index >= 28) return@forEach
            val view = inflateAppIcon(app)
            placeIcon(view, key, index, positions, leftPad, topPad, iconW, iconH, gapX, gapY, cols)
            bindIconTouch(view, key, onOpen = { openApp(app) }, onDelete = {
                Prefs.hideApp(this, pa)
                layoutDesktop()
                Toast.makeText(this, "Masaüstünden kaldırıldı (uygulama silinmedi)", Toast.LENGTH_SHORT).show()
            })
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
    }

    private fun inflateAppIcon(app: AppInfo): View {
        val view = LayoutInflater.from(this).inflate(R.layout.item_desktop_icon, desktop, false)
        view.findViewById<ImageView>(R.id.appIcon).setImageDrawable(app.icon)
        view.findViewById<TextView>(R.id.appLabel).text = app.label
        return view
    }

    private fun inflateEmojiIcon(label: String, emoji: String): View {
        val view = LayoutInflater.from(this).inflate(R.layout.item_desktop_icon, desktop, false)
        val icon = view.findViewById<ImageView>(R.id.appIcon)
        icon.visibility = View.GONE
        val plate = view.findViewById<View>(R.id.iconBg).parent as FrameLayout
        plate.addView(TextView(this).apply {
            text = emoji
            textSize = 26f
            gravity = Gravity.CENTER
            layoutParams = FrameLayout.LayoutParams(
                FrameLayout.LayoutParams.MATCH_PARENT,
                FrameLayout.LayoutParams.MATCH_PARENT
            )
        })
        view.findViewById<TextView>(R.id.appLabel).text = label
        return view
    }

    private fun placeIcon(
        view: View, key: String, index: Int, positions: JSONObject,
        leftPad: Int, topPad: Int, iconW: Int, iconH: Int, gapX: Int, gapY: Int, cols: Int
    ) {
        val saved = positions.optJSONObject(key)
        val x: Int
        val y: Int
        if (saved != null) {
            x = saved.optInt("x", leftPad)
            y = saved.optInt("y", topPad)
        } else {
            val col = index % cols
            val row = index / cols
            x = leftPad + col * (iconW + gapX)
            y = topPad + row * (iconH + gapY)
        }
        view.x = x.toFloat()
        view.y = y.toFloat()
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
                        val ddx = nx - v.x; val ddy = ny - v.y
                        if (selectedKeys.contains(key) && selectedKeys.size > 1) {
                            selectedKeys.forEach { k ->
                                iconViews[k]?.let { iv ->
                                    iv.x = (iv.x + ddx).coerceIn(0f, maxX)
                                    iv.y = (iv.y + ddy).coerceIn(0f, maxY)
                                }
                            }
                        } else {
                            v.x = nx; v.y = ny
                        }
                    }
                    true
                }
                MotionEvent.ACTION_UP, MotionEvent.ACTION_CANCEL -> {
                    runnable?.let { v.removeCallbacks(it) }
                    if (dragging) {
                        v.animate().scaleX(1f).scaleY(1f).setDuration(80).start()
                        v.elevation = 0f
                        if (selectedKeys.contains(key) && selectedKeys.size > 1) {
                            selectedKeys.forEach { k -> iconViews[k]?.let { savePos(k, it.x, it.y) } }
                        } else savePos(key, v.x, v.y)
                        dragging = false
                        if (!moved && onDelete != null) {
                            // long press without move → delete to trash confirm
                            onDelete.invoke()
                        }
                    } else if (!moved) {
                        if (selectedKeys.isNotEmpty()) toggleSelect(key) else onOpen()
                    }
                    true
                }
                else -> false
            }
        }
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
                    if (banding) { banding = false; selection.clear() }
                    true
                }
                else -> false
            }
        }
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
                    state.normalX = panel.x; state.normalY = panel.y
                    true
                }
                else -> false
            }
        }
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
        // Fixed shortcuts
        fun addChip(label: String, onClick: () -> Unit) {
            val t = TextView(this).apply {
                text = label
                setTextColor(ContextCompat.getColor(this@MainActivity, R.color.text))
                textSize = 11f
                setPadding(18, 12, 18, 12)
                setBackgroundResource(R.drawable.bg_choice)
                setOnClickListener { onClick() }
                val lp = LinearLayout.LayoutParams(
                    LinearLayout.LayoutParams.WRAP_CONTENT,
                    LinearLayout.LayoutParams.WRAP_CONTENT
                )
                lp.marginEnd = 6
                layoutParams = lp
            }
            taskbarApps.addView(t)
        }
        addChip("AST") { openInternal("ast", "AST Terminal") }
        addChip("Dosyalar") { openInternal("files", "Dosya Gezgini") }
        openWindows.values.forEach { w ->
            addChip(if (w.minimized) "□ ${w.title}" else w.title) {
                if (w.minimized) restoreWindow(w.id) else bringFront(w.id)
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
        var cwd = RealFs.root()
        fun append(s: String) {
            output.append(s)
            scroll.post { scroll.fullScroll(View.FOCUS_DOWN) }
        }
        append("AST (panel) · ${cwd.absolutePath}\nhelp | pwd | ls | cd | neofetch\n\n")
        input.setOnEditorActionListener { _, _, _ ->
            val cmd = input.text?.toString()?.trim().orEmpty()
            input.setText("")
            if (cmd.isEmpty()) return@setOnEditorActionListener true
            append("$ $cmd\n")
            val p = cmd.split(Regex("\\s+"))
            when (p[0].lowercase()) {
                "help" -> append("pwd ls cd neofetch clear exit\n")
                "clear" -> output.text = ""
                "pwd" -> append(cwd.absolutePath + "\n")
                "ls" -> {
                    val list = RealFs.list(cwd)
                    append(list.joinToString("  ") { if (it.isDirectory) it.name + "/" else it.name } + "\n")
                }
                "cd" -> {
                    val t = if (p.size < 2 || p[1] == "~") RealFs.home()
                    else if (p[1].startsWith("/")) File(p[1]) else File(cwd, p[1])
                    if (t.isDirectory && t.canRead()) cwd = t else append("cd: hata\n")
                }
                "neofetch" -> append("AstraSage OS · ${Build.MODEL} · Android ${Build.VERSION.RELEASE}\n")
                "exit" -> closeWindow("ast")
                else -> append("ast: $cmd?\n")
            }
            true
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
