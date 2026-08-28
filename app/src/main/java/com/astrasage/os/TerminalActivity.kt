package com.astrasage.os

import com.astrasage.os.desktop.DesktopManager

import android.os.Build
import android.os.Bundle
import android.os.Environment
import android.os.StatFs
import android.view.inputmethod.EditorInfo
import android.widget.EditText
import android.widget.ScrollView
import android.widget.TextView
import androidx.appcompat.app.AppCompatActivity
import java.io.File

class TerminalActivity : AppCompatActivity() {

    private lateinit var output: TextView
    private lateinit var input: EditText
    private lateinit var scroll: ScrollView
    private var cwd: File = RealFs.root()
    private val history = mutableListOf<String>()
    private var histIdx = -1

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_terminal)

        output = findViewById(R.id.output)
        input = findViewById(R.id.input)
        scroll = findViewById(R.id.scroll)

        findViewById<android.view.View>(R.id.btnClose).setOnClickListener { finish() }

        append("AstraSage OS · AST Terminal v3 (native)\n")
        append("Gerçek dosya sistemi: ${cwd.absolutePath}\n")
        append("Komutlar: help, pwd, ls, cd, cat, mkdir, touch, rm, df, neofetch, apps, clear, exit\n\n")
        printPrompt()

        input.setOnEditorActionListener { _, actionId, _ ->
            if (actionId == EditorInfo.IME_ACTION_GO || actionId == EditorInfo.IME_ACTION_DONE) {
                val cmd = input.text?.toString()?.trim().orEmpty()
                input.setText("")
                if (cmd.isNotEmpty()) {
                    append("${promptText()}$cmd\n")
                    history.add(cmd)
                    histIdx = history.size
                    runCommand(cmd)
                }
                printPrompt()
                true
            } else false
        }
    }

    private fun promptText() = "user@astrasage:${shortPath()}$ "

    private fun shortPath(): String {
        val r = RealFs.root().absolutePath
        val p = cwd.absolutePath
        return if (p == r) "/" else if (p.startsWith(r)) p.removePrefix(r) else p
    }

    private fun printPrompt() {
        // prompt shown only when echoing commands
    }

    private fun append(text: String) {
        output.append(text)
        scroll.post { scroll.fullScroll(android.view.View.FOCUS_DOWN) }
    }

    private fun runCommand(raw: String) {
        val parts = raw.split(Regex("\\s+"))
        val cmd = parts.first().lowercase()
        val args = parts.drop(1)
        when (cmd) {
            "as" -> {
                    append(DesktopManager.listStatus()+"\n")
                }
                "help" -> append(
                """
                |pwd          — çalışma dizini
                |ls [yol]     — listele
                |cd [yol]     — dizin değiştir  (~ = Documents/AstraSage)
                |cat <dosya>  — dosya oku
                |mkdir <ad>   — klasör oluştur
                |touch <ad>   — boş dosya
                |rm <ad>      — sil
                |df           — depolama bilgisi
                |neofetch     — sistem bilgisi
                |apps         — yüklü uygulamalar
                |clear / exit
                |
                """.trimMargin()
            )
            "clear" -> output.text = ""
            "pwd" -> append(cwd.absolutePath + "\n")
            "whoami" -> append((Prefs.getUser(this).ifBlank { "user" }) + "\n")
            "uname" -> append("AstraSage OS native · Android ${Build.VERSION.RELEASE}\n")
            "ls" -> {
                val target = if (args.isEmpty()) cwd else resolve(args[0])
                if (target == null || !target.exists()) {
                    append("ls: bulunamadı\n")
                } else if (target.isFile) {
                    append(target.name + "\n")
                } else {
                    val list = RealFs.list(target)
                    if (list.isEmpty()) append("(boş)\n")
                    else append(list.joinToString("  ") {
                        if (it.isDirectory) "${it.name}/" else it.name
                    } + "\n")
                }
            }
            "cd" -> {
                val t = if (args.isEmpty() || args[0] == "~") RealFs.home() else resolve(args[0])
                when {
                    t == null || !t.exists() -> append("cd: dizin yok\n")
                    !t.isDirectory -> append("cd: dizin değil\n")
                    !t.canRead() -> append("cd: izin yok (Depolama izni ver)\n")
                    else -> cwd = t
                }
            }
            "cat" -> {
                if (args.isEmpty()) {
                    append("kullanım: cat <dosya>\n"); return
                }
                val t = resolve(args[0])
                when {
                    t == null || !t.exists() -> append("cat: yok\n")
                    t.isDirectory -> append("cat: klasör\n")
                    t.length() > 200_000 -> append("cat: dosya çok büyük\n")
                    else -> try {
                        append(t.readText() + "\n")
                    } catch (e: Exception) {
                        append("cat: ${e.message}\n")
                    }
                }
            }
            "mkdir" -> {
                if (args.isEmpty()) {
                    append("kullanım: mkdir <ad>\n"); return
                }
                val t = resolve(args[0])!!
                if (t.exists()) append("mkdir: zaten var\n")
                else if (t.mkdirs()) append("oluşturuldu: ${t.absolutePath}\n")
                else append("mkdir: başarısız (izin?)\n")
            }
            "touch" -> {
                if (args.isEmpty()) {
                    append("kullanım: touch <ad>\n"); return
                }
                val t = resolve(args[0])!!
                try {
                    if (!t.exists()) t.createNewFile()
                    append("ok\n")
                } catch (e: Exception) {
                    append("touch: ${e.message}\n")
                }
            }
            "rm" -> {
                if (args.isEmpty()) {
                    append("kullanım: rm <ad>\n"); return
                }
                val t = resolve(args[0])
                if (t == null || !t.exists()) append("rm: yok\n")
                else if (t.deleteRecursively()) append("silindi\n")
                else append("rm: başarısız\n")
            }
            "df" -> {
                try {
                    val path = Environment.getExternalStorageDirectory().absolutePath
                    val st = StatFs(path)
                    val total = st.totalBytes
                    val free = st.availableBytes
                    append("Disk: ${RealFs.formatSize(total)} toplam, ${RealFs.formatSize(free)} boş\n")
                    append("Yol: $path\n")
                } catch (e: Exception) {
                    append("df: ${e.message}\n")
                }
            }
            "neofetch" -> {
                val apps = try {
                    AppRepository.loadLauncherApps(packageManager).size
                } catch (_: Exception) {
                    0
                }
                var disk = "?"
                try {
                    val st = StatFs(Environment.getExternalStorageDirectory().absolutePath)
                    disk = "${RealFs.formatSize(st.availableBytes)} boş / ${RealFs.formatSize(st.totalBytes)}"
                } catch (_: Exception) {
                }
                append(
                    """
                    |
                    |  █████╗ ███████╗
                    | ██╔══██╗██╔════╝
                    | ███████║███████╗
                    | ██╔══██║╚════██║
                    | ██║  ██║███████║
                    | ╚═╝  ╚═╝╚══════╝
                    |
                    |OS       AstraSage OS 3.0 (Kotlin)
                    |Android  ${Build.VERSION.RELEASE} (API ${Build.VERSION.SDK_INT})
                    |Device   ${Build.MANUFACTURER} ${Build.MODEL}
                    |User     ${Prefs.getUser(this).ifBlank { "user" }}
                    |Apps     $apps
                    |Disk     $disk
                    |CWD      ${cwd.absolutePath}
                    |
                    """.trimMargin() + "\n"
                )
            }
            "apps" -> {
                val list = AppRepository.loadLauncherApps(packageManager)
                append(list.joinToString("\n") { "  • ${it.label}" } + "\n")
            }
            "exit" -> finish()
            else -> append("ast: komut bulunamadı: $cmd\n")
        }
    }

    private fun resolve(path: String): File? {
        if (path == "~") return RealFs.home()
        if (path.startsWith("/")) return File(path)
        return File(cwd, path)
    }
}
