"""aspi_sync.py against a local fake GitHub (API tree + raw files + zipball fallback)."""
import http.server, io, json, os, subprocess, sys, tempfile, threading, zipfile, shutil
ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
def pkg(name, ver, extra=None, top=""):
    b = io.BytesIO()
    with zipfile.ZipFile(b, "w") as z:
        z.writestr(top + "ASPIManifest.json", json.dumps({"name": name, "version": ver, "description": "d"}))
        z.writestr(top + "main.as", 'printc("x");\n')
        for k, v in (extra or {}).items(): z.writestr(top + k, v)
    return b.getvalue()
REPO = "EnderAstra7744/aspi-package-sage"
files = {"packages/AsCode-1.3.0.aspi": pkg("AsCode", "1.3.0", {"installer.txt":
         "#EAP\n- AsUtil\n- AsExtra\n\n#'AsUtil\n#* https://github.com/EnderAstra7744/AsUtil\n#* ver='1.03'\n#'AsExtra\n#* https://github.com/EnderAstra7744/AsExtra\n#* ver='lastver'\n"}),
         "packages/AsExtra-0.2.aspi": pkg("AsExtra", "0.2"), "README.md": b"hi"}
zips = {"EnderAstra7744/AsUtil/zipball/1.03": pkg("AsUtil", "1.03", top="AsUtil-1.03/")}
class H(http.server.BaseHTTPRequestHandler):
    def log_message(self, *a): pass
    def do_GET(self):
        p = self.path
        if p.startswith("/api/repos/%s/git/trees/HEAD" % REPO):
            body = json.dumps({"tree": [{"type": "blob", "path": k} for k in files]}).encode()
        elif p.startswith("/raw/%s/HEAD/" % REPO) and p.split("HEAD/", 1)[1] in files:
            body = files[p.split("HEAD/", 1)[1]]
        elif p.startswith("/api/repos/") and p[len("/api/repos/"):] in zips:
            body = zips[p[len("/api/repos/"):]]
        else:
            self.send_response(404); self.end_headers(); return
        self.send_response(200); self.end_headers(); self.wfile.write(body)
srv = http.server.HTTPServer(("127.0.0.1", 0), H)
threading.Thread(target=srv.serve_forever, daemon=True).start()
base = "http://127.0.0.1:%d" % srv.server_port
d = tempfile.mkdtemp()
env = dict(os.environ, ASPI_API=base + "/api", ASPI_RAW=base + "/raw", ASPI_CACHE=d + "/c", ASPI_IMAGE=d + "/r.img")
r = subprocess.run([sys.executable, "-I", os.path.join(ROOT, "sys/tools/aspi_sync.py"), "fetch", "AsCode"], capture_output=True, text=True, env=env)
out = r.stdout + r.stderr
ok = r.returncode == 0 and "AsCode 1.3.0" in out and "AsUtil 1.03" in out and "AsExtra 0.2" in out and "3 package(s)" in out and "not in the repo, trying" in out
img = open(d + "/r.img", "rb").read() if os.path.exists(d + "/r.img") else b""
ok = ok and img[:8] == b"ASPIREPO" and len(img) % 512 == 0
r2 = subprocess.run([sys.executable, "-I", os.path.join(ROOT, "sys/tools/aspi_sync.py"), "fetch", "Nope"], capture_output=True, text=True, env=env)
ok = ok and "not found" in r2.stdout
# auto: no names needed, everything in the repo is fetched, dependencies included
d2 = tempfile.mkdtemp()
env2 = dict(env, ASPI_CACHE=d2 + "/c", ASPI_IMAGE=d2 + "/r.img")
r3 = subprocess.run([sys.executable, "-I", os.path.join(ROOT, "sys/tools/aspi_sync.py"), "auto"], capture_output=True, text=True, env=env2)
img3 = open(d2 + "/r.img", "rb").read() if os.path.exists(d2 + "/r.img") else b""
ok = ok and r3.returncode == 0 and img3[:8] == b"ASPIREPO" and b"AsCode" in img3 and b"AsExtra" in img3 and b"AsUtil" in img3
# auto offline: never fails, keeps working from the cache
env3 = dict(env2, ASPI_API="http://127.0.0.1:9/api", ASPI_RAW="http://127.0.0.1:9/raw")
r4 = subprocess.run([sys.executable, "-I", os.path.join(ROOT, "sys/tools/aspi_sync.py"), "auto"], capture_output=True, text=True, env=env3)
ok = ok and r4.returncode == 0 and "not reachable" in r4.stdout and "3 package(s)" in r4.stdout
shutil.rmtree(d2)
shutil.rmtree(d)

print("aspi_sync: %s" % ("ok" if ok else "FAILED\n" + out + r2.stdout + r3.stdout + r4.stdout))
sys.exit(0 if ok else 1)
