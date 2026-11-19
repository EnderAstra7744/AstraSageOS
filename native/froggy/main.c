/* FroggyCode 2.0 - events, chats, saving. See fg.h. */
#include <stdarg.h>
#include "fg.h"

int vsnprintf(char *b, size_t n, const char *f, va_list ap);

extern const as_api *__api;
const as_api *A;
State S;

/* ================================================================ small helpers */

static void put_line(char *dst, const char *s)
{
    size_t n = strlen(s);
    if (n >= LINE_LEN) n = LINE_LEN - 1;
    memcpy(dst, s, n);
    dst[n] = 0;
}

void log_con(int kind, const char *fmt, ...)
{
    char b[LINE_LEN * 2];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(b, sizeof b, fmt, ap);
    va_end(ap);
    if (S.ncon == CON_LINES) { memmove(S.con[0], S.con[1], (CON_LINES - 1) * LINE_LEN); memmove(S.conk, S.conk + 1, CON_LINES - 1); S.ncon--; }
    put_line(S.con[S.ncon], b);
    S.conk[S.ncon++] = (char)kind;
}

void term_add(const char *s)
{
    while (*s) {                                                  /* one terminal row per line of text */
        const char *e = s;
        while (*e && *e != '\n') e++;
        char b[LINE_LEN];
        size_t n = (size_t)(e - s);
        if (n >= LINE_LEN) n = LINE_LEN - 1;
        memcpy(b, s, n); b[n] = 0;
        if (S.nterm == TERM_LINES) { memmove(S.term[0], S.term[1], (TERM_LINES - 1) * LINE_LEN); S.nterm--; }
        put_line(S.term[S.nterm++], b);
        s = *e ? e + 1 : e;
    }
}

int exec_cmd(const char *cmd, char *out, int cap)
{
    out[0] = 0;
    if (!A->exec) return -2;
    return A->exec(cmd, out, cap);
}

void redraw_now(void)
{
    ui_draw();
    A->win_show();
}

/* ================================================================ files: settings, memory, chats (every file is limited to 4096 bytes) */

static int load_file(const char *name, char *buf, int cap)
{
    int n = A->file_read(name, buf, cap - 1);
    if (n < 0) return -1;
    buf[n] = 0;
    return n;
}

void settings_save(void)
{
    char b[400];
    snprintf(b, sizeof b, "theme=%d\nbig=%d\nlang=%d\npanel=%d\ndock=%d\nfiles=%d\nterm=%d\nweb=%d\nplug=%d\n", S.theme, S.big, S.lang_en, S.panel,
             S.dock_open, S.always[P_FILES], S.always[P_TERM], S.always[P_WEB], S.always[P_PLUG]);
    A->file_put("fc_settings.txt", b, 0);
}

static void settings_load(void)
{
    static char b[1024];
    S.panel = 1; S.dock_open = 1;
    if (load_file("fc_settings.txt", b, sizeof b) < 0) return;
    for (char *l = b; *l;) {
        char *e = strchr(l, '\n');
        if (e) *e = 0;
        int v = atoi(strchr(l, '=') ? strchr(l, '=') + 1 : "0");
        if (!strncmp(l, "theme=", 6)) S.theme = v;
        else if (!strncmp(l, "big=", 4)) S.big = v;
        else if (!strncmp(l, "lang=", 5)) S.lang_en = v;
        else if (!strncmp(l, "panel=", 6)) S.panel = v;
        else if (!strncmp(l, "dock=", 5)) S.dock_open = v;
        else if (!strncmp(l, "files=", 6)) S.always[P_FILES] = v;
        else if (!strncmp(l, "term=", 5)) S.always[P_TERM] = v;
        else if (!strncmp(l, "web=", 4)) S.always[P_WEB] = v;
        else if (!strncmp(l, "plug=", 5)) S.always[P_PLUG] = v;
        if (!e) break;
        l = e + 1;
    }
    if (S.theme < 0 || S.theme > 2) S.theme = 0;
}

