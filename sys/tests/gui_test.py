"""Builds the GUI harness (the real kernel GUI code as a 32-bit Linux process), runs it and turns
the screenshots into PNG files for inspection."""
import os, subprocess, sys, tempfile
ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
os.chdir(ROOT)
subprocess.run(["make", "-s", "assets/froggycode.elf", "assets/netsurf.elf"], check=True, stderr=subprocess.DEVNULL)
os.makedirs("build/test_pkgs", exist_ok=True)           # the real OS downloads these from GitHub; the harness has them linked in
subprocess.run(["python3", "sys/tools/mkpkg.py", "packages/FroggyCode", "build/test_pkgs/FroggyCode.aspi", "assets/froggycode.elf"], check=True, stdout=subprocess.DEVNULL)
subprocess.run(["python3", "sys/tools/mkpkg.py", "packages/NetSurf", "build/test_pkgs/NetSurf.aspi", "assets/netsurf.elf"], check=True, stdout=subprocess.DEVNULL)
open("build/test_pkgs.S", "w").write(""".section .rodata
.globl asset_froggy_start, asset_froggy_end, asset_nspkg_start, asset_nspkg_end
asset_froggy_start: .incbin "build/test_pkgs/FroggyCode.aspi"
asset_froggy_end:
asset_nspkg_start: .incbin "build/test_pkgs/NetSurf.aspi"
asset_nspkg_end:
""")
OUT = os.environ.get("GUI_SHOTS") or tempfile.mkdtemp(prefix="as_gui_")
SRC = ["kernel/kstring.c", "kernel/kprintf.c", "kernel/sysinfo.c", "terminal/ast.c", "terminal/console.c",
       "terminal/editor.c", "terminal/neofetch.c", "terminal/keyboard.c", "fs/fs.c", "fs/persist.c",
       "sys/users.c", "sys/config.c", "sys/perm.c", "sys/sha256.c", "sys/astrascript.c", "sys/as_host_kernel.c"] + ["AstraScript/src/" + f + ".c" for f in "builtins check classes collections engine errors fileobj functions interpreter lexer memory modules parser strings types lib_data".split()] + ["sys/inflate.c", "sys/lzma.c",
       "sys/archive.c", "sys/aspi.c", "sys/proc.c", "sys/png.c", "sys/bootlog.c", "arch/x86/drivers/video.c", "arch/x86/drivers/mouse.c", "arch/x86/drivers/sound.c", "gui/gfx.c", "gui/theme.c", "gui/bmp.c",
       "gui/coro.c", "gui/astragui.c", "gui/astraui.c", "gui/app_ast.c", "gui/app_files.c", "gui/app_welcome.c", "gui/app_settings.c", "gui/app_login.c", "gui/wallpaper.c", "gui/app_tasks.c", "gui/app_script.c", "gui/app_store.c", "gui/shell.c", "kernel/native.c", "sys/tests/gui_harness.c"]
os.makedirs("build", exist_ok=True)
for cpp, obj in (("sys/aspi_convertor.cpp", "build/aspi_convertor_gui.o"), ("sys/errscreen.cpp", "build/errscreen_gui.o")):
    r = subprocess.run(["g++", "-m32", "-ffreestanding", "-fno-builtin", "-fno-pic", "-fno-exceptions", "-fno-rtti", "-fno-threadsafe-statics", "-fno-use-cxa-atexit",
                        "-fno-stack-protector", "-std=gnu++17", "-O1", "-DAS_NO_IO", "-Ikernel", "-Iterminal", "-Ifont", "-Igui", "-IAstraScript/include", "-c", cpp,
                        "-o", obj], capture_output=True, text=True)
    if r.returncode:
        print(r.stdout + r.stderr); sys.exit(1)
cmd = ["gcc", "-m32", "-ffreestanding", "-fno-builtin", "-fno-pic", "-no-pie", "-nostdlib", "-static", "-O1",
       "-fno-stack-protector", "-std=gnu11", "-Wall", "-Wno-unused-function", "-DAS_NO_IO", *(["-DSHOT_BLACK"] if os.environ.get("GUI_BLACK") else []), "-Ikernel", "-Iterminal", "-Ifont", "-Igui", "-IAstraScript/include",
       "-o", "build/gui_harness", "build/aspi_convertor_gui.o", "build/errscreen_gui.o"] + SRC + ["gui/assets.S", "build/test_pkgs.S"]
r = subprocess.run(cmd, capture_output=True, text=True)
if r.returncode:
    print(r.stdout + r.stderr); sys.exit(1)
TC = []                                             # programs built by toolchain/as-cc with every available backend
for be in ("gcc", "clang", "tcc"):
    if be == "tcc" and not os.path.exists("build/tcc/i386-tcc"): continue
    o = "build/toolchain_hello_%s.elf" % be
    b = subprocess.run(["toolchain/as-cc", "--backend", be, "-Os", "-o", o, "native/hello.c"], capture_output=True, text=True)
    if b.returncode: print(b.stdout + b.stderr); sys.exit(1)
    TC.append(o)
r = subprocess.run(["build/gui_harness", OUT] + TC, capture_output=True, text=True, timeout=120)
print(r.stdout, end="")
if r.stderr: print(r.stderr)
try:
    from PIL import Image
    for f in sorted(os.listdir(OUT)):
        if f.endswith(".ppm"):
            Image.open(os.path.join(OUT, f)).save(os.path.join(OUT, f[:-4] + ".png"))
            os.unlink(os.path.join(OUT, f))
except ImportError:
    pass
print("screenshots:", OUT)
sys.exit(1 if r.returncode or "FAIL:" in r.stdout else 0)
