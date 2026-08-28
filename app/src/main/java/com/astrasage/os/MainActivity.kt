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
        findViewById<View>(R.id.menuStore)?.setOnClickListener { hideStart(); openPlayStore() }
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
        val scale = Prefs.getIconScale(this).coerceIn(0.7f, 1.5f)
        val iconW = (76 * density * scale).toInt()
        val iconH = (90 * density * scale).toInt()
        val topPad = (16 * density).toInt()
        val leftPad = (8 * density).toInt()
        val gapX = (6 * density * scale).toInt()
        val gapY = (8 * density * scale).toInt()
        val cols = if (scale > 1.15f) 3 else if (scale < 0.85f) 5 else 4
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
        if (!recycle.contains("sys:ast")) {
            val astView = LayoutInflater.from(this).inflate(R.layout.item_desktop_icon, desktop, false)
            astView.findViewById<ImageView>(R.id.appIcon).setImageResource(R.drawable.ast_icon)
            astView.findViewById<TextView>(R.id.appLabel).text = "AST"
            placeIcon(astView, "sys:ast", index, positions, leftPad, topPad, iconW, iconH, gapX, gapY, cols)
            bindIconTouch(astView, "sys:ast", onOpen = { openInternal("ast", "AST Terminal") }, onDelete = {
                Prefs.moveToRecycle(this, "sys:ast"); layoutDesktop()
            })
            index++
        }
        sys("sys:calendar", "Takvim", "📅") { openInternal("calendar", "Takvim / Saat") }

        // Only essential apps (browser, store, files, settings, phone, messages, camera…)
        val essential = essentialApps(allApps)
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
            val view = inflateEmojiIcon("AstraStore", "🛒")
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
                        // Visual feedback when over trash
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
                            sendToTrash(key)
                            return@setOnTouchListener true
                        }
                        if (selectedKeys.contains(key) && selectedKeys.size > 1) {
                            selectedKeys.forEach { k -> iconViews[k]?.let { savePos(k, it.x, it.y) } }
                        } else {
                            savePos(key, v.x, v.y)
                        }
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
        val popup = android.widget.PopupMenu(this, anchor)
        popup.menu.add(0, 1, 0, "Aç")
        popup.menu.add(0, 2, 1, "İkon: Küçük")
        popup.menu.add(0, 3, 2, "İkon: Normal")
        popup.menu.add(0, 4, 3, "İkon: Büyük")
        popup.menu.add(0, 5, 4, "Yeni klasör")
        popup.menu.add(0, 6, 5, "Yeni dosya")
        if (key != "sys:trash") popup.menu.add(0, 7, 6, "Çöp kutusuna taşı")
        popup.setOnMenuItemClickListener { item ->
            when (item.itemId) {
                1 -> onOpen()
                2 -> { Prefs.setIconScale(this, 0.75f); layoutDesktop() }
                3 -> { Prefs.setIconScale(this, 1.0f); layoutDesktop() }
                4 -> { Prefs.setIconScale(this, 1.35f); layoutDesktop() }
                5 -> createDesktopFolder()
                6 -> createDesktopFile()
                7 -> sendToTrash(key)
            }
            true
        }
        popup.show()
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
        popup.menu.add(0, 3, 2, "İkon: Küçük")
        popup.menu.add(0, 4, 3, "İkon: Normal")
        popup.menu.add(0, 5, 4, "İkon: Büyük")
        popup.menu.add(0, 6, 5, "Yenile")
        popup.setOnMenuItemClickListener { item ->
            when (item.itemId) {
                1 -> createDesktopFolder()
                2 -> createDesktopFile()
                3 -> { Prefs.setIconScale(this, 0.75f); layoutDesktop() }
                4 -> { Prefs.setIconScale(this, 1.0f); layoutDesktop() }
                5 -> { Prefs.setIconScale(this, 1.35f); layoutDesktop() }
                6 -> layoutDesktop()
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
        // Two-finger pinch to resize (like trackpad / mouse wheel zoom on desktop)
        var startDist = 0f
        var startW = 0
        var startH = 0
        panel.setOnTouchListener { v, e ->
            when (e.actionMasked) {
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
        fun addDock(label: String, emoji: String? = null, resId: Int? = null, onClick: () -> Unit) {
            val wrap = LinearLayout(this).apply {
                orientation = LinearLayout.HORIZONTAL
                gravity = android.view.Gravity.CENTER_VERTICAL
                setPadding(12, 6, 12, 6)
                setOnClickListener { onClick() }
                setBackgroundResource(R.drawable.bg_dock_item)
                val lp = LinearLayout.LayoutParams(
                    LinearLayout.LayoutParams.WRAP_CONTENT,
                    LinearLayout.LayoutParams.MATCH_PARENT
                )
                lp.marginEnd = 6
                layoutParams = lp
            }
            if (resId != null) {
                wrap.addView(ImageView(this).apply {
                    setImageResource(resId)
                    layoutParams = LinearLayout.LayoutParams(22, 22)
                    scaleType = ImageView.ScaleType.FIT_CENTER
                })
            } else if (emoji != null) {
                wrap.addView(TextView(this).apply {
                    text = emoji
                    textSize = 14f
                    setPadding(0, 0, 6, 0)
                })
            }
            wrap.addView(TextView(this).apply {
                text = label
                setTextColor(0xFFEEEEEE.toInt())
                textSize = 11f
                maxLines = 1
            })
            taskbarApps.addView(wrap)
        }
        addDock("AST", resId = R.drawable.ast_icon) { openInternal("ast", "AST Terminal") }
        addDock("Dosyalar", emoji = "📁") { openInternal("files", "Dosya Gezgini") }
        addDock("Store", emoji = "🛒") { openPlayStore() }
        openWindows.values.forEach { w ->
            val mark = if (w.minimized) "• " else ""
            addDock(mark + w.title.take(12), emoji = "▣") {
                if (w.minimized) restoreWindow(w.id) else bringFront(w.id)
            }
        }
        addDock("Uygulamalar", emoji = "☰") { openAppDrawer() }
    }

    private var drawerOverlay: View? = null

    private fun openAppDrawer() {
        hideStart()
        if (drawerOverlay != null) {
            closeAppDrawer()
            return
        }
        val overlay = LayoutInflater.from(this).inflate(R.layout.panel_app_drawer, windowsHost, false)
        val lp = FrameLayout.LayoutParams(
            FrameLayout.LayoutParams.MATCH_PARENT,
            FrameLayout.LayoutParams.MATCH_PARENT
        )
        windowsHost.addView(overlay, lp)
        drawerOverlay = overlay
        overlay.elevation = 50f

        overlay.findViewById<View>(R.id.btnCloseDrawer).setOnClickListener { closeAppDrawer() }

        val grid = overlay.findViewById<RecyclerView>(R.id.drawerGrid)
        val span = if (resources.configuration.orientation == android.content.res.Configuration.ORIENTATION_LANDSCAPE) 8 else 5
        grid.layoutManager = androidx.recyclerview.widget.GridLayoutManager(this, span)

        val apps = allApps
        grid.adapter = object : RecyclerView.Adapter<RecyclerView.ViewHolder>() {
            override fun getItemCount() = apps.size
            override fun onCreateViewHolder(parent: android.view.ViewGroup, viewType: Int): RecyclerView.ViewHolder {
                val v = LayoutInflater.from(parent.context).inflate(R.layout.item_drawer_app, parent, false)
                return object : RecyclerView.ViewHolder(v) {}
            }
            override fun onBindViewHolder(holder: RecyclerView.ViewHolder, position: Int) {
                val app = apps[position]
                val v = holder.itemView
                v.findViewById<ImageView>(R.id.drawerIcon).setImageDrawable(app.icon)
                v.findViewById<TextView>(R.id.drawerLabel).text = app.label
                v.setOnClickListener { openApp(app) }
                // Long press → pin to desktop
                v.setOnLongClickListener {
                    val pa = "${app.packageName}/${app.activityName}"
                    Prefs.pinApp(this@MainActivity, pa)
                    vibrate()
                    Toast.makeText(this@MainActivity, "${app.label} masaüstüne eklendi", Toast.LENGTH_SHORT).show()
                    layoutDesktop()
                    true
                }
                // Drag from drawer onto desktop: long-press then move to desktop edge
                bindDrawerDrag(v, app)
            }
        }
    }

    private fun closeAppDrawer() {
        drawerOverlay?.let { windowsHost.removeView(it) }
        drawerOverlay = null
    }

    @SuppressLint("ClickableViewAccessibility")
    private fun bindDrawerDrag(itemView: View, app: AppInfo) {
        val touchSlop = ViewConfiguration.get(this).scaledTouchSlop
        val longPressTimeout = ViewConfiguration.getLongPressTimeout().toLong()
        var downX = 0f; var downY = 0f
        var dragging = false; var moved = false
        var ghost: View? = null
        var runnable: Runnable? = null

        itemView.setOnTouchListener { v, e ->
            when (e.actionMasked) {
                MotionEvent.ACTION_DOWN -> {
                    downX = e.rawX; downY = e.rawY
                    dragging = false; moved = false
                    runnable = Runnable {
                        dragging = true
                        vibrate()
                        // floating ghost icon
                        ghost = ImageView(this).apply {
                            setImageDrawable(app.icon)
                            layoutParams = FrameLayout.LayoutParams(100, 100)
                            x = e.rawX - 50
                            y = e.rawY - 50
                            elevation = 80f
                            alpha = 0.85f
                        }
                        windowsHost.addView(ghost)
                    }
                    v.postDelayed(runnable!!, longPressTimeout)
                    false // allow click/longclick too
                }
                MotionEvent.ACTION_MOVE -> {
                    val dx = e.rawX - downX; val dy = e.rawY - downY
                    if (!dragging && (kotlin.math.abs(dx) > touchSlop || kotlin.math.abs(dy) > touchSlop)) {
                        runnable?.let { v.removeCallbacks(it) }
                    }
                    if (dragging) {
                        moved = true
                        ghost?.x = e.rawX - 50
                        ghost?.y = e.rawY - 50
                    }
                    dragging
                }
                MotionEvent.ACTION_UP, MotionEvent.ACTION_CANCEL -> {
                    runnable?.let { v.removeCallbacks(it) }
                    if (dragging && moved) {
                        // Drop anywhere outside drawer top area counts as pin to desktop
                        val pa = "${app.packageName}/${app.activityName}"
                        Prefs.pinApp(this, pa)
                        // save position near drop
                        val key = "app:$pa"
                        val density = resources.displayMetrics.density
                        val obj = loadPositions()
                        obj.put(key, org.json.JSONObject().apply {
                            put("x", (e.rawX - 40).toInt().coerceAtLeast(0))
                            put("y", (e.rawY - 40).toInt().coerceAtLeast(0))
                        })
                        Prefs.setIconPositions(this, obj.toString())
                        Toast.makeText(this, "${app.label} masaüstüne eklendi", Toast.LENGTH_SHORT).show()
                        closeAppDrawer()
                        layoutDesktop()
                    }
                    ghost?.let { windowsHost.removeView(it) }
                    ghost = null
                    dragging = false
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


    private val ESSENTIAL_PACKAGES = listOf(
        "com.android.chrome", "com.chrome.beta", "com.sec.android.app.sbrowser",
        "com.opera.browser", "org.mozilla.firefox", "com.microsoft.emmx",
        "com.android.vending", // Play Store → shown as AstraStore open
        "com.google.android.apps.nbu.files", "com.sec.android.app.myfiles",
        "com.android.documentsui", "com.google.android.documentsui",
        "com.android.settings",
        "com.google.android.dialer", "com.samsung.android.dialer", "com.android.dialer",
        "com.google.android.apps.messaging", "com.samsung.android.messaging",
        "com.google.android.GoogleCamera", "com.sec.android.app.camera", "com.android.camera2",
        "com.google.android.apps.photos", "com.sec.android.gallery3d",
        "com.google.android.gm", "com.google.android.apps.maps",
        "com.google.android.youtube", "com.android.calculator2",
        "com.google.android.calculator", "com.sec.android.app.popupcalculator",
        "com.google.android.deskclock", "com.sec.android.app.clockpackage",
        "com.google.android.contacts", "com.samsung.android.contacts"
    )

    private fun isStorePackage(pkg: String) =
        pkg == "com.android.vending" || pkg.contains("appmarket") || pkg.contains("store") && pkg.contains("huawei")

    private fun essentialApps(all: List<AppInfo>): List<AppInfo> {
        val out = mutableListOf<AppInfo>()
        val seen = mutableSetOf<String>()
        for (pkg in ESSENTIAL_PACKAGES) {
            val match = all.filter { it.packageName == pkg }
            match.forEach {
                if (it.packageName !in seen) {
                    // Skip raw Play Store icon — we show AstraStore instead
                    if (it.packageName == "com.android.vending") return@forEach
                    seen.add(it.packageName)
                    out.add(it)
                }
            }
        }
        // fallback browsers by name
        if (out.none { it.packageName.contains("chrome") || it.label.contains("Browser", true) }) {
            all.firstOrNull { it.label.contains("Chrome", true) || it.label.contains("Internet", true) || it.label.contains("Browser", true) }
                ?.let { out.add(0, it) }
        }
        return out.distinctBy { it.packageName }
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