void mem_save(void)
{
    static char b[4096];
    int n = 0;
    for (int i = 0; i < S.nmem; i++) {
        int w = snprintf(b + n, sizeof b - (size_t)n, "%s|%s\n", S.mem[i].key, S.mem[i].val);
        if (w < 0 || n + w >= (int)sizeof b - 1) break;
        n += w;
    }
    b[n] = 0;
    A->file_put("fc_memory.txt", b, 0);
}

static void mem_load(void)
{
    static char b[4200];
    S.nmem = 0;
    if (load_file("fc_memory.txt", b, sizeof b) < 0) return;
    for (char *l = b; *l && S.nmem < MAX_MEM;) {
        char *e = strchr(l, '\n');
        if (e) *e = 0;
        char *bar = strchr(l, '|');
        if (bar) {
            *bar = 0;
            snprintf(S.mem[S.nmem].key, MEM_KEY, "%s", l);
            snprintf(S.mem[S.nmem].val, MEM_VAL, "%s", bar + 1);
            S.nmem++;
        }
        if (!e) break;
        l = e + 1;
    }
}

/* one chat = one file "fc_chat<id>.txt": a line per message, "U..." or "F...", new lines written as \n */
static void chat_file(int id, char *out, size_t cap) { snprintf(out, cap, "fc_chat%d.txt", id); }

void chat_save(int i)
{
    static char b[4600];
    Chat *c = &S.chats[i];
    if (c->n == 0) return;
    int first = 0;
    for (;;) {                                                    /* drop the oldest messages until the rest fits in 4000 bytes */
        int need = 0;
        for (int k = first; k < c->n; k++) {
            if (c->m[k].role == R_ASK) continue;
            need += (int)strlen(c->m[k].text) * 2 + 3;
        }
        if (need < 3900 || first >= c->n - 1) break;
        first++;
    }
    int n = 0;
    for (int k = first; k < c->n; k++) {
        if (c->m[k].role == R_ASK) continue;
        if (n + 3 >= 4000) break;
        b[n++] = c->m[k].role == R_USER ? 'U' : 'F';
        for (const char *t = c->m[k].text; *t && n + 4 < 4000; t++) {
            if (*t == '\n') { b[n++] = '\\'; b[n++] = 'n'; }
            else if (*t == '\\') { b[n++] = '\\'; b[n++] = '\\'; }
            else b[n++] = *t;
        }
        b[n++] = '\n';
    }
    b[n] = 0;
    char f[40]; chat_file(c->id, f, sizeof f);
    A->file_put(f, b, 0);
    static char ix[600];
    int w = 0;
    for (int k = 0; k < S.nchats; k++) {
        if (S.chats[k].n == 0) continue;
        int r = snprintf(ix + w, sizeof ix - (size_t)w, "%d|%s\n", S.chats[k].id, S.chats[k].title);
        if (r < 0 || w + r >= (int)sizeof ix - 1) break;
        w += r;
    }
    ix[w] = 0;
    A->file_put("fc_index.txt", ix, 0);
}

static void chats_load(void)
{
    static char ix[700], b[4200];
    S.nchats = 0;
    if (load_file("fc_index.txt", ix, sizeof ix) >= 0) {
        for (char *l = ix; *l && S.nchats < MAX_CHATS - 1;) {
            char *e = strchr(l, '\n');
            if (e) *e = 0;
            char *bar = strchr(l, '|');
            if (bar) {
                *bar = 0;
                Chat *c = &S.chats[S.nchats];
                memset(c, 0, sizeof *c);
                c->id = atoi(l);
                snprintf(c->title, sizeof c->title, "%s", bar + 1);
                char f[40]; chat_file(c->id, f, sizeof f);
                if (load_file(f, b, sizeof b) >= 0) {
                    for (char *m = b; *m && c->n < MAX_MSGS;) {
                        char *me = strchr(m, '\n');
                        if (me) *me = 0;
                        if (*m == 'U' || *m == 'F') {
                            Msg *g = &c->m[c->n++];
                            g->role = *m == 'U' ? R_USER : R_FROG;
                            int o = 0;
                            for (const char *t = m + 1; *t && o < MSG_LEN - 1; t++) {
                                if (*t == '\\' && t[1] == 'n') { g->text[o++] = '\n'; t++; }
                                else if (*t == '\\' && t[1] == '\\') { g->text[o++] = '\\'; t++; }
                                else g->text[o++] = *t;
                            }
                            g->text[o] = 0;
                        }
                        if (!me) break;
                        m = me + 1;
                    }
                }
                if (c->n) S.nchats++;
            }
            if (!e) break;
            l = e + 1;
        }
    }
}

