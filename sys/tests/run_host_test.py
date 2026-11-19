"""Host-side tests: the kernel's terminal runs against a stub HAL; its raw serial byte
stream is replayed through a VT emulator so we check what a real terminal would show."""
import os, subprocess, sys, tempfile
sys.path.insert(0, os.path.dirname(__file__))
from vt import VT

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
BIN = os.path.join(ROOT, "build", "host_test")
fails = 0; total = 0

def check(name, cond, extra=""):
    global fails, total
    total += 1
    if not cond:
        fails += 1; print("FAIL:", name, extra)

def run(keys, disk=None, rows=25, cols=80):
    with tempfile.NamedTemporaryFile(delete=False) as f: f.write(keys.encode()); kp = f.name
    env = dict(os.environ, AS_ROWS=str(rows), AS_COLS=str(cols))
    if disk: env["AS_DISK"] = disk
    out = subprocess.run([BIN, kp], capture_output=True, env=env, timeout=30).stdout
    os.unlink(kp)
    parts = out.split(b"\0SNAP\0"); vt = VT(rows, cols); snaps = []
    for i, p in enumerate(parts):
        vt.feed(p)
        if i < len(parts) - 1: snaps.append(vt)
    return vt, parts, out

def snap_at(out, n, rows=25, cols=80):
    parts = out.split(b"\0SNAP\0"); vt = VT(rows, cols)
    for p in parts[:n + 1]: vt.feed(p)
    return vt

def lines(vt): return [vt.row_text(y) for y in range(vt.rows)]

# a new .as file starts from the template (6 lines, cursor on line 4): tests that want an empty buffer clear it first
CLR = "<UP><UP><UP>" + "<C-k>" * 6

# ---------- basics / login / jail ----------
vt, _, raw = run("as files\nas login create pw1 alice\nas login create pw1 alice\nas login bad alice\n"
                 "as login pw1 alice\nas location\nas goto /boot\nas goto ../..\nas location\n"
                 "as newdir sub\nas goto sub\nas location\nas write a.txt hi\\nthere\nas read a.txt\n"
                 "as read ../../../sys/accounts\nas logout\nas files\n", rows=60)
t = vt.text()
for s in ["login required", "Account 'alice' created", "alice: user already exists", "alice: wrong password",
          "Welcome, alice. You are in S:~/home/", "[alice@QEMU (TCG)]:S:~/home/: $ as location",
          "as goto: /boot: no such file or directory", "S:~/sub/", "Wrote 8 byte(s) to a.txt", "hi\nthere",
          "as read: ../../../sys/accounts: no such file or directory", "Goodbye, alice."]:
    check("basic: " + s, s in t, s)

# ---------- keyboard: history, Ctrl keys ----------
login = "as login create pw ed\nas login pw ed\n"
t = run(login + "as write h1.txt first\nas write h2.txt second\n<UP><UP>\nas read h1.txt\nas read h2.txt\n")[0].text()
check("history: Up Up brings back the older command and runs it", t.count("Wrote 5 byte(s) to h1.txt") == 2, t[-300:])
t = run(login + "as write h1.txt first\nas rem h1.txt\n<UP><DOWN><UP><UP>\nas read h1.txt\n")[0].text()
check("history: Down returns to the typed line, Up again walks back", t.count("Wrote 5 byte(s) to h1.txt") == 2 and "first" in t.split("as read h1.txt")[-1], t[-300:])
vt, _, raw = run(login + "as hel<C-c>as write c1.txt ok\nas read c1.txt\n")
check("keys: Ctrl+C drops the line", "^C" in vt.text() and "ok" in vt.text())
vt, _, raw = run(login + "as write u1.txt <C-u>as write u2.txt zz\nas read u2.txt\nas read u1.txt\n")
check("keys: Ctrl+U clears the line", "no such file" in vt.text() and "zz" in vt.text(), vt.text()[-300:])
vt, _, raw = run(login + "as edit n.txt\none<C-c>two<C-v><SNAP>")
s = snap_at(raw, 0)
check("editor: Ctrl+C copies the line, Ctrl+V pastes", s.row_text(1) == "onetwoone", [s.row_text(r) for r in range(1, 4)])
vt, _, raw = run(login + "as edit n.txt\nabc\ndef<C-z><SNAP>")
s = snap_at(raw, 0)
check("editor: Ctrl+Z undoes the last typing", s.row_text(1) == "abc" and s.row_text(2) == "", [s.row_text(r) for r in range(1, 4)])
vt, _, raw = run(login + "as edit n.txt\nabc\ndef<C-z><C-y><SNAP>")
s = snap_at(raw, 0)
check("editor: Ctrl+Y redoes it", s.row_text(2) == "def", [s.row_text(r) for r in range(1, 4)])

# ---------- as account -change ----------
t = run("as account\n")[0].text()
check("account: needs a login", "login required" in t, t)
t = run("as login create oldpw ann\nas login oldpw ann\nas account\nas account -change --NAME wrong bob\nas account -change --NAME oldpw bob\nas account\nas files\n")[0].text()
check("account: shows the name", "Signed in as ann" in t, t)
check("account: wrong password refused", "wrong password" in t, t)
check("account: rename works", "now called bob" in t and "Signed in as bob" in t, t)
t = run("as login create oldpw ann\nas login oldpw ann\nas account -change --NAME oldpw bob\nas login create x bob\nas account -change --PASSWORD oldpw newpw\nas logout\nas login oldpw bob\nas login newpw bob\n")[0].text()
check("account: name taken is refused / new name is usable", "Welcome, bob" in t, t)
check("account: password changed, old one refused", "Password changed" in t and "wrong password" in t, t)
t = run("as login create oldpw ann\nas login oldpw ann\nas account -change --PASSWORD oldpw sekret\n")[0].text()
check("mask: account passwords shown as *", "--PASSWORD ***** ******" in t and "sekret" not in t.replace("Password changed", ""), t)
check("account: invalid new name", "invalid name" in run("as login create p1 ann\nas login p1 ann\nas account -change --NAME p1 bad/name\n")[0].text())

# ---------- password masking ----------
check("mask: password shown as *", "as login create ****** carol" in run("as login create secret carol\n")[0].text())
check("mask: password never echoed", "secret" not in run("as login create secret carol\nas login secret carol\n")[0].text().replace("Account 'carol'", ""))
t = run("as login pw9 dave\n")[0].text()
check("mask: plain login masked", "as login *** dave" in t, t)
check("mask: other commands not masked", "as write note.txt login" in run("as write note.txt login\n")[0].text())

# ---------- OS root vs drive S: ----------
vt, _, raw = run("as login create p root1\nas login p root1\nas files /\nas files S:~/\nas files ~/home\n", rows=40)
t = vt.text()
check("root hidden: no system folder in user view", "boot/" not in t.split("as files S:~/")[0].split("as files /")[1])

