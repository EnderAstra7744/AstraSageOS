package com.astrasage.os

import android.os.Bundle
import android.view.inputmethod.EditorInfo
import android.widget.EditText
import android.widget.ScrollView
import android.widget.TextView
import androidx.appcompat.app.AppCompatActivity

class TerminalActivity : AppCompatActivity() {
    private lateinit var output: TextView
    private lateinit var input: EditText
    private lateinit var scroll: ScrollView
    private lateinit var engine: AstEngine

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_terminal)
        output = findViewById(R.id.output)
        input = findViewById(R.id.input)
        scroll = findViewById(R.id.scroll)
        findViewById<android.view.View>(R.id.btnClose)?.setOnClickListener { finish() }

        engine = AstEngine(this) { s ->
            if (s == "\u0000CLEAR\n") {
                output.text = ""
                return@AstEngine
            }
            if (s == "\u0000EXIT\n") {
                finish()
                return@AstEngine
            }
            output.append(s)
            scroll.post { scroll.fullScroll(android.view.View.FOCUS_DOWN) }
        }
        engine.banner()
        output.append(engine.prompt())

        input.setOnEditorActionListener { _, actionId, _ ->
            if (actionId == EditorInfo.IME_ACTION_DONE || actionId == EditorInfo.IME_ACTION_GO || actionId == EditorInfo.IME_NULL) {
                val cmd = input.text?.toString().orEmpty()
                input.setText("")
                output.append(cmd + "\n")
                engine.run(cmd)
                if (!cmd.trim().equals("exit", true) && !cmd.trim().equals("quit", true)) {
                    output.append(engine.prompt())
                }
                true
            } else false
        }
    }
}