Chat *chat_cur(void) { return &S.chats[S.cur]; }

void chat_new(void)
{
    if (S.nchats > 0 && S.chats[S.cur].n == 0) { S.view = V_CHAT; S.scroll = 0; return; }       /* the current chat is still empty */
    if (S.nchats == MAX_CHATS) {                                  /* the oldest one makes room */
        memmove(&S.chats[0], &S.chats[1], sizeof(Chat) * (MAX_CHATS - 1));
        S.nchats--;
    }
    int id = 1;
    for (int i = 0; i < S.nchats; i++) if (S.chats[i].id >= id) id = S.chats[i].id + 1;
    Chat *c = &S.chats[S.nchats];
    memset(c, 0, sizeof *c);
    c->id = id;
    snprintf(c->title, sizeof c->title, "%s", tr("Yeni sohbet", "New chat"));
    S.cur = S.nchats++;
    S.view = V_CHAT; S.scroll = 0;
}

void chat_open(int i)
{
    if (i < 0 || i >= S.nchats) return;
    S.cur = i; S.view = V_CHAT; S.scroll = 0;
}

void chat_add(int role, const char *text)
{
    Chat *c = chat_cur();
    if (c->n == MAX_MSGS) { memmove(&c->m[0], &c->m[1], sizeof(Msg) * (MAX_MSGS - 1)); c->n--; }
    Msg *m = &c->m[c->n++];
    m->role = (char)role;
    snprintf(m->text, MSG_LEN, "%s", text);
    if (role == R_USER && c->n <= 2 && !strcmp(c->title, tr("Yeni sohbet", "New chat"))) {       /* the first thing said names the chat */
        int k = 0;
        for (const char *t = text; *t && k < 30; t++) { if ((*t & 0xC0) != 0x80) k++; if (k > 30) break; }
        snprintf(c->title, sizeof c->title, "%.30s", text);
    }
    S.scroll = 0;
}

