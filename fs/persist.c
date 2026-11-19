/*
 * Persistence: mirrors the file tree to the ATA disk so files survive reboots.
 *
 * Disk layout (512-byte sectors), format 4:
 *   LBA 0                 header  "ASFS" + version + node count + save counter
 *   LBA 2112 .. 2239      node table (8 records of 64 bytes per sector, 1024 nodes); a record holds the
 *                         start sector and sector count of the node's contents
 *   LBA 2240 ..           file contents in extents (any length), first-fit allocated
 *
 * Format 3 (256 nodes, 4 KiB per node at LBA 64 + node * 8) is still read: the first save after it writes
 * everything in the new layout in the unused part of the disk and flips the header last, so the old data
 * stays intact until the new copy is complete.
 *
 * Saving writes changed contents into fresh extents first, then the table, then the header; an interrupted
 * save leaves the previous table (and the extents it points to) intact.
 */
#include "astra.h"

#define MAGIC        0x53465341u        /* "ASFS" little endian */
#define VERSION      4u
#define V3           3u
#define V3_NODES     256u
#define V3_DATA_LBA  64u
#define TABLE_LBA    2112u
#define TABLE_SECTS  (FS_MAX_NODES / 8)
#define DATA_LBA     (TABLE_LBA + TABLE_SECTS)
#define REGION_MAX   2097152u            /* sectors of contents the allocator manages (1 GiB) */

typedef struct {
    u32 magic, version, nodes, counter;
    u8  pad[496];
} header;

typedef struct {
    u8 used, is_dir; u16 pad;
    int32_t parent; u32 size, mtime;
    char name[FS_NAME_MAX];
    u32 ctime;
    u32 start, secs;                    /* format 4: extent of the contents (LBA, sectors) */
    u8 reserved[64 - 52];
} disk_rec;

static int   active, present;
static u32   counter, region;           /* sectors in the content region */
static int   migrate;                   /* loaded from format 3: write everything on the next save */
static u32   ext_start[FS_MAX_NODES], ext_secs[FS_MAX_NODES];
static u8    used_map[REGION_MAX / 8];
static const char *status = "no disk (volatile)";

int         persist_active(void)  { return active; }
const char *persist_status(void)  { return status; }

static int  bit(u32 i)            { return used_map[i >> 3] >> (i & 7) & 1; }
/* extents of big files that were moved or deleted: they are only handed back after the next save commits the new table,
 * so a crash in between still finds the old data where the old table says it is */
#define PEND_MAX 32
static u32 pend_start[PEND_MAX], pend_secs[PEND_MAX]; static int pend_n;
static void setbits(u32 lba, u32 n, int v);
static void release_later(u32 lba, u32 n)
{
    if (!n) return;
    if (pend_n < PEND_MAX) { pend_start[pend_n] = lba; pend_secs[pend_n++] = n; }
    else setbits(lba, n, 0);
}

static void setbits(u32 lba, u32 n, int v)
{
    if (lba < DATA_LBA) return;
    for (u32 i = lba - DATA_LBA; n && i < region; i++, n--) { if (v) used_map[i >> 3] |= (u8)(1u << (i & 7)); else used_map[i >> 3] &= (u8)~(1u << (i & 7)); }
}

/* first fit; 0 = no room */
static u32 alloc_secs(u32 n)
{
    u32 run = 0;
    for (u32 i = 0; i < region; i++) {
        if (!(i & 7) && used_map[i >> 3] == 0xFF) { run = 0; i += 7; continue; }      /* a full byte of used sectors */
        if (bit(i)) { run = 0; continue; }
        if (++run == n) { u32 start = i + 1 - n; return DATA_LBA + start; }
    }
    return 0;
}