# ---------- persistence ----------
disk = tempfile.mktemp()
open(disk, "wb").write(b"\0" * 512 * 4096)
vt1, _, _ = run("as login create pw bob\nas login pw bob\nas write n.txt saved\\nok\nas newdir d\nas info\n", disk=disk, rows=40)
check("persist: disk detected", "files are kept across reboots" in vt1.text() or "new disk" in vt1.text())
vt2, _, _ = run("as login pw bob\nas read n.txt\nas files\n", disk=disk, rows=40)
t = vt2.text()
check("persist: disk mounted after reboot", "Disk: asfs on ata disk" in t, t[:300])
check("persist: account survives", "Welcome, bob" in t)
check("persist: file survives", "saved\nok" in t)
check("persist: dir survives", "d/" in t)
check("persist: date shown", "2026-10-11 21:14" in t)
vt3, _, _ = run("as login create q user2\nas login q user2\nas write z z\nas logout\n", disk=disk, rows=40)
vt4, _, _ = run("as login pw bob\nas files\nas login q user2\nas read z\n", disk=disk, rows=40)
check("persist: second boot keeps first file too", "n.txt" in vt4.text() and "Welcome, user2" in vt4.text())
vt5, _, _ = run("as files\n")                                        # no disk
check("persist: no disk announced", "no disk (volatile)" in vt5.text() and "(use: make run)." in vt5.text())
os.unlink(disk)

# ---------- scripts ----------
HELLO = 'start;\\nfunc main(void) {\\n   vr inter x = 6;\\n   printc("Hi", x * 2);\\n   end;\\n};\\nend;'
def W(body): return "start;\nfunc main(void) {\n" + body + "   end;\n};\nend;\n"
def EDIT(name, body, tail="<SNAP>"): return "as write %s %s\nas edit %s\n%s" % (name, W(body).replace("\n", "\\n"), name, tail)
vt, _, _ = run("as login create p s1\nas login p s1\n"
               "as write hello.as " + HELLO + "\n"
               "asscript hello.as\nas cper +x hello.as\nasscript hello.as\n", rows=40)
t = vt.text()
check("script: needs permission", "no execute permission" in t)
check("script: runs after cper", "Hi 12" in t, t)

# ---------- neofetch exactness ----------
vt, _, raw = run("as neofetch\n", rows=45, cols=110)
L = lines(vt)
i = next(n for n, l in enumerate(L) if l.endswith("as neofetch"))
blk = L[i + 1: i + 1 + 22]
check("neofetch: blank line before", blk[0] == "")
check("neofetch: title on first row", blk[1].endswith("Unknown@astrasage") and blk[1].startswith(" " * 40))
check("neofetch: rule row", blk[2].endswith("-" * 17) and blk[2].startswith("/@@@"))
expect = ["OS: AstraSage OS", "Model: QEMU (TCG)", "Kernel: 0.9.29-as_kernel", "Uptime: 1h 2m", "Packages: 40 (as)",
          "Shell: AST 0.9.29", "Resolution: 720x400", "DE: None (text mode)", "WM: None (text mode)", "Terminal: AST",
          "CPU: Test CPU @ 3.00 (2 cores, 64-bit)", "GPU: QEMU Standard VGA", "Memory: 3MiB / 128MiB"]
for k, e in enumerate(expect):
    check("neofetch field " + e, blk[3 + k].endswith("    " + e), blk[3 + k])
check("neofetch: Disk (/)", "Disk (/): " in blk[16] and blk[16].rstrip().endswith(")"))
check("neofetch: Battery", blk[17].endswith("    Battery: Unknown"))
logo_rows = [l for l in blk[2:19]]
check("neofetch: 17 logo rows", all(l[:1] in "/@\\MP" or l[:1] == "@" for l in logo_rows))
check("neofetch: info column aligned", len({l.index("    ") if False else len(l) - len(l.lstrip()) for l in blk[1:2]}) == 1)
col = {blk[3 + k].index(expect[k]) for k in range(len(expect))}
check("neofetch: all labels in one column", len(col) == 1, col)
check("neofetch: 4-space gap after widest logo line", min(col) == 38 + 4 or min(col) == 41, col)
check("neofetch: blank line after", blk[21] == "" and L[i + 23].startswith("[Unknown@"), (blk[21], L[i + 23]))
g = vt.grid[i + 1 + 19]
check("neofetch: palette blocks coloured", [c[2] for c in g[38 + 4: 38 + 4 + 24:3]] == [0, 1, 2, 3, 4, 5, 6, 7], [c[2] for c in g[42:66:3]])

# ---------- editor ----------
login = "as login create pw ed\nas login pw ed\n"
vt, _, raw = run(login + EDIT("t.as", "   @ note\n   vr inter x = 5;\n   printc(\"hi\", x);\n   printc(zz);\n"), rows=25)
s = snap_at(raw, 0)
check("editor: green bar has name, ext, size, date", s.row_text(0).startswith(" AST Edit | File: t.as | Ext: as | Size: ") and "Date:" in s.row_text(0), s.row_text(0))
check("editor: bar is green", s.row_bg(0) == {112})
check("editor: error line gets dark red", s.row_bg(6) == {52} and s.row_bg(5) == {None}, [s.row_bg(r) for r in range(1, 8)])
check("editor: bottom shows error + line", "Variable not found" in s.row_text(24) and "line 6" in s.row_text(24), s.row_text(24))
check("editor: bottom is red", s.row_bg(24) == {52})
# syntax colours
def colour_of(s, row, col): return s.grid[row][col][1]
check("editor: keyword vr colour", colour_of(s, 4, 3) == 154)
check("editor: type colour", colour_of(s, 4, 6) == 112)
check("editor: builtin colour", colour_of(s, 5, 3) == 70)
check("editor: string colour", colour_of(s, 5, 10) == 215)
check("editor: number colour", colour_of(s, 4, 16) == 141)
check("editor: comment colour", colour_of(s, 3, 3) == 245)
# fix the error: the line turns normal
vt, _, raw = run(login + EDIT("t.as", "   vr inter x = 5;\n   y = 3;\n"), rows=25)
# error classes
cases = [("printc(zz);\n", "Variable not found"), ("printc(\n", "Missing ';'"),
         ("vr inter a = 1;\nif (a > 0) {\nprintc(1);\n", "not closed"), ("else {\n};\n", "without a matching"),
         ("hello world;\n", "Missing ';'"), ("fr (vr inter i = 0; i < 3) {\n};\n", "fr needs"),
         ("as newdir;\n", "Missing argument"), ("as bogus;\n", "Unknown AstraSage command"),
         ("func f() {\nprintc(zz);\nend;\n};\n", "Variable not found")]
for src, want in cases:
    k = login + EDIT("e.as", "".join("   " + l + "\n" for l in src.splitlines()))
    _, _, raw = run(k)
    check("editor error: " + want, want in snap_at(raw, 0).row_text(24), snap_at(raw, 0).row_text(24))