/* installed plugins (ASPI packages) and the files of the user's folder */
void refresh_lists(void)
{
    static char out[2048];
    char keep[MAX_PLUG][32]; int nk = 0;
    for (int i = 0; i < S.nplug; i++) if (S.plug_sel[i]) snprintf(keep[nk++], 32, "%s", S.plug_name[i]);   /* what was ticked stays ticked */
    S.nplug = 0;
    if (A->aspi && A->aspi("list", out, sizeof out) >= 0) {
        for (char *l = out; *l && S.nplug < MAX_PLUG;) {
            char *e = strchr(l, '\n');
            if (e) *e = 0;
            char name[32]; int k = 0, o = 0;
            while (l[k] == ' ') k++;
            while (l[k] && l[k] != ' ' && o < 31) name[o++] = l[k++];
            name[o] = 0;
            if (o > 0 && isalpha((unsigned char)name[0]) && strcmp(name, "No")) {
                snprintf(S.plug_name[S.nplug], 32, "%s", name);
                S.plug_sel[S.nplug] = 0;
                for (int j = 0; j < nk; j++) if (!strcmp(keep[j], name)) S.plug_sel[S.nplug] = 1;
                S.nplug++;
            }
            if (!e) break;
            l = e + 1;
        }
    }
    S.nproj = 0;
    if (exec_cmd("as files", out, sizeof out) >= 0) {
        for (char *l = out; *l && S.nproj < 16;) {
            char *e = strchr(l, '\n');
            if (e) *e = 0;
            char *p = l;                                          /* "     57 B  2026-11-01 09:10  hello.as" - the name is the last word of a line starting with a size */
            while (*p == ' ') p++;
            char name[32]; int o = 0;
            name[0] = 0;
            if (isdigit((unsigned char)*p)) {
                char *w = p + strlen(p);
                while (w > p && w[-1] == ' ') w--;
                char *st = w;
                while (st > p && st[-1] != ' ') st--;
                for (; st < w && o < 31; st++) name[o++] = *st;
            }
            name[o] = 0;
            size_t nl = strlen(name);
            if (nl > 2 && (!strcmp(name + nl - 3, ".as") || (nl > 3 && (!strcmp(name + nl - 4, ".txt") || !strcmp(name + nl - 4, ".cpp"))) || !strcmp(name + nl - 2, ".c")))
                snprintf(S.proj[S.nproj++], 32, "%s", name);
            if (!e) break;
            l = e + 1;
        }
    }
}

/* ================================================================ events */

static void send_input(void)
{
    if (S.thinking) return;
    char t[INP_LEN];
    snprintf(t, sizeof t, "%s", S.inp);
    size_t n = strlen(t);
    while (n && t[n - 1] == ' ') t[--n] = 0;
    if (!n) return;
    S.inp[0] = 0;
    brain_say(t);
    chat_save(S.cur);
}

static void run_terminal(void)
{
    char t[INP_LEN];
    snprintf(t, sizeof t, "%s", S.tinp);
    S.tinp[0] = 0;
    if (!t[0]) return;
    char line[LINE_LEN + 8];
    snprintf(line, sizeof line, "$ %s", t);
    term_add(line);
    if (!strcmp(t, "clear")) { S.nterm = 0; return; }
    static char out[2048];
    int r;
    if (!strncmp(t, "aspi ", 5) && A->aspi) r = A->aspi(t + 5, out, sizeof out);
    else r = exec_cmd(t, out, sizeof out);
    if (r < 0) {
        term_add(tr("Bu komut burada çalışmaz (edit, login, reboot gibi komutlar AST penceresinde kullanılır).",
                    "This command does not run here (edit, login, reboot and similar are used in the AST window)."));
        log_con(L_ERR, "terminal: %s", t);
    } else {
        term_add(out[0] ? out : "");
        log_con(L_OK, "terminal: %s", t);
    }
}

static void on_click(int x, int y)
{
    int arg = 0;
    int id = ui_hit(x, y, &arg);
    switch (id) {
    case B_VIEW:     S.view = arg; S.settings_open = 0; if (arg != V_CHAT) refresh_lists(); break;
    case B_SETTINGS: S.settings_open = !S.settings_open; break;
    case B_PANEL:    S.panel = !S.panel; settings_save(); break;
    case B_DOCK:     S.dock_open = !S.dock_open; settings_save(); break;
    case B_TAB:      S.dock_tab = arg; if (arg == 0) S.focus = 1; break;
    case B_NEWCHAT:  chat_new(); S.settings_open = 0; break;
    case B_OPENCHAT: chat_open(arg); S.settings_open = 0; break;
    case B_PERM:     brain_perm(arg >> 4, arg & 15); chat_save(S.cur); break;
    case B_PLUG:     if (arg >= 0 && arg < S.nplug) S.plug_sel[arg] = !S.plug_sel[arg]; break;
    case B_THEME:    S.theme = arg; settings_save(); break;
    case B_SIZE:     S.big = arg; settings_save(); break;
    case B_LANG:     S.lang_en = arg; settings_save(); break;
    case B_REVOKE:   S.always[arg] = 0; settings_save(); break;
    case B_MEMDEL:
        if (arg >= 0 && arg < S.nmem) { memmove(&S.mem[arg], &S.mem[arg + 1], sizeof(MemEnt) * (size_t)(S.nmem - arg - 1)); S.nmem--; mem_save(); }
        break;
    case B_CLOSE:    S.settings_open = 0; break;
    case B_SCROLL:   S.scroll += arg * 60; if (S.scroll < 0) S.scroll = 0; break;
    case B_PROJ:     S.view = V_CHAT; project_open(arg); chat_save(S.cur); break;
    case B_INPUT:    S.focus = 0; break;
    case B_TINPUT:   S.focus = 1; break;
    case B_SEND:     send_input(); break;
    case B_SAMPLE:   S.focus = 0; brain_sample(arg); chat_save(S.cur); break;
    default: break;
    }
}

