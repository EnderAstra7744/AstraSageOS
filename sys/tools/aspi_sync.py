#!/usr/bin/env python3
"""
aspi_sync.py - fills the ASPI repo disk (aspi_repo.img) on the host.

as_kernel has no network stack, so the download from GitHub happens here; everything else
(archive conversion, manifest, installer.txt, dependencies, compiling, installing) is done
inside AstraSageOS by `as aspi install <Name>`.

  python3 sys/tools/aspi_sync.py fetch AsCode [AsUtil ...]   download packages (+ dependencies) and rebuild the image
  python3 sys/tools/aspi_sync.py build FILE_OR_DIR ...        build the image from local archives (offline)
  python3 sys/tools/aspi_sync.py auto                         what `make run` does by itself: download everything, never fails
  python3 sys/tools/aspi_sync.py list                         show what is in the cache / image

Archives: .aspi (zip), .jar, .tar.gz, .tar.xz. Downloaded files are kept in ./aspi_cache/.
Repo: https://github.com/EnderAstra7744/aspi-package-sage  (file layout is discovered through the GitHub
tree API: every archive whose file name starts with the package name is taken).
NOTE: the live GitHub layout could not be reached while this tool was written; the file-name
matching is therefore a documented guess - use `build` with files you downloaded yourself if it misses.
Environment (for tests/mirrors): ASPI_API (default https://api.github.com), ASPI_RAW
(https://raw.githubusercontent.com), ASPI_GH (https://github.com), ASPI_REPO (EnderAstra7744/aspi-package-sage).
"""
import hashlib, io, json, os, re, struct, sys, tarfile, zipfile, urllib.request, urllib.error

API  = os.environ.get("ASPI_API", "https://api.github.com").rstrip("/")
RAW  = os.environ.get("ASPI_RAW", "https://raw.githubusercontent.com").rstrip("/")
GH   = os.environ.get("ASPI_GH", "https://github.com").rstrip("/")
REPO = os.environ.get("ASPI_REPO", "EnderAstra7744/aspi-package-sage")
CACHE = os.environ.get("ASPI_CACHE", "aspi_cache")
IMAGE = os.environ.get("ASPI_IMAGE", "aspi_repo.img")
EXTS = (".aspi", ".jar", ".tar.gz", ".tgz", ".tar.xz", ".txz")
MAX_ARCHIVE = 2 * 1024 * 1024
MAX_PKGS = 64


TIMEOUT = 60


def http(url):
    req = urllib.request.Request(url, headers={"User-Agent": "aspi_sync/0.5"})
    with urllib.request.urlopen(req, timeout=TIMEOUT) as r:
        return r.read()


def kind_of(name):
    n = name.lower()
    for ext, k in ((".aspi", "aspi"), (".jar", "jar"), (".tar.gz", "tar.gz"), (".tgz", "tar.gz"),
                   (".tar.xz", "tar.xz"), (".txz", "tar.xz")):
        if n.endswith(ext):
            return k
    return None


def read_member(data, kind, wanted):
    """Returns the text of ASPIManifest.json / installer.txt (any folder depth <= 1) or None."""
    try:
        if kind in ("aspi", "jar"):
            with zipfile.ZipFile(io.BytesIO(data)) as z:
                names = [n for n in z.namelist() if n.rstrip("/").split("/")[-1] == wanted and n.count("/") <= 1]
                return z.read(sorted(names, key=len)[0]).decode("utf-8", "replace") if names else None
        mode = "r:gz" if kind == "tar.gz" else "r:xz"
        with tarfile.open(fileobj=io.BytesIO(data), mode=mode) as t:
            names = [m for m in t.getmembers() if m.isfile() and m.name.split("/")[-1] == wanted and m.name.count("/") <= 1]
            return t.extractfile(sorted(names, key=lambda m: len(m.name))[0]).read().decode("utf-8", "replace") if names else None
    except Exception as e:
        sys.exit("aspi_sync: cannot read %s inside archive: %s" % (wanted, e))


def manifest_of(data, kind, label):
    txt = read_member(data, kind, "ASPIManifest.json")
    if txt is None:
        sys.exit("aspi_sync: %s has no ASPIManifest.json" % label)
    try:
        m = json.loads(txt)
        return str(m["name"]), str(m["version"])
    except Exception as e:
        sys.exit("aspi_sync: %s: bad ASPIManifest.json (%s)" % (label, e))