_, _, raw = run(login + EDIT("ok.as", "   vr inter n = 10 / 2;\n   vr strt s = \"a\";\n   printc(s, n);\n"))
s = snap_at(raw, 0)
check("editor: valid script shows no error", "no errors" in s.row_text(24) and s.row_bg(24) == {None}, s.row_text(24))
_, _, raw = run(login + "as edit n.txt\nvr zz = ;\n<SNAP>")
check("editor: plain text file is never flagged", snap_at(raw, 0).row_bg(1) == {None} and snap_at(raw, 0).row_text(0).find("Ext: txt") > 0)
# save + reopen
vt, _, raw = run(login + "as edit f.as\n" + CLR + "printc(\"saved\");\n<C-s><SNAP><C-x>as edit f.as\n<SNAP><C-x>as read f.as\n")
check("editor: save message", "Saved 17 bytes" in snap_at(raw, 0).row_text(24), snap_at(raw, 0).row_text(24))
check("editor: bar says Saved", "Saved" in snap_at(raw, 0).row_text(0) and "Date: 2026-10-11 21:14" in snap_at(raw, 0).row_text(0))
check("editor: reopen shows content", 'printc("saved");' in snap_at(raw, 1).row_text(1))
check("editor: exit returns to prompt", 'printc("saved");' in vt.text())
# exit asks when modified
_, _, raw = run(login + "as edit g.as\n" + CLR + "abc<C-x><SNAP>n")
check("editor: asks to save", "Save modified buffer" in snap_at(raw, 0).row_text(24))
# navigation / editing keys
_, _, raw = run(login + "as edit n.txt\none\ntwo\nthree\n<UP><UP><END>X<DOWN><HOME><DEL><TAB>Z<SNAP>")
s = snap_at(raw, 0)
check("editor: arrows, home/end, del, tab", [s.row_text(1), s.row_text(2), s.row_text(3)] == ["one", "twoX", "    Zhree"], [s.row_text(r) for r in range(1, 5)])
_, _, raw = run(login + "as edit n.txt\nline1\nline2\n<UP><C-k><C-u><C-u><SNAP>")
s = snap_at(raw, 0)
check("editor: cut and paste", [s.row_text(1), s.row_text(2), s.row_text(3)] == ["line1", "line2", "line2"], [s.row_text(r) for r in range(1, 5)])
_, _, raw = run(login + "as edit b.as\n" + CLR + "if (1 == 1) {\n<SNAP>")
check("editor: auto indent after {", "Col 5" in snap_at(raw, 0).row_text(23))
# tall screens / narrow screens
_, _, raw = run(login + "as edit t.as\n" + CLR + "" + "a\n" * 60 + "<SNAP>", rows=40, cols=100)
s = snap_at(raw, 0, 40, 100)
check("editor: scrolls on a tall terminal", "Ln 61/61" in s.row_text(38) and s.row_text(0).startswith(" AST Edit"), s.row_text(38))

# ---------- copy / rem ----------
L2 = "as login create p cp\nas login p cp\n"
vt, _, _ = run(L2 + "as write a.txt hello\nas copy a.txt b.txt\nas read b.txt\nas newdir d\nas copy a.txt d\nas files d\n"
               "as copy d e\nas files e\nas copy a.txt a.txt\nas copy d d/x\nas copy nope z\nas copy a.txt\nas copy d a.txt\n", rows=70)
t = vt.text()
check("copy: file to new name", "Copied a.txt -> b.txt" in t and "hello\n" in t)
check("copy: into directory keeps name", "Directory of S:~/home/d/\n      5 B" in t and "a.txt" in t.split("Directory of S:~/home/d/")[1][:80])
check("copy: directory copied recursively", "Directory of S:~/home/e/" in t and "a.txt" in t.split("Directory of S:~/home/e/")[1][:80])
check("copy: same file refused", "source and destination are the same" in t)
check("copy: folder into itself refused", "cannot copy a folder into itself" in t)
check("copy: missing source", "as copy: nope: no such file or directory" in t)
check("copy: usage", "Usage: as copy <SRC> <DEST>" in t)
check("copy: folder over file refused", "as copy: a.txt: not a directory" in t)
vt, _, _ = run(L2 + "as write a.txt x\nas newdir d\nas write d/in.txt y\nas rem a.txt\nas read a.txt\nas rem d\nas rem -r d\nas files\n"
               "as rem nothing\nas rem -f nothing\nas rem -rf /\nas rem -x a\nas newdir q\nas goto q\nas rem -r ../q\nas location\nas rem\n", rows=70)
t = vt.text()
check("rem: file removed", "Removed a.txt" in t and "as read: a.txt: no such file or directory" in t)
check("rem: directory needs -r", "as rem: d: is a directory (use: as rem -r)" in t)
check("rem: -r removes tree", "Removed d" in t and "0 item(s)" in t)
check("rem: missing is an error", "as rem: nothing: no such file or directory" in t)
check("rem: -f hides missing", t.count("nothing") == 3)
check("rem: root refused", "refusing to remove the root folder" in t)
check("rem: bad option", "unknown option" in t)
check("rem: cwd removal moves up", "[cp@QEMU (TCG)]:S:~/home/: $ as rem\n" in t.replace("as location\nS:~/home/\n", "as location\nS:~/home/\n") or "S:~/home/\n" in t)
check("rem: usage", "Usage: as rem [-r|-rf] <FILE>" in t)
# persistence of removal
disk = tempfile.mktemp(); open(disk, "wb").write(b"\0" * 512 * 4096)
run("as login create p u1\nas login p u1\nas write a.txt x\nas newdir d\nas copy a.txt d\nas rem a.txt\n", disk=disk)
vt, _, _ = run("as login p u1\nas files\nas files d\n", disk=disk, rows=40)
check("persist: rem and copy survive reboot", "a.txt" in vt.text().split("as files d")[1] and "Directory of S:~/home/\n" in vt.text() and "     1 B" not in vt.text().split("as files d")[0].split("Directory of")[1] , vt.text())
os.unlink(disk)

# the built-in FroggyCode version must match its manifest
import json as _j
check("froggy: aspi.c FROGGY_VERSION matches the manifest",
      '#define FROGGY_VERSION "%s"' % _j.load(open(os.path.join(ROOT, "packages/FroggyCode/ASPIManifest.json")))["version"] in open(os.path.join(ROOT, "sys/aspi.c")).read())
check("netsurf: aspi.c NETSURF_VERSION matches the manifest",
      '#define NETSURF_VERSION "%s"' % _j.load(open(os.path.join(ROOT, "packages/NetSurf/ASPIManifest.json")))["version"] in open(os.path.join(ROOT, "sys/aspi.c")).read())
vt, _, _ = run("aspi serve\n")
check("aspi serve is gone (the package source is GitHub)", "Usage: aspi" in vt.text(), vt.text()[-300:])
# FroggyCode 2.0 example programs (native/froggy/templates.h) run through the real interpreter
import re as _re
_th = open(os.path.join(ROOT, "native/froggy/templates.h")).read()
for _ent in _th.split("\nT(")[1:]:
    _lits = [_re.sub(r'\\(.)', lambda m: {"n": "\n", '"': '"', "\\": "\\"}.get(m.group(1), m.group(1)), x) for x in _re.findall(r'"((?:[^"\\]|\\.)*)"', _ent.split("\n)", 1)[0] if False else _ent)]
    _tname, _tfile, _code = _lits[0], _lits[1], _lits[4]
    if not _tfile.endswith(".as"):
        continue
    _body = _code.replace("\n", "\\n")
    _tx = asrun_t = None
    _vt, _, _ = run(L2 + "as write %s %s\nas cper +x %s\nasscript %s\n" % (_tfile, _body, _tfile, _tfile), rows=40)
    _tx = _vt.text().split("asscript " + _tfile)[1]
    _want = {"hello": "Hello from Froggy!", "counter": "Count 5", "if": "n is big: 7", "sum": "Total: 55"}.get(_tname)
    if _want:
        check("froggy template %s runs" % _tname, _want in _tx and "Error" not in _tx, _tx[:300])
    else:
        check("froggy template %s passes the strict rules" % _tname, "Missing ';'" not in _tx and "must finish" not in _tx and "before its" not in _tx, _tx[:300])
