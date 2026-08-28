package com.astrasage.os

import android.annotation.SuppressLint
import android.content.ComponentName
import android.content.Intent
import android.graphics.Rect
import android.os.Build
import android.os.Bundle
import android.os.VibrationEffect
import android.os.Vibrator
import android.os.VibratorManager
import android.text.Editable
import android.text.TextWatcher
import android.view.LayoutInflater
import android.view.MotionEvent
import android.view.View
import android.view.ViewConfiguration
import android.widget.FrameLayout
import android.widget.ImageView
import android.widget.TextView
import android.widget.Toast
import androidx.appcompat.app.AlertDialog
import androidx.appcompat.app.AppCompatActivity
import androidx.core.view.isVisible
import androidx.recyclerview.widget.GridLayoutManager
import androidx.recyclerview.widget.RecyclerView
import org.json.JSONObject
import java.io.File

class MainActivity : AppCompatActivity() {

    private lateinit var desktop: FrameLayout
    private lateinit var selection: SelectionOverlay
    private lateinit var appGrid: RecyclerView
    private lateinit var startMenu: View
    private lateinit var subtitle: TextView
    private lateinit var searchBox: android.widget.EditText
    private lateinit var adapter: AppAdapter

    private var allApps: List<AppInfo> = emptyList()
    private var desktopMode = true
    private val selectedKeys = mutableSetOf<String>()
    private val iconViews = mutableMapOf<String, View>()