def parse_installer(txt):
    """-> list of (name, url, ver) following the installer.txt format."""
    secs, cur, eap, in_eap = {}, None, [], False
    for raw in txt.splitlines():
        l = raw.strip()
        if l.startswith("#EAP"):
            in_eap, cur = True, None
            continue
        m = re.match(r"#'\s*(\S+)", l)
        if m:
            cur, in_eap = m.group(1), False
            secs[cur] = {"url": "", "ver": "lastver"}
            continue
        if in_eap:
            m = re.match(r"[-•*]\s*(\S+)", l)
            if m:
                eap.append(m.group(1))
            continue
        if cur and l.startswith("#*"):
            body = l[2:].strip()
            m = re.match(r"ver\s*=\s*['\"]?([^'\"]+)['\"]?", body)
            if m:
                secs[cur]["ver"] = m.group(1)
            elif body.startswith("http"):
                secs[cur]["url"] = body
    return [(n, secs.get(n, {}).get("url", ""), secs.get(n, {}).get("ver", "lastver")) for n in eap]


def store(name, version, kind, data):
    os.makedirs(CACHE, exist_ok=True)
    path = os.path.join(CACHE, "%s-%s.%s" % (name, version, kind))
    with open(path, "wb") as f:
        f.write(data)
    return path


def repo_tree():
    url = "%s/repos/%s/git/trees/HEAD?recursive=1" % (API, REPO)
    return [e["path"] for e in json.loads(http(url)).get("tree", []) if e.get("type") == "blob"]


def fetch_from_repo(name, want_ver, tree):
    got = []
    low = name.lower()
    for path in tree:
        base = path.split("/")[-1]
        k = kind_of(base)
        if not k:
            continue
        stem = base.lower()
        if not (stem.startswith(low) and (len(stem) == len(low) or not stem[len(low)].isalnum() or stem[len(low):].startswith("."))) \
                and ("/" + low + "/") not in ("/" + path.lower()):
            continue
        data = http("%s/%s/HEAD/%s" % (RAW, REPO, path))
        if len(data) > MAX_ARCHIVE:
            print("  skip %s: larger than 2 MiB" % path)
            continue
        n, v = manifest_of(data, k, path)
        if n.lower() != low:
            continue
        if want_ver not in ("", "lastver", "latest") and v != want_ver:
            continue
        got.append((n, v, k, data, path))
    return got


def fetch_from_url(name, url, want_ver):
    m = re.match(r"https?://github\.com/([^/]+/[^/#?]+?)(?:\.git)?/?$", url)
    if not m:
        return []
    ref = "HEAD" if want_ver in ("", "lastver", "latest") else want_ver
    tried = [ref] if ref == "HEAD" else [ref, "v" + ref]
    for r in tried:
        try:
            data = http("%s/repos/%s/zipball/%s" % (API, m.group(1), r))
        except urllib.error.URLError:
            continue
        if len(data) > MAX_ARCHIVE:
            print("  skip %s: larger than 2 MiB" % url)
            return []
        n, v = manifest_of(data, "aspi", url)
        return [(n, v, "aspi", data, url)]
    return []


def fetch(names):
    tree = None
    queue = [(n, "lastver", "") for n in names]
    done = set()
    while queue:
        name, ver, url = queue.pop(0)
        key = (name.lower(), ver)
        if key in done:
            continue
        done.add(key)
        print("fetch %s (%s)" % (name, ver))
        found = []
        try:
            if tree is None:
                tree = repo_tree()
            found = fetch_from_repo(name, ver, tree)
        except (urllib.error.URLError, ValueError) as e:
            print("  repo %s not reachable: %s" % (REPO, e))
        if not found and url:
            print("  not in the repo, trying %s" % url)
            try:
                found = fetch_from_url(name, url, ver)
            except urllib.error.URLError as e:
                print("  %s not reachable: %s" % (url, e))
        if not found:
            print("  !! %s %s not found" % (name, ver))
            continue
        for n, v, k, data, src in found:
            p = store(n, v, k, data)
            print("  ok  %s %s <- %s  (%d bytes, %s)" % (n, v, src, len(data), k))
            inst = read_member(data, k, "installer.txt")
            if inst:
                for dn, durl, dver in parse_installer(inst):
                    queue.append((dn, dver, durl))