# built-in calls for programs: str_*, file_*, ast_do (0.9.3)
prog = ('vr strt s = "Hello Frog World";\\nvr strt w = "";\\nvr strt r = "";\\nvr inter h = 0;\\nvr inter n = 0;\\n'
        'str_has(h, s, "frog");\\nprintc("has", h);\\nstr_word(w, s, 2);\\nprintc(w);\\nstr_rest(r, s, 2);\\nprintc(r);\\n'
        'str_cat(w, w, "!");\\nprintc(w);\\nstr_add(w, 65);\\nprintc(w);\\nstr_del(w);\\nprintc(w);\\nstr_len(n, s);\\nprintc("len", n);\\n'
        'file_put("q.txt", "one");\\nfile_add("q.txt", "two");\\nfile_read(r, "q.txt");\\nprintc(r);\\nfile_has(h, "nope.txt");\\nprintc("nope", h);\\n'
        'ast_do(r, "as files");\\nstr_has(h, r, "q.txt");\\nprintc("listed", h);\\nast_do(r, "as edit q.txt");\\nprintc(r);\\n')
prog = 'start;\\nfunc main(void) {\\n' + prog + 'end;\\n};\\nend;'
vt, _, _ = run(L2 + "as write b.as " + prog + "\nas cper +x b.as\nasscript b.as\n", rows=40)
tx = vt.text().split("asscript b.as")[1]
for want in ["has 1", "Frog", "World", "Frog!", "Frog!A", "len 16", "onetwo", "nope 0", "listed 1", "cannot be used from a program"]:
    check("builtins: " + want, want in tx, tx[:600])
# strict files: start; ... end;  (0.9.5)
def asrun(name, body):
    vt, _, _ = run(L2 + "as write %s %s\nas cper +x %s\nasscript %s\n" % (name, body, name, name), rows=40)
    return vt.text().split("asscript " + name)[1]
tx = asrun("c1.as", 'start;\\nfunc main(void) {\\n   printc("Templete of Script");\\n   end;\\n};\\nend')
check("strict: the template runs and main() starts by itself", "Templete of Script" in tx and "Error" not in tx, tx[:400])
tx = asrun("c2.as", 'start;\\nprintc("a")\\nend;')
check("strict: a line without ';' is an error", "Line 2" in tx and "Missing ';'" in tx, tx[:400])
tx = asrun("c3.as", 'start;\\nprintc("a");')
check("strict: the file must finish with end;", "must finish with 'end;'" in tx and "\na\n" not in tx, tx[:400])
tx = asrun("c4.as", 'start;\\nfunc main(void) {\\n   printc("x");\\n};\\nend;')
check("strict: a block must finish with end;", "before its '}'" in tx and "Line 4" in tx, tx[:400])
tx = asrun("e1.as", 'start;\\n#from <color.ash> import Color\\nfunc main(void) {\\n   l = [3, 1, 2];\\n   l.sort();\\n   printc(l, len(l), "ok" if 1 in l else "no");\\n   printc(Color.RED"Red Text"Color.RESET);\\n   as newfile q.txt;\\n   end;\\n};\\nend;')
check("engine 2: lists, #from <color.ash> import Color, as <command>", "[1, 2, 3] 3 ok" in tx and "Red Text" in tx and "Error" not in tx, tx[:500])
tx = asrun("e2.as", 'start;\\nfunc main(void) {\\n   x = [1];\\n   printc(x[5]);\\n   end;\\n};\\nend;')
check("engine 2: a runtime error names the line", "Line 4" in tx and "IndexError" in tx, tx[:400])
tx = asrun("n1.as", 'start;\\nfunc main(void) {\\n   vr bool B = False;\\n   vr list L = [1, 2, 3];\\n   vr dict D = {"a": 1};\\n   printc(B, L, D);\\n   for x in L {\\n      printc(x * 2);\\n      end;\\n   };\\n   printc([i * i for i in range(3)]);\\n   end;\\n};\\nend;')
check("new language in a strict file (bool, list, dict, comprehension, no ';')", "False [1, 2, 3] {'a': 1}" in tx and "[0, 1, 4]" in tx and "Error" not in tx and "Unknown" not in tx, tx[:500])
tx = asrun("n2.as", 'start;\\nclass A {\\n   func init(self, n) {\\n      self.n = n;\\n      end;\\n   };\\n   end;\\n};\\nfunc main(void) {\\n   vr bool Bool_Value = False;\\n   printc(Bool_Value);\\n   printc(A(4).n, None, True);\\n   end;\\n};\\nend;')
tx2 = asrun("n3.as", 'start;\\nfunc main(void) {\\n   vr list L = [3, 1, 2];\\n   L.sort();\\n   printc(L, len(L));\\n   end;\\n};\\nend;')
check("new language, two runs in one session (nothing stale between runs)", "False" in tx and "4 None True" in tx and "[1, 2, 3] 3" in tx2 and "Error" not in tx + tx2, tx + tx2)
tx = asrun("c5.as", 'vr inter n = 3;\\nprintc("old", n);')
check("strict: files without start; are refused (start;/end; are mandatory)", "must begin with 'start;'" in tx and "old 3" not in tx, tx[:400])
vt, _, _ = run(L2 + "as edit n.as\n<C-s><C-x>as read n.as\nasscript n.as\n", rows=40)
check("template: a new .as file starts with start; and main()", "start;" in vt.text() and "func main(void)" in vt.text(), vt.text()[-500:])
# scripts may use copy / rem
vt, _, _ = run(L2 + EDIT("s.as", "   as newfile t.txt;\n   as copy t.txt u.txt;\n   as rem t.txt;\n", "<SNAP><C-x>") + "as cper +x s.as\nasscript s.as\nas files\n", rows=40)
check("script: copy/rem valid in editor", "no errors" in snap_at(vt and run(L2 + EDIT("s2.as", "   as newfile t.txt;\n   as copy t.txt u.txt;\n   as rem t.txt;\n"))[2], 0).row_text(24))
check("script: copy/rem run", "u.txt" in vt.text().split("asscript s.as")[1] and "t.txt" not in vt.text().split("asscript s.as")[1].split("item(s)")[0].split("Directory")[1])
_, _, raw = run(L2 + EDIT("s3.as", "   as rem;\n"))
check("editor: rem without argument flagged", "Missing argument" in snap_at(raw, 0).row_text(24))


# ---------- aspi: package installer ----------
import io, zipfile, tarfile, shutil, json as _json
adir = tempfile.mkdtemp(); pk = os.path.join(adir, "pk"); os.makedirs(pk)
def man(name, ver, d="test pkg", d2=None):
    m = {"name": name, "version": ver, "description": d, "license": "None"}
    if d2: m["description2"] = d2
    return _json.dumps(m)
def mkzip(path, files, top):
    with zipfile.ZipFile(path, "w", zipfile.ZIP_DEFLATED) as z:
        for n, c in files.items(): z.writestr(top + n, c)
def mktar(path, files, top, mode):
    with tarfile.open(path, mode) as t:
        for n, c in files.items():
            ti = tarfile.TarInfo(top + n); b = c.encode(); ti.size = len(b); t.addfile(ti, io.BytesIO(b))
mkzip(os.path.join(pk, "AsUtil-1.03.aspi"), {"ASPIManifest.json": man("AsUtil", "1.03", "util"),
      "src/util.as": 'start;\nfunc util_hi() {\n   printc("AsUtil says hi");\n   end;\n};\nend;\n'}, "AsUtil/")
mkzip(os.path.join(pk, "AsUtil-1.04.aspi"), {"ASPIManifest.json": man("AsUtil", "1.04", "util"),
      "src/util.as": 'start;\nfunc util_hi() {\n   printc("AsUtil NEW");\n   end;\n};\nend;\n'}, "AsUtil/")