static int quit_flag;

static void on_key(int k)
{
    char *buf = S.focus ? S.tinp : S.inp;
    size_t n = strlen(buf);
    if (k == 27) { if (S.settings_open) S.settings_open = 0; else quit_flag = 1; return; }
    if (k == 9)  { S.focus = !S.focus; if (S.focus) { S.dock_open = 1; S.dock_tab = 0; } return; }
    if (k == 0x107) { S.scroll += 160; return; }                  /* PgUp */
    if (k == 0x108) { S.scroll -= 160; if (S.scroll < 0) S.scroll = 0; return; }
    if (k == 10 || k == 13) { if (S.focus) run_terminal(); else send_input(); return; }
    if (k == 8 || k == 127) {
        while (n && (buf[n - 1] & 0xC0) == 0x80) buf[--n] = 0;
        if (n) buf[n - 1] = 0;
        return;
    }
    if (k >= 32 && k < 127 && n + 1 < INP_LEN) { buf[n] = (char)k; buf[n + 1] = 0; return; }
    if (k >= 0xA0 && k < 0x800 && n + 3 < INP_LEN) {              /* a letter outside ASCII arrives as its Unicode number: stored as UTF-8 */
        if (k < 0x80) buf[n++] = (char)k;
        else { buf[n++] = (char)(0xC0 | (k >> 6)); buf[n++] = (char)(0x80 | (k & 63)); }
        buf[n] = 0;
    }
}

int main(int argc, char **argv)
{
    (void)argc; (void)argv;
    A = __api;
    memset(&S, 0, sizeof S);
    if (!A->win_open("FroggyCode", 900, 620)) {
        A->puts("FroggyCode needs the graphical desktop (it is a window program).\n");
        return 1;
    }
    srand(A->ms());
    settings_load();
    mem_load();
    chats_load();
    chat_new();
    S.view = V_CHAT;
    refresh_lists();
    log_con(L_INFO, "%s", tr("FroggyCode 2.0 hazır", "FroggyCode 2.0 is ready"));
    term_add(tr("AST terminali. Komut yaz, Enter'a bas. (Tab: sohbet <-> terminal)", "AST terminal. Type a command and press Enter. (Tab: chat <-> terminal)"));
    A->win_size(&ui_W, &ui_H);

    for (;;) {
        redraw_now();
        int ev[4];
        int r = A->win_event(ev, 70);
        if (r < 0) break;
        if (r == 0) { ui_tick(); continue; }
        do {
            if (ev[0] == AS_EV_CLOSE) goto done;
            if (ev[0] == AS_EV_KEY) { on_key(ev[3]); if (quit_flag) goto done; }
            else if (ev[0] == AS_EV_DOWN && (ev[3] & 1)) on_click(ev[1], ev[2]);
            r = A->win_event(ev, 0);                              /* everything that is waiting, then one redraw */
        } while (r > 0);
        if (r < 0) break;
    }
done:
    for (int i = 0; i < S.nchats; i++) if (S.chats[i].n) chat_save(i);
    A->win_close();
    return 0;
}