def add_local(paths):
    files = []
    for p in paths:
        if os.path.isdir(p):
            files += [os.path.join(p, f) for f in sorted(os.listdir(p))]
        else:
            files.append(p)
    n = 0
    for f in files:
        k = kind_of(f)
        if not k:
            continue
        data = open(f, "rb").read()
        if len(data) > MAX_ARCHIVE:
            print("skip %s: larger than 2 MiB" % f)
            continue
        name, ver = manifest_of(data, k, f)
        store(name, ver, k, data)
        print("added %s %s (%s)" % (name, ver, k))
        n += 1
    if not n:
        sys.exit("aspi_sync: no archives (.aspi .jar .tar.gz .tar.xz) found")


def build_image():
    ents = []
    for f in sorted(os.listdir(CACHE)) if os.path.isdir(CACHE) else []:
        k = kind_of(f)
        if not k:
            continue
        data = open(os.path.join(CACHE, f), "rb").read()
        name, ver = manifest_of(data, k, f)
        if len(name) > 23 or len(ver) > 23:
            sys.exit("aspi_sync: %s: name/version too long" % f)
        ents.append((name, ver, k, data))
    if not ents:
        sys.exit("aspi_sync: nothing to put on the repo disk")
    if len(ents) > MAX_PKGS:
        sys.exit("aspi_sync: more than %d packages" % MAX_PKGS)
    index_secs = (len(ents) + 3) // 4
    lba = 1 + index_secs
    head = bytearray(512)
    head[0:8] = b"ASPIREPO"
    struct.pack_into("<II", head, 8, 1, len(ents))
    src = (GH.split("://")[-1] + "/" + REPO).encode()[:63]
    head[16:16 + len(src)] = src
    index = bytearray(index_secs * 512)
    blobs = []
    for i, (name, ver, k, data) in enumerate(ents):
        e = bytearray(128)
        e[0:len(name)] = name.encode()
        e[32:32 + len(ver)] = ver.encode()
        e[56:56 + len(k)] = k.encode()
        assert len(e) == 128
        struct.pack_into("<II", e, 64, lba, len(data))
        e[72:104] = hashlib.sha256(data).digest()
        assert len(e) == 128
        index[i * 128:(i + 1) * 128] = e
        secs = (len(data) + 511) // 512
        blobs.append(data + b"\0" * (secs * 512 - len(data)))
        lba += secs
    with open(IMAGE, "wb") as f:
        f.write(bytes(head) + bytes(index) + b"".join(blobs))
    print("wrote %s: %d package(s), %d bytes" % (IMAGE, len(ents), os.path.getsize(IMAGE)))
    for name, ver, k, data in ents:
        print("  %-16s %-10s %-7s %d B" % (name, ver, k, len(data)))


def auto():
    """Everything without arguments, never fatal: every package archive of the repo is downloaded (plus the
    dependencies named in their installer.txt), the image is rebuilt. No network -> the cache is used as it is."""
    global TIMEOUT
    TIMEOUT = 12
    try:
        tree = repo_tree()
        names = []
        for path in tree:
            base = path.split("/")[-1]
            k = kind_of(base)
            if not k or "/" + base.lower() in ("/aspi_repo.img",):
                continue
            try:
                data = http("%s/%s/HEAD/%s" % (RAW, REPO, path))
                if len(data) > MAX_ARCHIVE:
                    continue
                n, v = manifest_of(data, k, path)
            except (SystemExit, Exception) as e:
                print("  skip %s: %s" % (path, e))
                continue
            if n not in names:
                names.append(n)
        if names:
            fetch(names)
    except (urllib.error.URLError, OSError, ValueError) as e:
        print("aspi: GitHub is not reachable (%s) - using what is in %s/" % (e, CACHE))
    if os.path.isdir(CACHE) and any(kind_of(f) for f in os.listdir(CACHE)):
        try:
            build_image()
        except SystemExit as e:
            print("aspi: image not rebuilt: %s" % e)
    else:
        print("aspi: no packages cached yet - `as aspi` will say 'no repo disk' until a download works")
    return 0


def main(argv):
    if len(argv) == 2 and argv[1] == "auto":
        return auto()
    if len(argv) < 2 or argv[1] not in ("fetch", "build", "list"):
        print(__doc__)
        return 1
    if argv[1] == "fetch":
        if len(argv) < 3:
            print("usage: aspi_sync.py fetch <Name> [Name...]")
            return 1
        fetch(argv[2:])
    elif argv[1] == "build":
        add_local(argv[2:])
    build_image()
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