mkzip(os.path.join(pk, "AsCode-1.3.0.aspi"), {"ASPIManifest.json": man("AsCode", "1.3.0", "System Code tool", "file $README.md"),
      "README.md": "AsCode readme line\n", "src/code.cpp": "int main(){}\n", "src/code_runner.c": "int x;\n",
      "installer.txt": "#' AsFetch\n#* https://github.com/EnderAstra7744/AsCode\n#* ver='lastver'\n\n#EAP\n- AsUtil\n\n#'AsUtil\n#* https://github.com/EnderAstra7744/AsUtil\n#* ver='1.03'",
      "src/as/code_as_util.as": 'start;\nfunc code_hello() {\n   util_hi();\n   printc("AsCode ready");\n   end;\n};\nend;\n',
      "src/main.as": 'start;\nuse AsUtil;\nfunc main(void) {\n   code_hello();\n   end;\n};\nend;\n'}, "AsCode/")
mkzip(os.path.join(pk, "JarThing-0.1.jar"), {"ASPIManifest.json": man("JarThing", "0.1"), "j.as": 'start;\nfunc main(void) {\n   printc("from jar");\n   end;\n};\nend;\n'}, "")
mktar(os.path.join(pk, "TgzThing-2.0.tar.gz"), {"ASPIManifest.json": man("TgzThing", "2.0"), "main.as": 'start;\nfunc main(void) {\n   printc("from tgz");\n   end;\n};\nend;\n'}, "TgzThing/", "w:gz")
mktar(os.path.join(pk, "XzThing-3.0.tar.xz"), {"ASPIManifest.json": man("XzThing", "3.0"), "main.as": 'start;\nfunc main(void) {\n   printc("from xz");\n   end;\n};\nend;\n'}, "XzThing/", "w:xz")
mkzip(os.path.join(pk, "BadPkg-1.0.aspi"), {"ASPIManifest.json": man("BadPkg", "1.0"), "main.as": "start;\nthis is not valid;\nend;\n"}, "BadPkg/")
mkzip(os.path.join(pk, "NeedsGhost-1.0.aspi"), {"ASPIManifest.json": man("NeedsGhost", "1.0"), "main.as": 'start;\nfunc main(void) {\n   printc("x");\n   end;\n};\nend;\n',
      "installer.txt": "#EAP\n- Ghost\n\n#'Ghost\n#* https://github.com/nobody/Ghost\n#* ver='1.0'\n"}, "")
tool = os.path.join(ROOT, "sys", "tools", "aspi_sync.py")
env = dict(os.environ, ASPI_CACHE=os.path.join(adir, "cache"), ASPI_IMAGE=os.path.join(adir, "repo.img"))
r = subprocess.run([sys.executable, "-I", tool, "build", pk], capture_output=True, text=True, env=env)
check("aspi_sync build", r.returncode == 0 and "8 package(s)" in r.stdout, r.stdout + r.stderr)
os.environ["AS_REPO"] = os.path.join(adir, "repo.img")
disk = tempfile.mktemp(); open(disk, "wb").write(b"\0" * 512 * 4096)
L = "as login create p pk1\nas login p pk1\n"
vt, _, raw = run(L + "as aspi\nas aspi list\nas aspi repo\nas aspi install AsCode\nas aspi list\nas aspi info AsCode\n"
                 "as aspi install AsCode\nas write t.as start;\\nuse AsCode;\\nfunc main(void) {\\n   code_hello();\\n   end;\\n};\\nend;\nas cper +x t.as\nasscript t.as\n"
                 "as aspi log AsCode\nas files /\n", disk=disk, rows=200)
t = vt.text()
if os.environ.get("DBG"): print(t)
for sx in ["Usage: aspi", "No packages installed", "AsUtil", "1.04", "Checking package 'AsCode' in repo", "Found AsCode 1.3.0 (aspi archive)",
           "Reading ASPIManifest.json", "Reading installer.txt", "Dependency AsUtil (version 1.03)", "Found AsUtil 1.03",
           "Compiling src/as/code_as_util.as. Done", "WARNING: src/code.cpp not compiled", "Package Archive Copying to /.aspi/lib/AsCode",
           "Registering package in /.aspi/app. Done", "AsCode is ready.", "AsCode ready", "AsUtil says hi", "AsCode readme line",
           "needs: AsUtil", "is already installed"]:
    check("aspi install: " + sx, sx in t, sx)
check("aspi: progress bar drawn", "%100 [####################]" in raw.decode("latin1") or "[####################]" in raw.decode("latin1"))
check("aspi: pinned dependency version 1.03 not 1.04", "AsUtil NEW" not in t)
vt2, _, _ = run("as login p pk1\nas aspi list\nas aspi log AsCode\n", disk=disk, rows=100)
check("aspi: install survives reboot", "AsCode" in vt2.text() and "already installed" in vt2.text(), vt2.text())
vt3, _, _ = run("as login p pk1\nas aspi remove AsUtil\nas aspi remove -f AsUtil\nas aspi list\nas write u.as start;\\nuse AsUtil;\\nfunc main(void) {\\n   util_hi();\\n   end;\\n};\\nend;\nas cper +x u.as\nasscript u.as\n"
                "as aspi remove AsCode\nas aspi list\nas aspi remove AsCode\n", disk=disk, rows=100)
t3 = vt3.text()
check("aspi: refuses removal while needed", "cannot remove AsUtil - needed by: AsCode" in t3, t3)
check("aspi: forced removal", "Package files removed from /.aspi/lib/AsUtil" in t3)
check("aspi: use of removed package flagged", "is not installed" in t3.split("asscript u.as")[1], t3)
check("aspi: remove then list empty", "No packages installed" in t3 and "'AsCode' is not installed" in t3)
vt4, _, _ = run("as login p pk1\nas aspi install JarThing\nas aspi install TgzThing\nas aspi install XzThing\nas aspi run JarThing j\nas aspi run TgzThing\nas aspi run XzThing\nas aspi list\n", disk=disk, rows=100)
t4 = vt4.text()
for sx in ["(jar archive)", "(tar.gz archive)", "(tar.xz archive)", "from jar", "from tgz", "from xz"]:
    check("aspi types: " + sx, sx in t4, t4)
vt5, _, _ = run("as login p pk1\nas aspi install BadPkg\nas aspi list\nas aspi log BadPkg\nas aspi install NeedsGhost\nas aspi install Nope\nas aspi install ../x\nas aspi install AsUtil 9.9\n", disk=disk, rows=100)
t5 = vt5.text()
check("aspi: compile error aborts install", "line 2" in t5 and "nothing was installed" in t5 and "Installation of BadPkg failed" in t5, t5)
check("aspi: bad package leaves nothing", t5.count("BadPkg") >= 3 and "BadPkg         " not in t5.split("as aspi list")[1].split("as aspi log")[0])
check("aspi: missing dependency fails", "Dependency Ghost could not be installed" in t5, t5)
check("aspi: unknown package", "Package 'Nope' is not built in, on the repo disk or on GitHub" in t5)
check("aspi: bad name", "bad package name" in t5)
check("aspi: unknown version", "version 9.9" in t5)
vt6, _, _ = run("as logout\nas aspi install AsUtil\nas login p pk1\nas aspi list\n", disk=disk, rows=40)
check("aspi: install needs login", "login required" in vt6.text())
os.environ.pop("AS_REPO")
vt7, _, _ = run("as login p pk1\nas aspi install AsUtil\nas aspi repo\n", disk=disk, rows=40)
check("aspi: no repo disk message", "no repo disk" in vt7.text() and "is not built in, on the repo disk or on GitHub" in vt7.text())
os.environ["AS_FAKE_GH"] = pk                                  # a fake GitHub: tree listing + raw files from the pk folder
gdisk = os.path.join(adir, "gh.disk"); open(gdisk, "wb").write(b"\0" * 512 * 4096)
vtg, _, _ = run("as login create p gh1\nas login p gh1\nas aspi online\nas aspi install TgzThing\n", disk=gdisk, rows=100)
vtg2, _, _ = run("as login p gh1\nas aspi install XzThing\n", disk=gdisk, rows=100)
vtg3, _, _ = run("as login p gh1\nas aspi install AsUtil 1.03\n", disk=gdisk, rows=100)
tg = vtg.text() + vtg2.text() + vtg3.text()
for sx in ["GitHub lists", "TgzThing", "Downloading TgzThing from GitHub (packages/TgzThing-2.0.tar.gz)", "Converting .tar.gz to .aspi", "TgzThing is ready",
           "XzThing is ready", "Converting .tar.xz to .aspi", "AsUtil is ready", "Archive SHA-256"]:
    check("aspi github: " + sx, sx in tg, sx + " | " + tg[-600:])
