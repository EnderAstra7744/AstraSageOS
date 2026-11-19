"""aspi_convertor.cpp against Python's tarfile/zipfile: names, contents, CRC, top-folder removal, refusals."""
import io, os, subprocess, sys, tarfile, tempfile, zipfile
ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
B = os.path.join(ROOT, "build"); os.makedirs(B, exist_ok=True)
inc = ["-I" + os.path.join(ROOT, d) for d in ("kernel", "terminal", "font", "gui")]
subprocess.run(["g++", "-DHOST_TEST", "-Wall", "-Wextra", "-std=gnu++17", "-fno-exceptions", "-fno-rtti", "-fno-threadsafe-statics", "-fno-use-cxa-atexit"] + inc +
               ["-c", os.path.join(ROOT, "sys/aspi_convertor.cpp"), "-o", B + "/conv_t.o"], check=True)
exe = B + "/convertor_host"
subprocess.run(["gcc", "-DHOST_TEST", "-O1", "-std=gnu11", "-Wno-unused-function"] + inc + ["-o", exe, os.path.join(ROOT, "sys/tests/convertor_host.c"), B + "/conv_t.o",
               os.path.join(ROOT, "kernel/kstring.c"), os.path.join(ROOT, "sys/inflate.c"), os.path.join(ROOT, "sys/lzma.c")], check=True)
fails = 0
def check(name, ok, extra=""):
    global fails
    if not ok: fails += 1; print("FAIL:", name, extra)
def mktar(files, mode, extra=None):
    b = io.BytesIO()
    with tarfile.open(fileobj=b, mode=mode) as t:
        for n, c in files.items():
            if c is None:
                ti = tarfile.TarInfo(n); ti.type = tarfile.DIRTYPE; t.addfile(ti)
            else:
                ti = tarfile.TarInfo(n); d = c if isinstance(c, bytes) else c.encode(); ti.size = len(d); t.addfile(ti, io.BytesIO(d))
        if extra: extra(t)
    return b.getvalue()
def conv(data, kind):
    d = tempfile.mkdtemp(); i = d + "/in"; o = d + "/out.aspi"; open(i, "wb").write(data)
    r = subprocess.run([exe, kind, i, o], capture_output=True, text=True)
    return r.stdout.strip(), (open(o, "rb").read() if os.path.exists(o) else None)

big = bytes(range(256)) * 700                                         # 179 KB, binary
files = {"Pkg-1.0": None, "Pkg-1.0/ASPIManifest.json": '{"name":"Pkg","version":"1.0"}', "Pkg-1.0/src": None, "Pkg-1.0/src/main.as": 'printc("x");\n',
         "Pkg-1.0/data.bin": big, "Pkg-1.0/empty.txt": ""}
for kind, mode in (("gz", "w:gz"), ("xz", "w:xz")):
    msg, z = conv(mktar(files, mode), kind)
    check(kind + ": converts", msg.startswith("OK"), msg)
    if z:
        zf = zipfile.ZipFile(io.BytesIO(z))
        check(kind + ": zip is valid (CRC)", zf.testzip() is None)
        names = sorted(zf.namelist())
        check(kind + ": top folder removed", names == ["ASPIManifest.json", "data.bin", "empty.txt", "src/", "src/main.as"], str(names))
        check(kind + ": contents equal", zf.read("data.bin") == big and zf.read("src/main.as") == b'printc("x");\n' and zf.read("empty.txt") == b"")
# no common top folder: names stay
msg, z = conv(mktar({"a.txt": "1", "b/c.txt": "2"}, "w:gz"), "gz")
check("no top folder to remove", msg.startswith("OK") and sorted(zipfile.ZipFile(io.BytesIO(z)).namelist()) == ["a.txt", "b/c.txt"], msg)
# a single top folder with ONE nested folder: only one level is removed
msg, z = conv(mktar({"Top/ASPIManifest.json": "{}", "Top/Sub/x.as": "y"}, "w:gz"), "gz")
check("only one level removed", sorted(zipfile.ZipFile(io.BytesIO(z)).namelist()) == ["ASPIManifest.json", "Sub/x.as"])
# GNU long names (> 100 chars)
long = "T" * 30 + "/" + "d" * 40 + "/" + "f" * 40
b = io.BytesIO()
with tarfile.open(fileobj=b, mode="w:gz", format=tarfile.GNU_FORMAT) as t:
    d = b"long"; ti = tarfile.TarInfo(long); ti.size = len(d); t.addfile(ti, io.BytesIO(d))
    d = b"{}"; ti = tarfile.TarInfo("T" * 30 + "/ASPIManifest.json"); ti.size = 2; t.addfile(ti, io.BytesIO(d))
msg, z = conv(b.getvalue(), "gz")
check("GNU long names", z is not None and zipfile.ZipFile(io.BytesIO(z)).read("d" * 40 + "/" + "f" * 40) == b"long", msg)
# refusals
for bad, label in (("../evil.txt", "dot-dot"), ("/abs/evil.txt", "absolute"), ("ok/../../evil.txt", "nested dot-dot")):
    b = io.BytesIO()
    with tarfile.open(fileobj=b, mode="w:gz") as t:
        ti = tarfile.TarInfo(bad); ti.size = 1; t.addfile(ti, io.BytesIO(b"x"))
    msg, z = conv(b.getvalue(), "gz")
    check("unsafe path refused: " + label, msg.startswith("ERR") and "unsafe" in msg and z is None, msg)
msg, z = conv(b"this is not gzip data at all.....", "gz"); check("not gzip -> error", msg.startswith("ERR"), msg)
msg, z = conv(b"xx" * 40, "xz"); check("not xz -> error", msg.startswith("ERR"), msg)
msg, z = conv(mktar({"Top/": None}, "w:gz"), "gz"); check("no files -> error", msg.startswith("ERR") and "no files" in msg, msg)
print("convertor: %s" % ("ok" if not fails else "FAILED"))
sys.exit(1 if fails else 0)