static int load(void)
{
    static header h;
    static u8 sector[512];

    if (ata_read(0, &h) < 0) { status = "disk read error (volatile)"; return -1; }
    if (h.magic != MAGIC) {                            /* blank or foreign disk: format on first save */
        active = 1;
        status = "new disk (formatted on first save)";
        return 0;
    }
    int v3 = h.version == V3 && h.nodes == V3_NODES;
    if (!v3 && (h.version != VERSION || h.nodes != FS_MAX_NODES)) {
        status = "incompatible disk format (volatile)";
        return -1;
    }
    counter = h.counter;
    migrate = v3;

    fs_clear_all();
    kmemset(used_map, 0, sizeof used_map);
    kmemset(ext_start, 0, sizeof ext_start); kmemset(ext_secs, 0, sizeof ext_secs);
    int restored = 0;
    u32 tsects = v3 ? V3_NODES / 8 : TABLE_SECTS, tlba = v3 ? 1u : TABLE_LBA;
    for (u32 t = 0; t < tsects; t++) {
        if (ata_read(tlba + t, sector) < 0) { status = "disk read error (volatile)"; fs_clear_all(); return -1; }
        for (u32 k = 0; k < 8; k++) {
            int node = (int)(t * 8 + k);
            const disk_rec *d = (const disk_rec *)(sector + k * 64);
            if (!d->used) continue;
            fs_rec r;
            kmemset(&r, 0, sizeof r);
            r.used = 1; r.is_dir = d->is_dir; r.parent = d->parent;
            r.size = d->size; r.mtime = d->mtime; r.ctime = d->ctime;
            kmemcpy(r.name, d->name, FS_NAME_MAX);
            if (!r.is_dir && v3 && r.size > 4096u) continue;
            if (fs_put_rec(node, &r, 0) != FS_OK) continue;
            if (!r.is_dir && r.size > FS_STORE_MAX) {          /* big file: stays on the disk, only its extent is registered */
                ext_start[node] = d->start; ext_secs[node] = d->secs;
                setbits(d->start, d->secs, 1);
            } else if (!r.is_dir && r.size) {
                u32 secs = (r.size + 511) / 512;
                u32 lba = v3 ? V3_DATA_LBA + (u32)node * 8 : d->start;
                char *buf = fs_rec_buf(node);
                for (u32 s = 0; buf && s < secs; s++) {
                    if (ata_read(lba + s, buf + s * 512) < 0) {
                        status = "disk read error (volatile)";
                        fs_clear_all();
                        return -1;
                    }
                }
                if (!v3) { ext_start[node] = lba; ext_secs[node] = secs; setbits(lba, secs, 1); }
            }
            if (!r.is_dir) restored++;
        }
    }
    fs_clear_dirty();
    active = 1;
    status = v3 ? "asfs on ata disk (old format, upgraded on the next save)" : "asfs on ata disk";
    return restored;
}

int persist_init(void)
{
    present = ata_init();
    if (!present) { bl_add(BL_WARN, "ATA disk 0: none found - files are lost on shutdown"); return -1; }
    bl_add(BL_OK, "ATA disk 0 found: %u sectors (%u MiB)", ata_sectors(), ata_sectors() / 2048);
    if (ata_sectors() < DATA_LBA + 256) { status = "disk too small (volatile)"; bl_add(BL_ERR, "ATA disk 0 too small for asfs: %s", status); return -1; }
    region = ata_sectors() - DATA_LBA;
    if (region > REGION_MAX) region = REGION_MAX;
    int r = load();
    if (r < 0) bl_add(BL_ERR, "asfs: %s", status);
    else if (r == 0) bl_add(BL_WARN, "asfs: %s", status);
    else bl_add(BL_OK, "asfs: %d files read from the disk", r);
    return r;
}