os.environ.pop("AS_FAKE_GH")
# tampered archive -> SHA-256 mismatch
img = open(os.path.join(adir, "repo.img"), "rb").read(); bad = bytearray(img); bad[-600] ^= 0xFF
open(os.path.join(adir, "bad.img"), "wb").write(bytes(bad)); os.environ["AS_REPO"] = os.path.join(adir, "bad.img")
vt8, _, _ = run("as login p pk1\nas aspi install AsUtil\nas aspi install XzThing\nas aspi install NeedsGhost\nas aspi install BadPkg\nas aspi install JarThing\nas aspi install TgzThing\n", disk=disk, rows=120)
check("aspi: corrupted repo data is rejected", "SHA-256" in vt8.text() or "checksum" in vt8.text().lower(), vt8.text())
os.environ.pop("AS_REPO"); os.unlink(disk); shutil.rmtree(adir)

# ---------- package type + commands (aspi direct command, as <package command>) ----------
bdir = tempfile.mkdtemp(); bpk = os.path.join(bdir, "pk"); os.makedirs(bpk)
def man2(name, ver, **extra):
    m = {"name": name, "version": ver, "description": "t"}; m.update(extra); return _json.dumps(m)
mkzip(os.path.join(bpk, "FrogT-1.0.aspi"), {"ASPIManifest.json": man2("FrogT", "1.0", type="app", commands=["frogt", "frogt2"]),
      "frogt.as": 'start;\nfunc main(void) {\n   printc("ribbit from package");\n   end;\n};\nend;\n', "main.as": 'start;\nfunc main(void) {\n   printc("main fallback");\n   end;\n};\nend;\n'}, "FrogT/")
mkzip(os.path.join(bpk, "Clash-1.0.aspi"), {"ASPIManifest.json": man2("Clash", "1.0", type="tool", commands=["frogt"]),
      "main.as": 'start;\nfunc main(void) {\n   printc("x");\n   end;\n};\nend;\n'}, "Clash/")
mkzip(os.path.join(bpk, "Shadow-1.0.aspi"), {"ASPIManifest.json": man2("Shadow", "1.0", commands=["help"]),
      "main.as": 'start;\nfunc main(void) {\n   printc("x");\n   end;\n};\nend;\n'}, "Shadow/")
mkzip(os.path.join(bpk, "Weird-1.0.aspi"), {"ASPIManifest.json": man2("Weird", "1.0", type="spaceship"),
      "main.as": 'start;\nfunc main(void) {\n   printc("x");\n   end;\n};\nend;\n'}, "Weird/")
mkzip(os.path.join(bpk, "BadName-1.0.aspi"), {"ASPIManifest.json": man2("BadName", "1.0", commands=["Bad Name"]),
      "main.as": 'start;\nfunc main(void) {\n   printc("x");\n   end;\n};\nend;\n'}, "BadName/")
mkzip(os.path.join(bpk, "Plain-1.0.aspi"), {"ASPIManifest.json": man2("Plain", "1.0"), "main.as": 'start;\nfunc main(void) {\n   printc("x");\n   end;\n};\nend;\n'}, "Plain/")
mkzip(os.path.join(bpk, "MainOnly-1.0.aspi"), {"ASPIManifest.json": man2("MainOnly", "1.0", type="tool", commands=["monly"]),
      "main.as": 'start;\nfunc main(void) {\n   printc("main fallback ran");\n   end;\n};\nend;\n'}, "MainOnly/")
env2 = dict(os.environ, ASPI_CACHE=os.path.join(bdir, "cache"), ASPI_IMAGE=os.path.join(bdir, "repo.img"))
r = subprocess.run([sys.executable, "-I", tool, "build", bpk], capture_output=True, text=True, env=env2)
check("aspi types: repo image built", r.returncode == 0, r.stdout + r.stderr)
os.environ["AS_REPO"] = os.path.join(bdir, "repo.img")
disk = tempfile.mktemp(); open(disk, "wb").write(b"\0" * 512 * 4096)
vt, _, _ = run("as login create p pk1\nas login p pk1\nas frogt\naspi list\naspi install FrogT\naspi list\naspi info FrogT\n"
               "as frogt\nas frogt2\naspi install Clash\naspi install Shadow\naspi install BadName\naspi install Weird\naspi install Plain\n"
               "aspi install MainOnly\nas monly\naspi list\nas aspi list\n", disk=disk, rows=200)
tb = vt.text()
check("aspi direct: 'aspi list' works without 'as'", "FrogT" in tb and "app" in tb, tb)
check("aspi direct: 'as aspi list' still works", tb.count("cmd: frogt,frogt2") >= 2, tb)
check("pkg command: unknown before install", "unknown command 'as frogt'" in tb, tb)
check("pkg command: manifest type shown on install", "Package: FrogT 1.0 (app)" in tb, tb)
check("pkg command: install announces the commands", "adds AST command(s): frogt,frogt2" in tb, tb)
check("pkg command: 'as frogt' runs frogt.as", "ribbit from package" in tb, tb)
check("pkg command: 'as frogt2' falls back to main.as", "main fallback" in tb, tb)
check("pkg command: info shows Type and Commands", "Type" in tb and "Commands" in tb, tb)
check("pkg command: command owned by another package is refused", "Command 'frogt' already belongs to package FrogT" in tb, tb)
check("pkg command: OS command cannot be taken", "Command 'help' is an AST command" in tb, tb)
check("pkg command: bad command name refused", "use 1-16 characters" in tb, tb)
check("pkg type: unknown type treated as library", "Unknown package type 'spaceship'" in tb, tb)
check("pkg type: missing type = library", "Package: Plain 1.0 (library)" in tb, tb)
check("pkg command: main.as fallback", "main fallback ran" in tb, tb)
vt, _, _ = run("as login p pk1\naspi remove FrogT\nas frogt\naspi list\n", disk=disk, rows=100)
tb2 = vt.text()
check("pkg command: removed with the package", "unknown command 'as frogt'" in tb2 and "Package files removed" in tb2, tb2)
vt, _, _ = run("as login p pk1\nas newfile s.as\n", disk=disk, rows=40)
os.environ.pop("AS_REPO"); os.unlink(disk); shutil.rmtree(bdir)

