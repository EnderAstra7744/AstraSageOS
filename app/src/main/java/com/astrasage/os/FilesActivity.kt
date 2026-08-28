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
    private lateinit var list: RecyclerView
    private var cwd: File = RealFs.root()

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_files)

        pathText = findViewById(R.id.pathView)
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
            cwd = RealFs.home()
            refresh()
        }
        findViewById<View>(R.id.btnRefresh)?.setOnClickListener { refresh() }
        findViewById<View>(R.id.btnPin)?.setOnClickListener {
            Prefs.addDesktopPin(this, cwd.absolutePath)
            Toast.makeText(this, "Masaüstüne eklendi", Toast.LENGTH_SHORT).show()
        }

        findViewById<View>(R.id.navHome)?.setOnClickListener {
            cwd = RealFs.home(); refresh()
        }
        findViewById<View>(R.id.navDownloads)?.setOnClickListener {
            cwd = Environment.getExternalStoragePublicDirectory(Environment.DIRECTORY_DOWNLOADS)
            refresh()
        }
        findViewById<View>(R.id.navRoot)?.setOnClickListener {
            cwd = RealFs.root(); refresh()
        }
        findViewById<View>(R.id.navMusic)?.setOnClickListener {
            cwd = Environment.getExternalStoragePublicDirectory(Environment.DIRECTORY_MUSIC)
            refresh()
        }
        findViewById<View>(R.id.navPictures)?.setOnClickListener {
            cwd = Environment.getExternalStoragePublicDirectory(Environment.DIRECTORY_PICTURES)
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
                try {
                    val i = Intent(Settings.ACTION_MANAGE_APP_ALL_FILES_ACCESS_PERMISSION)
                    i.data = Uri.parse("package:$packageName")
                    startActivity(i)
                } catch (_: Exception) {
                    startActivity(Intent(Settings.ACTION_MANAGE_ALL_FILES_ACCESS_PERMISSION))
                }
            }
        } else {
            if (ContextCompat.checkSelfPermission(this, Manifest.permission.READ_EXTERNAL_STORAGE)
                != PackageManager.PERMISSION_GRANTED
            ) {
                ActivityCompat.requestPermissions(
                    this,
                    arrayOf(Manifest.permission.READ_EXTERNAL_STORAGE),
                    42
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
        list.adapter = object : RecyclerView.Adapter<RecyclerView.ViewHolder>() {
            override fun getItemCount() = files.size
            override fun onCreateViewHolder(parent: ViewGroup, viewType: Int): RecyclerView.ViewHolder {
                val v = LayoutInflater.from(parent.context).inflate(R.layout.item_file, parent, false)
                return object : RecyclerView.ViewHolder(v) {}
            }
            override fun onBindViewHolder(holder: RecyclerView.ViewHolder, position: Int) {
                val f = files[position]
                val name = holder.itemView.findViewById<TextView>(R.id.fileName)
                val meta = holder.itemView.findViewById<TextView>(R.id.fileMeta)
                name?.text = if (f.isDirectory) "📁 ${f.name}" else "📄 ${f.name}"
                meta?.text = if (f.isDirectory) "Klasör" else RealFs.formatSize(f.length())
                holder.itemView.setOnClickListener {
                    if (f.isDirectory) {
                        cwd = f
                        refresh()
                    } else {
                        Toast.makeText(this@FilesActivity, f.absolutePath, Toast.LENGTH_SHORT).show()
                    }
                }
                holder.itemView.setOnLongClickListener {
                    Prefs.addDesktopPin(this@FilesActivity, f.absolutePath)
                    Toast.makeText(this@FilesActivity, "Masaüstüne eklendi", Toast.LENGTH_SHORT).show()
                    true
                }
            }
        }
    }
}
