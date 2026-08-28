package com.astrasage.os

import android.Manifest
import android.content.Intent
import android.content.pm.PackageManager
import android.net.Uri
import android.os.Build
import android.os.Bundle
import android.os.Environment
import android.provider.Settings
import android.view.LayoutInflater
import android.view.View
import android.view.ViewGroup
import android.widget.TextView
import android.widget.Toast
import androidx.appcompat.app.AppCompatActivity
import androidx.core.app.ActivityCompat
import androidx.core.content.ContextCompat
import androidx.recyclerview.widget.LinearLayoutManager
import androidx.recyclerview.widget.RecyclerView
import java.io.File

class FilesActivity : AppCompatActivity() {

    private lateinit var pathText: TextView
    private lateinit var permHint: TextView
    private lateinit var list: RecyclerView
    private var cwd: File = RealFs.root()

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_files)

        pathText = findViewById(R.id.pathText)
        permHint = findViewById(R.id.permHint)
        list = findViewById(R.id.fileList)
        list.layoutManager = LinearLayoutManager(this)

        findViewById<View>(R.id.btnClose).setOnClickListener { finish() }
        findViewById<View>(R.id.btnUp).setOnClickListener {
            val parent = cwd.parentFile
            if (parent != null && parent.canRead()) {
                cwd = parent
                refresh()
            }
        }
        findViewById<View>(R.id.btnHome).setOnClickListener {
            cwd = RealFs.root()
            refresh()
        }

        requestStorageAccess()
        refresh()
    }

    override fun onResume() {
        super.onResume()
        refresh()
    }

    private fun requestStorageAccess() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
            if (!Environment.isExternalStorageManager()) {
                permHint.visibility = View.VISIBLE
                permHint.text = "Dosyalara erişim için dokun → İzin ver (Tüm dosyalar)"
                permHint.setOnClickListener {
                    try {
                        val i = Intent(Settings.ACTION_MANAGE_APP_ALL_FILES_ACCESS_PERMISSION)
                        i.data = Uri.parse("package:$packageName")
                        startActivity(i)
                    } catch (_: Exception) {
                        startActivity(Intent(Settings.ACTION_MANAGE_ALL_FILES_ACCESS_PERMISSION))
                    }
                }
            } else {
                permHint.visibility = View.GONE
            }
        } else {
            if (ContextCompat.checkSelfPermission(this, Manifest.permission.READ_EXTERNAL_STORAGE)
                != PackageManager.PERMISSION_GRANTED
            ) {
                ActivityCompat.requestPermissions(
                    this,
                    arrayOf(Manifest.permission.READ_EXTERNAL_STORAGE),
                    100
                )
            }
        }
    }

    private fun refresh() {
        pathText.text = cwd.absolutePath
        val files = try {
            RealFs.list(cwd)
        } catch (_: Exception) {
            emptyList()
        }
        list.adapter = FileAdapter(files,
            onOpen = { f ->
                if (f.isDirectory) {
                    cwd = f
                    refresh()
                } else {
                    openFile(f)
                }
            },
            onPin = { f ->
                Prefs.addDesktopPin(this, f.absolutePath)
                Toast.makeText(this, "Masaüstüne eklendi: ${f.name}", Toast.LENGTH_SHORT).show()
            }
        )
    }

    private fun openFile(f: File) {
        try {
            val uri = androidx.core.content.FileProvider.getUriForFile(
                this, "$packageName.provider", f
            )
            val mime = contentResolver.getType(uri) ?: "*/*"
            val i = Intent(Intent.ACTION_VIEW).setDataAndType(uri, mime).addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION)
            startActivity(Intent.createChooser(i, f.name))
        } catch (_: Exception) {
            Toast.makeText(this, "Açılamadı: ${f.name}", Toast.LENGTH_SHORT).show()
        }
    }

    class FileAdapter(
        private val items: List<File>,
        private val onOpen: (File) -> Unit,
        private val onPin: (File) -> Unit
    ) : RecyclerView.Adapter<FileAdapter.H>() {
        class H(v: View) : RecyclerView.ViewHolder(v) {
            val icon: TextView = v.findViewById(R.id.fileIcon)
            val name: TextView = v.findViewById(R.id.fileName)
            val meta: TextView = v.findViewById(R.id.fileMeta)
            val pin: TextView = v.findViewById(R.id.btnPin)
        }

        override fun onCreateViewHolder(parent: ViewGroup, viewType: Int): H {
            val v = LayoutInflater.from(parent.context).inflate(R.layout.item_file, parent, false)
            return H(v)
        }

        override fun getItemCount() = items.size

        override fun onBindViewHolder(h: H, position: Int) {
            val f = items[position]
            h.icon.text = if (f.isDirectory) "📁" else "📄"
            h.name.text = f.name
            h.meta.text = if (f.isDirectory) "Klasör" else RealFs.formatSize(f.length())
            h.itemView.setOnClickListener { onOpen(f) }
            h.pin.setOnClickListener { onPin(f) }
        }
    }
}