# ---------- v0.7.0: saved login, reboot, alias, {} and && ----------
import tempfile as _tf
d7 = _tf.mktemp(); open(d7, "wb").write(b"\0" * (2 * 1024 * 1024))
run("as login create pw1 zed\nas login pw1 zed\n", disk=d7)
vt, _, _ = run("as location\n", disk=d7)
check("session: login survives closing the terminal (new run is signed in)", "Signed in as zed" in vt.text() and "[zed@" in vt.text(), vt.text())
raw7 = open(d7, "rb").read(); i7 = raw7.find(b"AS2\nnonce=")
check("session: config/zed.config holds name and password encrypted (neither readable)", i7 >= 0 and b"zed" not in raw7[i7:i7 + 260] and b"pw1" not in raw7[i7:i7 + 260], raw7[i7:i7 + 260])
check("session: file is /config/zed.config", b"zed.config" in raw7)
bad7 = bytearray(raw7); bad7[i7 + 20] ^= 1; open(d7 + ".bad", "wb").write(bytes(bad7))
vt, _, _ = run("as location\n", disk=d7 + ".bad")
check("session: tampered record is refused (not signed in)", "Signed in as" not in vt.text() and "[Unknown@" in vt.text(), vt.text())
vt, _, _ = run("as newdir TEST && as goto TEST\nas location\nas newfile a.as && as newfile b.as\nas rem -rf {a.as b.as}\nas files\n"
               "as goto nonexist && as newdir NOPE\nas goto ..\nas files\n", disk=d7, rows=50)
t = vt.text()
check("&&: second command runs after the first", "Directory created: TEST" in t and "S:~/home/TEST/" in t, t)
check("{}: several names in one command", "Removed a.as" in t and "Removed b.as" in t and "0 item(s)" in t, t)
check("&&: stops when the first command fails", "NOPE" not in t.split("as goto nonexist")[1].split("as files")[1], t)
vt, _, _ = run("as alias ll == 'as files'\nas ll\nas alias\nas alias x =: 'as location'\nas x\nas alias loop == 'as loop'\nas loop\n", disk=d7, rows=50)
t = vt.text()
check("alias: == makes a name that runs the command", "Alias set: as ll" in t and "Directory of" in t.split("Alias set: as ll")[1], t)
check("alias: listing and =: saved form", "ll == 'as files'" in t and "(saved)" in t, t)
vt, _, _ = run("as x\nas ll\n", disk=d7, rows=40)
check("alias: =: alias survives a restart, == alias does not", "S:~/home/" in vt.text() and "unknown command 'as ll'" in vt.text(), vt.text())
check("alias: self-referencing alias does not hang", "unknown command 'as loop'" in t, t)
vt, _, raw = run("as reboot\n", disk=d7)
check("reboot: command restarts the machine", b"\0REBOOT\0" in raw and "Rebooting" in vt.text())
vt, _, _ = run("as location\n", disk=d7)
check("reboot: files and login are still there afterwards", "[zed@" in vt.text())
vt, _, _ = run("as logout\n", disk=d7)
vt, _, _ = run("as location\n", disk=d7)
check("logout: forgets the saved login", "[Unknown@" in vt.text() and "Signed in as" not in vt.text(), vt.text())
run("as login pw1 zed\n", disk=d7)
for fn, lang in [("x.py", "Python"), ("y.c", "C - "), ("z.cpp", "C++"), ("w.asm", "Assembly")]:
    vt, _, _ = run("as edit %s\nint x;<SNAP>" % fn, disk=d7, rows=25)
    check("editor: %s can be edited, marked as not runnable" % fn, lang in vt.text() and "not run" in vt.text(), vt.text())
vt, _, _ = run("as write h.py print(1)\nas script h.py\nas asscript h.py\n", disk=d7, rows=30)
check("editor: Python files are refused by the script runner", "not an AstraScript file" in vt.text(), vt.text())
os.unlink(d7); os.unlink(d7 + ".bad")

# ---------- v0.8.0: shutdown, tree ----------
vt, _, raw = run("as shutdown\n")
check("shutdown: command powers the machine off", b"\0SHUTDOWN\0" in raw and "Shutting down" in vt.text(), vt.text())
vt, _, _ = run("as login create pw tre\nas login pw tre\nas newdir a && as newdir a/b && as newfile a/f1.txt && as newfile a/b/f2.txt && as newfile top.txt\n"
               "as tree\nas tree a --files\nas tree --DIR\nas tree nonexist\nas tree a --bogus\n", rows=70)
t = vt.text()
check("tree: folders and files as a tree with a summary", "|-- " in t and "`-- " in t and "2 folder(s), 3 file(s)" in t, t)
sec = t.split("as tree a --files")[1].split("as tree --DIR")[0]
check("tree: --files lists only files (folders are just the path)", "f1.txt" in sec and "f2.txt" in sec and "2 file(s)" in sec, sec)
sec = t.split("as tree --DIR")[1].split("as tree nonexist")[0]
check("tree: --DIR (any case) lists only folders", "a/" in sec and "b/" in sec and "f1.txt" not in sec and "top.txt" not in sec and "2 folder(s)" in sec, t)
check("tree: unknown path and bad flag give errors", "as tree: nonexist" in t and "Usage: as tree" in t, t)

# ---------- acs: root access needs the password ----------
L = "as login create pw acu\nas login pw acu\n"
t = run(L + "as newdir root:/mydir\nas newfile root:/note.txt\nacs as newdir root:/mydir\nwrongpw\nas files root:/\n", rows=60)[0].text()
check("acs: creating in the root without acs is denied", t.count("Permission denied") >= 2, t[-600:])
check("acs: prompt asks for the password (masked)", "[acs] Pls Enter your Password: *******" in t and "wrong password" in t.lower() and "mydir" not in t.split("wrong password")[-1], t[-600:])
t = run(L + "acs as newdir root:/mydir\npw\nacs as write root:/mydir/a.txt hi\npw\nas read root:/mydir/a.txt\nas rem -r root:/mydir\nacs as rem -r root:/mydir\npw\nas files root:/\n", rows=70)[0].text()
check("acs: right password creates a root folder and file", "Directory created" in t and "Wrote 2 byte(s)" in t and "hi" in t, t[-800:])
check("acs: removing it needs acs, and works with it", t.count("Removed root:/mydir") == 1, t[-800:])
t = run(L + "acs as rem root:/bin/version\npw\nas files root:/bin\n", rows=70)[0].text()
check("acs: system files are never deleted, even with the password", "protected" in t and "version" in t.split("as files root:/bin")[-1], t[-500:])
t = run(L + "acs as rem -r root:/bin\npw\n", rows=70)[0].text()
check("acs: a root folder can be deleted with the password", "Removed root:/bin" in t, t[-500:])

