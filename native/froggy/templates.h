/*
 * Example programs Froggy can create. Written in the strict AstraScript layout (start; ... end;, ';' on every line, 'end;' in every block).
 * The host test (sys/tests/run_host_test.py) pulls the T(...) lines out of this file and runs each .as example through the real interpreter.
 *   T(name, file, description in Turkish, description in English, code)
 */
#ifndef FG_TEMPLATES_H
#define FG_TEMPLATES_H

#define T(name, file, dtr, den, code) { name, file, dtr, den, code },
typedef struct { const char *name, *file, *dtr, *den, *code; } Tpl;

static const Tpl TPLS[] = {
T("hello", "hello.as", "Ekrana yazı yazan en küçük program", "the smallest program: prints a line",
  "start;\nfunc main(void) {\n   printc(\"Hello from Froggy!\");\n   end;\n};\nend;\n")
T("counter", "counter.as", "1'den 5'e sayan döngü", "a loop that counts from 1 to 5",
  "start;\nvr inter i = 1;\nwhle (i < 6) {\n   printc(\"Count\", i);\n   i = i + 1;\n   end;\n};\nend;\n")
T("window", "window.as", "Pencere açan program (q ile kapanır)", "opens a window (q closes it)",
  "start;\nvr inter k = 0;\nfunc main(void) {\n   gui_open(\"Window\", 240, 110);\n   gui_clear(\"#DDF1E0\");\n   gui_text(20, 24, \"Hello from AstraScript!\", \"#1F3D2B\");\n   gui_text(20, 64, \"Press q to close\", \"#2E9A55\");\n   whle (k != 113) {\n      gui_wait(k, 500);\n      end;\n   };\n   gui_close();\n   end;\n};\nend;\n")
T("if", "check.as", "Koşul (if) örneği", "an if example",
  "start;\nvr inter n = 7;\nfunc main(void) {\n   if (n > 5) {\n      printc(\"n is big:\", n);\n      end;\n   };\n   end;\n};\nend;\n")
T("sum", "sum.as", "1'den 10'a kadar toplayan program", "adds the numbers 1 to 10",
  "start;\nvr inter i = 1;\nvr inter total = 0;\nfunc main(void) {\n   whle (i < 11) {\n      total = total + i;\n      i = i + 1;\n      end;\n   };\n   printc(\"Total:\", total);\n   end;\n};\nend;\n")
T("notes", "notes.txt", "Düz not dosyası", "a plain notes file",
  "My notes\n- learn AstraScript\n- say hi to Froggy\n")
};

#define NTPLS ((int)(sizeof TPLS / sizeof TPLS[0]))

#endif