int persist_save(void)
{
    static header h;
    static u8 sector[512], pad[512];
    static u32 old_start[FS_MAX_NODES], old_secs[FS_MAX_NODES];

    if (!active) return -1;
    kmemset(old_secs, 0, sizeof old_secs);

    for (int n = 1; n < FS_MAX_NODES; n++) {           /* contents first, into fresh extents */
        fs_rec r;
        fs_get_rec(n, &r);
        if (!r.used) continue;                          /* (extents of deleted nodes are freed after the commit) */
        if (r.is_dir || !r.size) continue;
        if (fs_is_big(n)) continue;                     /* big files are written in place as they change */
        if (!migrate && !fs_data_dirty(n) && ext_secs[n]) continue;
        u32 secs = (r.size + 511) / 512;
        u32 lba = alloc_secs(secs);
        if (!lba) { status = "disk full"; goto fail_free; }
        const char *src = fs_data(n);
        for (u32 s = 0; s < secs; s++) {
            const void *p = src + s * 512;
            if ((s + 1) * 512 > r.size) { kmemset(pad, 0, sizeof pad); kmemcpy(pad, src + s * 512, r.size - s * 512); p = pad; }
            if (ata_write(lba + s, p) < 0) goto fail;
        }
        old_start[n] = ext_start[n]; old_secs[n] = ext_secs[n];
        ext_start[n] = lba; ext_secs[n] = secs;
        setbits(lba, secs, 1);
    }
    for (u32 t = 0; t < TABLE_SECTS; t++) {            /* then the table */
        kmemset(sector, 0, sizeof sector);
        for (u32 k = 0; k < 8; k++) {
            int n = (int)(t * 8 + k);
            fs_rec r;
            fs_get_rec(n, &r);
            disk_rec *d = (disk_rec *)(sector + k * 64);
            d->used = r.used; d->is_dir = r.is_dir; d->parent = r.parent;
            d->size = r.size; d->mtime = r.mtime; d->ctime = r.ctime;
            kmemcpy(d->name, r.name, FS_NAME_MAX);
            d->start = ext_start[n]; d->secs = ext_secs[n];
        }
        if (ata_write(TABLE_LBA + t, sector) < 0) goto fail;
    }
    kmemset(&h, 0, sizeof h);                          /* header last: this is the commit */
    h.magic = MAGIC; h.version = VERSION; h.nodes = FS_MAX_NODES; h.counter = ++counter;
    if (ata_write(0, &h) < 0) goto fail;
    migrate = 0;
    for (int n = 1; n < FS_MAX_NODES; n++) {           /* the old extents are free now */
        fs_rec r;
        fs_get_rec(n, &r);
        if (old_secs[n]) setbits(old_start[n], old_secs[n], 0);
        if (!r.used && ext_secs[n]) { setbits(ext_start[n], ext_secs[n], 0); ext_start[n] = ext_secs[n] = 0; }
        if (r.used && (r.is_dir || !r.size) && ext_secs[n]) { setbits(ext_start[n], ext_secs[n], 0); ext_start[n] = ext_secs[n] = 0; }
    }
    for (int i = 0; i < pend_n; i++) setbits(pend_start[i], pend_secs[i], 0);
    pend_n = 0;
    fs_clear_dirty();
    status = "asfs on ata disk";
    return 0;
fail_free:                                             /* undo the extents of this attempt */
    for (int n = 1; n < FS_MAX_NODES; n++)
        if (old_secs[n]) { setbits(ext_start[n], ext_secs[n], 0); ext_start[n] = old_start[n]; ext_secs[n] = old_secs[n]; }
    return -1;
fail:
    status = "disk write error";
    return -1;
}

/* ---- big files: one contiguous extent per file, read and written directly ---- */

u32 persist_capacity_bytes(void) { return region * 512u; }
u32 persist_used_bytes(void)
{
    u32 n = 0;
    for (u32 i = 0; i < region; i++) if (bit(i)) n++;
    return n * 512u;
}

/* makes sure the node's extent holds at least `bytes`; a big file's contents move along when it has to grow */
int persist_big_reserve(int node, u32 bytes)
{
    static u8 buf[512];
    if (!active) return FS_ETOOBIG;
    u32 need = (bytes + 511) / 512;
    if (need <= ext_secs[node] && (fs_is_big(node) || ext_secs[node])) return FS_OK;
    u32 want = need + need / 8 + 16;                    /* some growth room */
    u32 lba = alloc_secs(want);
    if (!lba) { want = need; lba = alloc_secs(want); }
    if (!lba) return FS_EFULL;
    if (fs_is_big(node) && ext_secs[node]) {
        u32 used = (fs_size_real(node) + 511) / 512;
        for (u32 s = 0; s < used; s++) {
            if (ata_read(ext_start[node] + s, buf) < 0 || ata_write(lba + s, buf) < 0) return FS_EIO;
        }
    }
    release_later(ext_start[node], ext_secs[node]);
    ext_start[node] = lba; ext_secs[node] = want;
    setbits(lba, want, 1);
    return FS_OK;
}

int persist_big_rw(int node, u32 off, void *data, u32 len, int write)
{
    static u8 sec[512];
    u8 *p = data;
    if (!active || !ext_secs[node]) return FS_EIO;
    if (off + len < off || off + len > ext_secs[node] * 512u) return FS_EINVAL;
    while (len) {
        u32 s = off / 512, in = off % 512, k = 512 - in;
        if (k > len) k = len;
        u32 lba = ext_start[node] + s;
        if (!write) {
            if (ata_read(lba, sec) < 0) return FS_EIO;
            kmemcpy(p, sec + in, k);
        } else {
            if (k < 512 && ata_read(lba, sec) < 0) return FS_EIO;
            kmemcpy(sec + in, p, k);
            if (ata_write(lba, sec) < 0) return FS_EIO;
        }
        p += k; off += k; len -= k;
    }
    return FS_OK;
}

void persist_big_free(int node)
{
    release_later(ext_start[node], ext_secs[node]);
    ext_start[node] = ext_secs[node] = 0;
}

void persist_flush(void)
{
    if (active && fs_dirty()) persist_save();
}
