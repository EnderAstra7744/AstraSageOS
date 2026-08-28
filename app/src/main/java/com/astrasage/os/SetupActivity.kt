package com.astrasage.os

import android.content.Intent
import android.os.Bundle
import android.view.View
import android.widget.EditText
import android.widget.TextView
import android.widget.Toast
import androidx.appcompat.app.AppCompatActivity

class SetupActivity : AppCompatActivity() {

    private var lang = "tr"
    private var hasAccount = false

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)

        // Already logged in this session? skip — always show login if account exists
        setContentView(R.layout.activity_setup)

        val stepLang = findViewById<View>(R.id.stepLang)
        val stepAccount = findViewById<View>(R.id.stepAccount)
        val btnTr = findViewById<TextView>(R.id.btnTr)
        val btnEn = findViewById<TextView>(R.id.btnEn)
        val inputUser = findViewById<EditText>(R.id.inputUser)
        val inputPass = findViewById<EditText>(R.id.inputPass)
        val btnFinish = findViewById<TextView>(R.id.btnFinish)
        val btnReset = findViewById<TextView>(R.id.btnReset)
        val err = findViewById<TextView>(R.id.setupErr)
        val title = findViewById<TextView>(R.id.setupTitle)
        val sub = findViewById<TextView>(R.id.setupSub)

        hasAccount = Prefs.getUser(this).isNotBlank() && Prefs.getPass(this).isNotBlank()
        lang = Prefs.getLang(this)

        if (Prefs.isSetupDone(this) && hasAccount) {
            // Login only
            stepLang.visibility = View.GONE
            stepAccount.visibility = View.VISIBLE
            sub.text = if (lang == "en") "Sign in" else "Giriş yap"
            inputUser.setText(Prefs.getUser(this))
            btnFinish.text = if (lang == "en") "Sign in" else "Giriş Yap"
        } else {
            stepLang.visibility = View.VISIBLE
            stepAccount.visibility = View.GONE
            sub.text = "Dil seçin / Choose language"
        }

        fun selectLang(l: String) {
            lang = l
            Prefs.setLang(this, l)
            btnTr.setBackgroundResource(if (l == "tr") R.drawable.bg_choice_sel else R.drawable.bg_choice)
            btnEn.setBackgroundResource(if (l == "en") R.drawable.bg_choice_sel else R.drawable.bg_choice)
            stepLang.postDelayed({
                stepLang.visibility = View.GONE
                stepAccount.visibility = View.VISIBLE
                val exists = Prefs.getUser(this).isNotBlank()
                sub.text = if (l == "en") {
                    if (exists) "Sign in" else "Create account"
                } else {
                    if (exists) "Giriş yap" else "Hesap oluştur"
                }
                btnFinish.text = if (l == "en") {
                    if (exists) "Sign in" else "Create & continue"
                } else {
                    if (exists) "Giriş Yap" else "Oluştur ve devam"
                }
                if (exists) inputUser.setText(Prefs.getUser(this))
            }, 180)
        }

        btnTr.setOnClickListener { selectLang("tr") }
        btnEn.setOnClickListener { selectLang("en") }

        btnFinish.setOnClickListener {
            err.text = ""
            val u = inputUser.text?.toString()?.trim().orEmpty()
            val p = inputPass.text?.toString().orEmpty()
            if (u.length < 2) {
                err.text = if (lang == "en") "Username min 2 chars" else "Kullanıcı adı en az 2 karakter"
                return@setOnClickListener
            }
            if (p.length < 3) {
                err.text = if (lang == "en") "Password min 3 chars" else "Şifre en az 3 karakter"
                return@setOnClickListener
            }
            val savedUser = Prefs.getUser(this)
            val savedPass = Prefs.getPass(this)
            if (savedUser.isNotBlank()) {
                if (u != savedUser || p != savedPass) {
                    err.text = if (lang == "en") "Wrong username or password" else "Hatalı kullanıcı adı veya şifre"
                    return@setOnClickListener
                }
            } else {
                Prefs.setAccount(this, u, p)
                Toast.makeText(this, if (lang == "en") "Account created" else "Hesap oluşturuldu", Toast.LENGTH_SHORT).show()
            }
            Prefs.setSetupDone(this, true)
            startActivity(Intent(this, MainActivity::class.java))
            finish()
        }

        btnReset.setOnClickListener {
            Prefs.clearAccount(this)
            Prefs.setSetupDone(this, false)
            inputUser.setText("")
            inputPass.setText("")
            err.text = ""
            stepAccount.visibility = View.GONE
            stepLang.visibility = View.VISIBLE
            sub.text = "Dil seçin / Choose language"
            Toast.makeText(this, if (lang == "en") "Reset" else "Sıfırlandı", Toast.LENGTH_SHORT).show()
        }
    }
}