    // rubber-band
    private var bandStartX = 0f
    private var bandStartY = 0f
    private var banding = false

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_main)

        desktop = findViewById(R.id.desktop)
        selection = findViewById(R.id.selectionOverlay)
        appGrid = findViewById(R.id.appGrid)
        startMenu = findViewById(R.id.startMenu)
        subtitle = findViewById(R.id.subtitleText)
        searchBox = findViewById(R.id.searchBox)

        val span = if (resources.configuration.smallestScreenWidthDp >= 600) 6 else 4
        appGrid.layoutManager = GridLayoutManager(this, span)
        adapter = AppAdapter(emptyList(), onClick = { openApp(it) }, onLongClick = {
            Toast.makeText(this, "${it.label}\n${it.packageName}", Toast.LENGTH_SHORT).show()
        })
        appGrid.adapter = adapter

        searchBox.addTextChangedListener(object : TextWatcher {
            override fun beforeTextChanged(s: CharSequence?, start: Int, count: Int, after: Int) {}
            override fun onTextChanged(s: CharSequence?, start: Int, before: Int, count: Int) {}
            override fun afterTextChanged(s: Editable?) {
                val q = s?.toString().orEmpty()
                if (q.isNotBlank()) showGrid(AppRepository.filter(allApps, q))
                else if (desktopMode) showDesktop()
                else showGrid(allApps)
            }
        })

        findViewById<View>(R.id.btnStart).setOnClickListener {
            startMenu.isVisible = !startMenu.isVisible
        }
        findViewById<View>(R.id.btnAst).setOnClickListener {
            hideStart(); startActivity(Intent(this, TerminalActivity::class.java))
        }
        findViewById<View>(R.id.btnFiles).setOnClickListener {
            hideStart(); startActivity(Intent(this, FilesActivity::class.java))
        }
        findViewById<View>(R.id.btnAll).setOnClickListener {
            hideStart(); desktopMode = false; searchBox.setText(""); showGrid(allApps)
        }
        findViewById<View>(R.id.btnDesktop).setOnClickListener {
            hideStart(); desktopMode = true; searchBox.setText(""); showDesktop()
        }

        findViewById<View>(R.id.menuAst).setOnClickListener {
            hideStart(); startActivity(Intent(this, TerminalActivity::class.java))
        }
        findViewById<View>(R.id.menuFiles).setOnClickListener {
            hideStart(); startActivity(Intent(this, FilesActivity::class.java))
        }
        findViewById<View>(R.id.menuAll).setOnClickListener {
            hideStart(); desktopMode = false; showGrid(allApps)
        }
        findViewById<View>(R.id.menuAbout).setOnClickListener { hideStart(); showAbout() }
        findViewById<View>(R.id.menuLogout).setOnClickListener {
            hideStart()
            startActivity(Intent(this, SetupActivity::class.java))
            finish()
        }

        setupRubberBand()
        reloadApps()
    }

    override fun onResume() {
        super.onResume()
        if (desktopMode) showDesktop()
    }

    private fun hideStart() { startMenu.isVisible = false }

    private fun reloadApps() {
        allApps = AppRepository.loadLauncherApps(packageManager)
        subtitle.text = "${allApps.size} uygulama · ${Prefs.getUser(this)}"
        if (desktopMode) showDesktop() else showGrid(allApps)
    }

    private fun showGrid(list: List<AppInfo>) {
        desktop.isVisible = false
        selection.clear()
        appGrid.isVisible = true
        adapter.submit(list)
        desktopMode = false
    }

    private fun showDesktop() {
        appGrid.isVisible = false
        desktop.isVisible = true
        desktopMode = true
        layoutDesktop()
    }

    private fun layoutDesktop() {
        desktop.removeAllViews()
        iconViews.clear()
        selectedKeys.clear()

        val density = resources.displayMetrics.density
        val iconW = (76 * density).toInt()
        val iconH = (90 * density).toInt()
        val topPad = (112 * density).toInt()
        val leftPad = (8 * density).toInt()
        val gapX = (4 * density).toInt()
        val gapY = (6 * density).toInt()
        val cols = 4
        val positions = loadPositions()

        var index = 0

        // App icons (first 20)
        allApps.take(20).forEach { app ->
            val key = "app:${app.packageName}/${app.activityName}"
            val view = inflateIcon(
                label = app.label,
                drawable = app.icon,
                isFolder = false
            )
            placeIcon(view, key, index, positions, leftPad, topPad, iconW, iconH, gapX, gapY, cols)
            bindIconTouch(view, key, onOpen = { openApp(app) })
            index++
        }

        // Pinned files / folders from real FS
        Prefs.getDesktopPins(this).forEach { path ->
            val f = File(path)
            if (!f.exists()) return@forEach
            val key = "file:$path"
            val view = inflateIcon(
                label = f.name,
                drawable = null,
                isFolder = f.isDirectory,
                emoji = if (f.isDirectory) "📁" else "📄"
            )
            placeIcon(view, key, index, positions, leftPad, topPad, iconW, iconH, gapX, gapY, cols)
            bindIconTouch(view, key, onOpen = {
                if (f.isDirectory) {
                    startActivity(Intent(this, FilesActivity::class.java))
                } else {
                    Toast.makeText(this, f.absolutePath, Toast.LENGTH_SHORT).show()
                }
            }, onLongExtra = {
                Prefs.removeDesktopPin(this, path)
                Toast.makeText(this, "Masaüstünden kaldırıldı", Toast.LENGTH_SHORT).show()
                showDesktop()
            })
            index++
        }
    }

    private fun inflateIcon(
        label: String,
        drawable: android.graphics.drawable.Drawable?,
        isFolder: Boolean,
        emoji: String? = null
    ): View {
        val view = LayoutInflater.from(this).inflate(R.layout.item_desktop_icon, desktop, false)
        val icon = view.findViewById<ImageView>(R.id.appIcon)
        if (drawable != null) {
            icon.setImageDrawable(drawable)
        } else {
            // emoji fallback via label text size in plate — use a text approach
            icon.setImageDrawable(null)
            icon.visibility = View.GONE
            val plate = view.findViewById<View>(R.id.iconBg).parent as FrameLayout
            val tv = TextView(this).apply {
                text = emoji ?: if (isFolder) "📁" else "📄"
                textSize = 28f
                gravity = android.view.Gravity.CENTER
                layoutParams = FrameLayout.LayoutParams(
                    FrameLayout.LayoutParams.MATCH_PARENT,
                    FrameLayout.LayoutParams.MATCH_PARENT
                )
            }
            plate.addView(tv)
        }
        view.findViewById<TextView>(R.id.appLabel).text = label
        return view
    }

    private fun placeIcon(
        view: View,
        key: String,
        index: Int,
        positions: JSONObject,
        leftPad: Int,
        topPad: Int,
        iconW: Int,
        iconH: Int,
        gapX: Int,
        gapY: Int,
        cols: Int
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
    private fun bindIconTouch(
        view: View,
        key: String,
        onOpen: () -> Unit,
        onLongExtra: (() -> Unit)? = null
    ) {
        val touchSlop = ViewConfiguration.get(this).scaledTouchSlop
        val longPressTimeout = ViewConfiguration.getLongPressTimeout().toLong()
        var downX = 0f
        var downY = 0f
        var startX = 0f
        var startY = 0f
        var dragging = false
        var longPressed = false
        var moved = false
        var runnable: Runnable? = null

        view.setOnTouchListener { v, event ->
            when (event.actionMasked) {
                MotionEvent.ACTION_DOWN -> {
                    downX = event.rawX
                    downY = event.rawY
                    startX = v.x
                    startY = v.y
                    dragging = false
                    longPressed = false
                    moved = false
                    runnable = Runnable {
                        longPressed = true
                        dragging = true
                        v.animate().scaleX(1.1f).scaleY(1.1f).setDuration(80).start()
                        v.elevation = 28f
                        vibrate()
                        if (onLongExtra != null && selectedKeys.isEmpty()) {
                            // optional: long press menu for file pins handled on end if !moved
                        }
                    }
                    v.postDelayed(runnable!!, longPressTimeout)
                    true
                }
                MotionEvent.ACTION_MOVE -> {
                    val dx = event.rawX - downX
                    val dy = event.rawY - downY
                    if (!dragging && (kotlin.math.abs(dx) > touchSlop || kotlin.math.abs(dy) > touchSlop)) {
                        runnable?.let { v.removeCallbacks(it) }
                        if (!longPressed) return@setOnTouchListener true
                    }
                    if (dragging) {
                        moved = true
                        var nx = startX + dx
                        var ny = startY + dy
                        val maxX = (desktop.width - v.width).toFloat().coerceAtLeast(0f)
                        val maxY = (desktop.height - v.height).toFloat().coerceAtLeast(0f)
                        nx = nx.coerceIn(0f, maxX)
                        ny = ny.coerceIn(0f, maxY)
                        // multi-drag: move all selected
                        val ddx = nx - v.x
                        val ddy = ny - v.y
                        if (selectedKeys.contains(key) && selectedKeys.size > 1) {
                            selectedKeys.forEach { k ->
                                iconViews[k]?.let { iv ->
                                    iv.x = (iv.x + ddx).coerceIn(0f, maxX)
                                    iv.y = (iv.y + ddy).coerceIn(0f, maxY)
                                }
                            }
                        } else {
                            v.x = nx
                            v.y = ny
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
                            selectedKeys.forEach { k ->
                                iconViews[k]?.let { iv -> savePos(k, iv.x, iv.y) }
                            }
                        } else {
                            savePos(key, v.x, v.y)
                        }
                        dragging = false
                        if (!moved && onLongExtra != null) onLongExtra.invoke()
                    } else if (!moved) {
                        // toggle select if already multi, else open
                        if (selectedKeys.isNotEmpty()) {
                            toggleSelect(key)
                        } else {
                            onOpen()
                        }
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
        if (selectedKeys.contains(key)) {
            plate.setBackgroundResource(R.drawable.bg_icon_selected)
        } else {
            plate.setBackgroundResource(R.drawable.bg_icon_plate)
        }
    }

    private fun clearSelection() {
        val old = selectedKeys.toList()
        selectedKeys.clear()
        old.forEach { k -> iconViews[k]?.let { updateSelectedBg(it, k) } }
    }

    /** Empty-area drag = Windows-style blue selection rectangle */
    @SuppressLint("ClickableViewAccessibility")
    private fun setupRubberBand() {
        desktop.setOnTouchListener { _, event ->
            when (event.actionMasked) {
                MotionEvent.ACTION_DOWN -> {
                    hideStart()
                    // only start band if not on an icon — desktop receives events on empty space
                    bandStartX = event.x
                    bandStartY = event.y
                    banding = true
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
                        banding = false
                        selection.clear()
                    }
                    true
                }
                else -> false
            }
        }
    }

    private fun selectIconsInRect(r: android.graphics.RectF) {
        selectedKeys.clear()
        iconViews.forEach { (key, view) ->
            val vr = Rect()
            view.getHitRect(vr)
            // view x/y are used because getHitRect is relative to parent after layout
            val left = view.x
            val top = view.y
            val right = left + view.width
            val bottom = top + view.height
            val intersects = r.left < right && r.right > left && r.top < bottom && r.bottom > top
            if (intersects) selectedKeys.add(key)
            updateSelectedBg(view, key)
        }
    }

    private fun loadPositions(): JSONObject {
        return try {
            JSONObject(Prefs.getIconPositions(this))
        } catch (_: Exception) {
            JSONObject()
        }
    }

    private fun savePos(key: String, x: Float, y: Float) {
        val obj = loadPositions()
        obj.put(key, JSONObject().apply {
            put("x", x.toInt())
            put("y", y.toInt())
        })
        Prefs.setIconPositions(this, obj.toString())
    }

    private fun vibrate() {
        try {
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
                val vm = getSystemService(VibratorManager::class.java)
                vm?.defaultVibrator?.vibrate(
                    VibrationEffect.createOneShot(16, VibrationEffect.DEFAULT_AMPLITUDE)
                )
            } else {
                @Suppress("DEPRECATION")
                val v = getSystemService(VIBRATOR_SERVICE) as Vibrator
                if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.O) {
                    v.vibrate(VibrationEffect.createOneShot(16, VibrationEffect.DEFAULT_AMPLITUDE))
                } else {
                    @Suppress("DEPRECATION")
                    v.vibrate(16)
                }
            }
        } catch (_: Exception) {
        }
    }

    private fun openApp(app: AppInfo) {
        hideStart()
        try {
            val intent = Intent(Intent.ACTION_MAIN).apply {
                addCategory(Intent.CATEGORY_LAUNCHER)
                component = ComponentName(app.packageName, app.activityName)
                flags = Intent.FLAG_ACTIVITY_NEW_TASK
            }
            startActivity(intent)
        } catch (_: Exception) {
            try {
                packageManager.getLaunchIntentForPackage(app.packageName)?.let {
                    it.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)
                    startActivity(it)
                } ?: Toast.makeText(this, R.string.open_failed, Toast.LENGTH_SHORT).show()
            } catch (_: Exception) {
                Toast.makeText(this, R.string.open_failed, Toast.LENGTH_SHORT).show()
            }
        }
    }

    private fun showAbout() {
        val msg = """
            AstraSage OS 3.0 (Native)
            
            Kullanıcı: ${Prefs.getUser(this)}
            Cihaz: ${Build.MANUFACTURER} ${Build.MODEL}
            Android: ${Build.VERSION.RELEASE}
            Uygulama: ${allApps.size}
            
            Masaüstü boş alanda sürükle = çoklu seçim
            İkon: basılı tut = taşı
        """.trimIndent()
        AlertDialog.Builder(this)
            .setTitle("Bu cihaz hakkında")
            .setMessage(msg)
            .setPositiveButton("Tamam", null)
            .show()
    }
}