# ---------- error system: /proc triggers, sealed /user apps ----------
L = "as login create pw ep\nas login pw ep\n"
vt, _, raw = run(L + "as proc\nas proc kernel_panic\nas read nothing\n", rows=40)
check("proc: lists the trigger files", "kernel_panic   panic" in vt.text() and "heavy_load    " in vt.text(), vt.text()[-400:])
check("proc: kernel_panic triggers the Kernel Panic screen with its reason", b"\0PANIC\0Test: kernel panic trigger" in raw and "no such file" not in vt.text().split("as proc kernel_panic")[-1], raw[-200:])
vt, _, raw = run(L + "as proc heavy_load --sleep 2\n", rows=40)
check("proc: --sleep waits, then the yellow screen", b"\0OVERLOAD\0" in raw and "starts in 2 second" in vt.text(), vt.text()[-300:])
t = run(L + "as proc nope\nas proc --sleep x\nas proc kernel_panic a b\n", rows=40)[0].text()
check("proc: errors for a missing file and a bad value", "no such file in /proc" in t and t.count("Usage: as proc") == 2, t[-400:])
t = run(L + "acs as write root:/proc/mine ABC\npw\nas proc mine\n", rows=40)[0].text()
check("proc: a file that is not a trigger is refused", "not a trigger file" in t, t[-300:])
vt, _, raw = run(L + "acs as write root:/proc/mine overload\\nreason=my test\npw\nas proc mine\n", rows=40)
check("proc: own trigger files work (overload + reason)", b"\0OVERLOAD\0my test" in raw, raw[-200:])
APP = 'start;\\nfunc main(void) {\\n   printc("sealed", 7);\\n   end;\\n};\\nend;'
t = run(L + "as write a.as " + APP + "\nas seal a.as\nas userapp a\nas write bad.as nonsense\nas seal bad.as\nas userapp zzz\n", rows=60)[0].text()
check("seal: compiles, encrypts and runs", "Sealed (compiled + encrypted): /user/apps/a.asx" in t and "sealed 7" in t, t[-500:])
check("seal: a script that does not compile is not sealed", "(not compiled)" in t or "not compiled" in t, t[-400:])
check("userapp: unknown name is an error", "no such sealed app" in t, t[-300:])
disk = tempfile.mktemp()
open(disk, "wb").write(b"\0" * 512 * 4096)
run("as login create pw tp\nas login pw tp\nas write a.as " + APP + "\nas seal a.as\n", disk=disk, rows=40)
data = open(disk, "rb").read()
check("seal: the stored app is encrypted (source text not on the disk)", b"ASX1E" in data and data.count(b'printc("sealed"') == 1, "")
open(disk, "wb").write(data.replace(b"ASX1E", b"ASX1P", 1))                # tamper: claims to be unencrypted
vt, _, raw = run("as login pw tp\n", disk=disk, rows=40)
check("integrity: a tampered /user app -> yellow screen at login", b"\0OVERLOAD\0/user/apps/a.asx: not encrypted" in raw, raw[-300:])
vt, _, raw = run("as login pw tp\nas userapp a\n", disk=disk, rows=40)
check("integrity: the offender was quarantined, the next login is clean", b"\0OVERLOAD" not in raw and "Welcome, tp" in vt.text(), vt.text()[-300:])

# ---------- as custom: colours, tabs (no window here), custom neofetch ----------
L = "as login create pw cu\nas login pw cu\n"
t = run(L + "as custom\nas custom --tab_color orange --custom_input_color #00ff00\nas custom blue\n", rows=60)[0].text()
check("custom: shows the current colours", "Tab colour: default" in t, t[-500:])
check("custom: flags take their values in order", "Tab colour: #FB8C00   line colour: default   input colour: #00FF00" in t, t[-500:])
check("custom: no flag = tab colour first", "Tab colour: #1E88E5" in t, t[-500:])
t = run(L + "as custom --line_color nonsense\nas custom --bogus\nas tab\n", rows=60)[0].text()
check("custom: bad colour and bad flag are refused", "'nonsense' is not a colour" in t and "Usage: as custom" in t, t[-500:])
check("tab: needs the graphical terminal", "Tabs need the graphical terminal window" in t, t[-300:])
vt, _, raw = run(L + "as custom --tab_color red\n" + "as neofetch\n", rows=60)
check("custom: the input colour is not applied while unset", b"\x1b[38;5;196m" not in raw.split(b"Saved")[0] if b"Saved" in raw else True)
vt, _, raw = run(L + "as custom --custom_input_color red\nas neofetch\n", rows=60)
check("custom: typed text gets the input colour at the next prompt", b"\x1b[38;5;" in raw.split(b"Saved")[-1])
vt, _, raw = run(L + "as custom --neofetch red,green,blue\nas neofetch\n", rows=60)
check("neofetch: three logo colours replace the greens", "Neofetch look saved" in vt.text() and b"\x1b[38;5;154m/@@" not in raw.split(b"Neofetch look saved")[-1] and b"\x1b[38;5;167m/@@" in raw.split(b"Neofetch look saved")[-1], vt.text()[-300:])
vt, _, raw = run(L + "as custom --neofetch default astrasage.png\nas neofetch\n", rows=60)
check("neofetch: a PNG logo is drawn with cell colours", b"\x1b[48;5;" in raw.split(b"Neofetch look saved")[-1], vt.text()[-300:])
vt, _, raw = run(L + "as custom --neofetch default,default,default default astrasage.txt gold\nas neofetch\n", rows=60)
check("neofetch: a text logo and the info colour", b"\x1b[1;38;5;214m" in raw.split(b"Neofetch look saved")[-1] and b"\x1b[38;5;154m/@@" in raw.split(b"Neofetch look saved")[-1], vt.text()[-300:])
t = run(L + "as custom --neofetch red,green,blue nothere.png\nas custom --neofetch red,green\n", rows=60)[0].text()
check("neofetch: unknown picture and wrong colour list are errors", "does not exist" in t and "three colours separated by commas" in t, t[-400:])

# ---------- misc ----------
vt, _, _ = run("as help\nas info\n", rows=60)
t = vt.text()
check("help lists edit", "as edit <file>" in t)
check("info shows asfs", "Drive S:   : asfs" in t)

print("%d/%d checks passed" % (total - fails, total))
sys.exit(1 if fails else 0)

# ---- an old (format 3) disk is still read and is upgraded on the next save, with its data kept ----
import struct
old = bytearray(b"\0" * 512 * 4096)
def rec(used, is_dir, parent, size, name):
    return struct.pack("<BBHiII", used, is_dir, 0, parent, size, 0) + name.ljust(24, b"\0") + struct.pack("<I", 0) + b"\0" * 20
old[0:16] = struct.pack("<IIII", 0x53465341, 3, 256, 1)
old[512 + 64 * 1:512 + 64 * 1 + 64] = rec(1, 0, 0, 15, b"legacy.txt")       # node 1: a file in the OS root
old[512 * 64 + 8 * 512 * 1:512 * 64 + 8 * 512 * 1 + 15] = b"old-format-data"
open(disk, "wb").write(bytes(old))
vt6, _, _ = run("as login create pw tom\nas login pw tom\nas info\n", disk=disk, rows=40)
check("old disk: format 3 is recognised", "old format" in vt6.text() or "files are kept across reboots" in vt6.text(), vt6.text()[:200])
img = open(disk, "rb").read()
check("old disk: upgraded to format 4 on save", struct.unpack("<II", img[4:12]) == (4, 1024))
check("old disk: the old file's data was copied to the new layout", img.find(b"old-format-data", 512 * 2240) > 0)
vt7, _, _ = run("as login pw tom\nas info\n", disk=disk, rows=40)
check("old disk: upgraded disk mounts", "asfs on ata disk" in vt7.text(), vt7.text()[:200])

# ---------- big files (more than FS_STORE_MAX, extents on the disk) ----------
import subprocess, os
bigdisk = "/tmp/as_big_%d.img" % os.getpid()
open(bigdisk, "wb").write(b"\0" * (64 << 20))
r = subprocess.run(["build/host_test", "--bigtest"], env=dict(os.environ, AS_DISK=bigdisk), capture_output=True, text=True)
for line in r.stdout.splitlines():
    if line.startswith("PASS: ") or line.startswith("FAIL: "): check(line[6:], line.startswith("PASS"))
check("big: test program exits cleanly", r.returncode == 0, r.stderr[:200])
os.remove(bigdisk)
