/* FroggyCode 2.0 - what Froggy understands, answers and does. No made-up intelligence: a rule-based helper that
 * (1) looks into its saved memory, (2) asks Wikipedia over the internet when the user allows it, (3) otherwise asks the user and remembers the answer. */
#include "fg.h"
#include "templates.h"

typedef struct { int pending; int cat; int act; char arg[INP_LEN]; } Pend;
static Pend PD;
enum { ACT_FILES = 1, ACT_MAKE, ACT_RUN, ACT_TERM, ACT_WEB, ACT_PLUG, ACT_READ };

const char *brain_perm_name(int cat)
{
    switch (cat) {
    case P_FILES: return tr("Dosyalara erişim", "File access");
    case P_TERM:  return tr("Terminal komutları", "Terminal commands");
    case P_WEB:   return tr("İnternette arama", "Web search");
    default:      return tr("Eklentilere erişim", "Plugin access");
    }
}

void fmt_time(char *out, int cap)
{
    unsigned t = A->epoch();
    unsigned d = t / 86400, s = t % 86400;
    int y = 1970;
    for (;;) {
        int leap = (y % 4 == 0 && y % 100 != 0) || y % 400 == 0;
        unsigned yl = leap ? 366 : 365;
        if (d < yl) break;
        d -= yl; y++;
    }
    static const int ml[12] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
    int mo = 0, leap = (y % 4 == 0 && y % 100 != 0) || y % 400 == 0;
    while (mo < 12 && d >= (unsigned)(ml[mo] + (mo == 1 && leap))) { d -= (unsigned)(ml[mo] + (mo == 1 && leap)); mo++; }
    snprintf(out, (size_t)cap, "%04d-%02d-%02d %02d:%02d", y, mo + 1, (int)d + 1, (int)(s / 3600), (int)(s % 3600 / 60));
}

/* ---------------------------------------------------------------- text helpers */
static void lower(char *d, const char *s, size_t cap)
{
    size_t i = 0;
    for (; s[i] && i + 1 < cap; i++) d[i] = (char)tolower((unsigned char)s[i]);
    d[i] = 0;
}

static int has(const char *low, const char *w) { return strstr(low, w) != NULL; }

static void say(const char *s) { chat_add(R_FROG, s); }

static void sayf(const char *tr_t, const char *en_t, const char *a, const char *b)
{
    char buf[MSG_LEN];
    snprintf(buf, sizeof buf, tr(tr_t, en_t), a, b);
    say(buf);
}

/* ---------------------------------------------------------------- memory */
static const char *mem_find(const char *low)
{
    for (int i = 0; i < S.nmem; i++) {
        char k[MEM_KEY];
        lower(k, S.mem[i].key, sizeof k);
        if (k[0] && has(low, k)) return S.mem[i].val;
    }
    return NULL;
}

static void mem_add(const char *key, const char *val)
{
    for (int i = 0; i < S.nmem; i++)
        if (!strcmp(S.mem[i].key, key)) { snprintf(S.mem[i].val, MEM_VAL, "%s", val); mem_save(); return; }
    if (S.nmem == MAX_MEM) { memmove(&S.mem[0], &S.mem[1], sizeof(MemEnt) * (MAX_MEM - 1)); S.nmem--; }
    snprintf(S.mem[S.nmem].key, MEM_KEY, "%s", key);
    snprintf(S.mem[S.nmem].val, MEM_VAL, "%s", val);
    S.nmem++;
    mem_save();
}

/* the subject of a question: "python nedir?" -> "python", "what is a frog" -> "frog" */
static void subject(const char *low, char *out, size_t cap)
{
    static const char *CUT_B[] = { "nedir", "ne demek", "kimdir", "nasıl çalışır", "hakkında", "ara ", "araştır", NULL };
    static const char *CUT_A[] = { "what is ", "what are ", "who is ", "who was ", "search ", "tell me about ", "define ", NULL };
    char t[INP_LEN];
    snprintf(t, sizeof t, "%s", low);
    for (int i = 0; CUT_A[i]; i++) { char *p = strstr(t, CUT_A[i]); if (p) { memmove(t, p + strlen(CUT_A[i]), strlen(p + strlen(CUT_A[i])) + 1); break; } }
    for (int i = 0; CUT_B[i]; i++) { char *p = strstr(t, CUT_B[i]); if (p) { *p = 0; break; } }
    size_t n = strlen(t);
    while (n && (t[n - 1] == ' ' || t[n - 1] == '?' || t[n - 1] == '.' || t[n - 1] == '!')) t[--n] = 0;
    char *s = t;
    while (*s == ' ') s++;
    if (!strncmp(s, "a ", 2)) s += 2;
    else if (!strncmp(s, "an ", 3)) s += 3;
    else if (!strncmp(s, "the ", 4)) s += 4;
    snprintf(out, cap, "%.*s", (int)(cap - 1), s);
}

/* ---------------------------------------------------------------- web: Wikipedia summary */
static void url_title(const char *s, char *out, size_t cap)
{
    size_t o = 0;
    for (; *s && o + 4 < cap; s++) {
        unsigned char c = (unsigned char)*s;
        if (c == ' ') out[o++] = '_';
        else if (isalnum(c) || c == '-' || c == '_' || c == '.') out[o++] = (char)c;
        else { snprintf(out + o, 4, "%%%02X", c); o += 3; }
    }
    out[o] = 0;
}

static int json_extract(const char *j, char *out, size_t cap)
{
    const char *p = strstr(j, "\"extract\":\"");
    if (!p) return 0;
    p += 11;
    size_t o = 0;
    while (*p && *p != '"' && o + 5 < cap) {
        if (*p == '\\') {
            p++;
            if (*p == 'n') { out[o++] = ' '; p++; }
            else if (*p == 'u') {
                unsigned u = 0;
                for (int i = 1; i <= 4; i++) { char c = p[i]; u = u * 16 + (unsigned)(c >= 'a' ? c - 'a' + 10 : c >= 'A' ? c - 'A' + 10 : c - '0'); }
                p += 5;
                if (u < 0x80) out[o++] = (char)u;
                else if (u < 0x800) { out[o++] = (char)(0xC0 | (u >> 6)); out[o++] = (char)(0x80 | (u & 63)); }
                else { out[o++] = (char)(0xE0 | (u >> 12)); out[o++] = (char)(0x80 | ((u >> 6) & 63)); out[o++] = (char)(0x80 | (u & 63)); }
            } else out[o++] = *p++;
        } else out[o++] = *p++;
    }
    out[o] = 0;
    return o > 0;
}

static int web_lookup(const char *subj, char *out, size_t cap)
{
    static unsigned char buf[16384];
    char t[160], url[300];
    url_title(subj, t, sizeof t);
    if (!t[0] || !A->http_get) return 0;
    for (int pass = 0; pass < 2; pass++) {
        int en = pass == 0 ? S.lang_en : !S.lang_en;                      /* the chosen language first, then the other one */
        snprintf(url, sizeof url, "https://%s.wikipedia.org/api/rest_v1/page/summary/%s", en ? "en" : "tr", t);
        unsigned len = 0; int status = 0;
        log_con(L_INFO, "web: %s", url);
        int r = A->http_get(url, buf, sizeof buf - 1, &len, &status);
        if (r >= 0 && status == 200 && len > 0) {
            buf[len] = 0;
            if (json_extract((const char *)buf, out, cap)) { log_con(L_OK, "web: %s ok", en ? "en.wikipedia" : "tr.wikipedia"); return 1; }
        }
        log_con(L_ERR, "web: status %d", status);
    }
    return 0;
}

/* ---------------------------------------------------------------- actions (after the permission is clear) */
static int find_tpl(const char *low)
{
    for (int i = 0; i < NTPLS; i++) {
        char f[40];
        lower(f, TPLS[i].file, sizeof f);
        if (has(low, f)) return i;
    }
    for (int i = 0; i < NTPLS; i++) {
        char n[40];
        lower(n, TPLS[i].name, sizeof n);
        if (has(low, n)) return i;
    }
    if (has(low, "pencere") || has(low, "gui") || has(low, "arayüz")) return 2;
    if (has(low, "döngü") || has(low, "loop") || has(low, "say")) return 1;
    return -1;
}

static void act_make(int ti)
{
    const Tpl *t = &TPLS[ti];
    int r = A->file_put(t->file, t->code, 0);
    if (r < 0) { sayf("%s dosyası yazılamadı.", "Could not write %s.", t->file, ""); log_con(L_ERR, "file: %s", t->file); return; }
    char b[MSG_LEN];
    snprintf(b, sizeof b, tr("%s oluşturuldu: %s.\nÇalıştırmak için bana \"%s çalıştır\" de.", "Created %s: %s.\nTo run it tell me \"run %s\"."),
             t->file, S.lang_en ? t->den : t->dtr, t->file);
    say(b);
    log_con(L_OK, "file: %s", t->file);
    refresh_lists();
}

static void act_files(void)
{
    refresh_lists();
    if (S.nproj == 0) { say(tr("Klasörde henüz .as / .c / .cpp / .txt dosyası yok. \"hello.as örneği ver\" dersen bir tane yazarım.",
                               "There is no .as / .c / .cpp / .txt file yet. Say \"give me hello.as\" and I will write one.")); return; }
    char b[MSG_LEN];
    int n = snprintf(b, sizeof b, "%s", tr("Dosyalarını gördüm:", "Your files:"));
    for (int i = 0; i < S.nproj && n < MSG_LEN - 40; i++) n += snprintf(b + n, sizeof b - (size_t)n, "\n- %s", S.proj[i]);
    say(b);
    log_con(L_OK, "files: %d", S.nproj);
}

static void act_read(const char *name)
{
    static char buf[1200];
    int n = A->file_read(name, buf, sizeof buf - 1);
    if (n < 0) { sayf("%s dosyasını açamadım.", "I could not open %s.", name, ""); log_con(L_ERR, "read: %s", name); return; }
    buf[n] = 0;
    char b[MSG_LEN];
    int lines = 1;
    for (int i = 0; i < n; i++) if (buf[i] == '\n') lines++;
    int strict = !strncmp(buf, "start;", 6);
    snprintf(b, sizeof b, tr("%s: %d satır, %d bayt.%s\n", "%s: %d lines, %d bytes.%s\n"), name, lines, n,
             strict ? tr(" Katı AstraScript düzeninde (start; ... end;).", " Strict AstraScript layout (start; ... end;).") : "");
    size_t used = strlen(b);
    snprintf(b + used, sizeof b - used, "%.*s", (int)(MSG_LEN - used - 2 < 330 ? MSG_LEN - used - 2 : 330), buf);
    say(b);
    log_con(L_OK, "read: %s", name);
}

static void act_term(const char *cmd)
{
    static char out[2048];
    char line[LINE_LEN + 4];
    snprintf(line, sizeof line, "$ %s", cmd);
    term_add(line);
    int r = exec_cmd(cmd, out, sizeof out);
    if (r < 0) { say(tr("Bu komut burada çalışmıyor (AST penceresinde dene).", "That command does not run here (try the AST window).")); log_con(L_ERR, "term: %s", cmd); return; }
    term_add(out);
    char b[MSG_LEN];
    if (!out[0]) snprintf(b, sizeof b, "%s", tr("Komut çalıştı, çıktı boş.", "The command ran; no output."));
    else {
        size_t n = strlen(out);
        while (n && out[n - 1] == '\n') out[--n] = 0;
        snprintf(b, sizeof b, "%s\n%.420s", tr("Komut çalıştı:", "The command ran:"), out);
    }
    say(b);
    S.dock_open = 1; S.dock_tab = 0;
    log_con(L_OK, "term: %s", cmd);
}

static void act_plug(void)
{
    refresh_lists();
    int sel = 0;
    char b[MSG_LEN]; int n = 0;
    b[0] = 0;
    for (int i = 0; i < S.nplug; i++) {
        if (!S.plug_sel[i]) continue;
        sel++;
        static char out[600];
        out[0] = 0;
        if (A->aspi) A->aspi("list", out, sizeof out);
        char cmd[64]; snprintf(cmd, sizeof cmd, "info %s", S.plug_name[i]);
        char info[300] = "";
        if (A->aspi && A->aspi(cmd, out, sizeof out) >= 0) {
            char *e = strchr(out, '\n');
            if (e) *e = 0;
            snprintf(info, sizeof info, "%.120s", out);
        }
        n += snprintf(b + n, sizeof b - (size_t)n, "\n- %s %s", S.plug_name[i], info);
        if (n > MSG_LEN - 160) break;
    }
    if (!sel) { say(tr("Eklentiler sekmesinde (soldaki yapboz simgesi) bana göndermek istediklerini işaretle.", "Tick the plugins you want to send me in the Plugins tab (puzzle icon on the left).")); return; }
    char h[MSG_LEN];
    snprintf(h, sizeof h, "%s%s", tr("Bana gönderdiğin eklentiler:", "Plugins you sent me:"), b);
    say(h);
    log_con(L_OK, "plugins: %d", sel);
}

static void act_web(const char *subj)
{
    char ans[MSG_LEN];
    S.thinking = 1;
    say(tr("İnternette bakıyorum...", "Looking it up on the web..."));
    redraw_now();
    int ok = web_lookup(subj, ans, sizeof ans - 40);
    S.thinking = 0;
    Chat *c = chat_cur();
    c->n--;                                                       /* remove the "looking up" line */
    if (ok) {
        char b[MSG_LEN];
        snprintf(b, sizeof b, "%s\n%.440s", subj, ans);
        say(b);
        snprintf(S.teach_q, MEM_KEY, "%s", "");
    } else {
        snprintf(S.teach_q, MEM_KEY, "%.*s", MEM_KEY - 1, subj);
        char b[MSG_LEN];
        snprintf(b, sizeof b, tr("\"%s\" hakkında internette bir şey bulamadım (ya da bağlantı yok). Bana anlatır mısın? Bir sonraki mesajını Ayarlar > Bellek'e kaydedeceğim.",
                                 "I found nothing about \"%s\" on the web (or there is no connection). Can you tell me? I will save your next message in Settings > Memory."), subj);
        say(b);
    }
}

static void run_action(int act, const char *arg)
{
    char low[INP_LEN]; lower(low, arg, sizeof low);
    switch (act) {
    case ACT_FILES: act_files(); break;
    case ACT_MAKE: { int t = find_tpl(low); if (t >= 0) act_make(t); } break;
    case ACT_RUN: {
        static char src[1200];
        int n = A->file_read(arg, src, sizeof src - 1);
        if (n < 0) { sayf("%s dosyasını açamadım.", "I could not open %s.", arg, ""); log_con(L_ERR, "run: %s", arg); break; }
        src[n] = 0;
        if (strstr(src, "gui_")) {                                 /* a script window would take over Froggy's own window */
            sayf("%s kendi penceresini açıyor; Froggy'nin içinden çalıştıramam. AST'de şunu yaz: as script %s", "%s opens its own window; I cannot run it from inside Froggy. In AST type: as script %s", arg, arg);
            log_con(L_INFO, "run: %s needs its own window", arg);
            break;
        }
        char cmd[INP_LEN + 12];
        snprintf(cmd, sizeof cmd, "cper +x %s", arg);              /* AST wants the execute permission of a script first */
        { static char tmp[300]; exec_cmd(cmd, tmp, sizeof tmp); }
        snprintf(cmd, sizeof cmd, "script %s", arg);
        act_term(cmd);
    } break;
    case ACT_TERM: act_term(arg); break;
    case ACT_WEB: act_web(arg); break;
    case ACT_PLUG: act_plug(); break;
    case ACT_READ: act_read(arg); break;
    }
}

/* ask for the permission first unless it is already given */
static void need(int cat, int act, const char *arg, const char *q_tr, const char *q_en)
{
    if (S.always[cat] || chat_cur()->perm_chat[cat]) { run_action(act, arg); return; }
    PD.pending = 1; PD.cat = cat; PD.act = act;
    snprintf(PD.arg, sizeof PD.arg, "%s", arg);
    char b[MSG_LEN];
    snprintf(b, sizeof b, "%c%s", '0' + cat, tr(q_tr, q_en));
    chat_add(R_ASK, b);
    log_con(L_INFO, "permission asked: %s", brain_perm_name(cat));
}

void brain_perm(int cat, int mode)
{
    Chat *c = chat_cur();
    if (c->n && c->m[c->n - 1].role == R_ASK) c->n--;             /* the card goes away, the answer follows */
    if (!PD.pending) return;
    PD.pending = 0;
    if (mode == 0) { say(tr("Tamam, yapmıyorum.", "Okay, I will not do it.")); log_con(L_INFO, "permission denied: %s", brain_perm_name(cat)); return; }
    if (mode == 2) { S.always[cat] = 1; settings_save(); log_con(L_OK, "permission always: %s", brain_perm_name(cat)); }
    else { c->perm_chat[cat] = 1; log_con(L_OK, "permission this chat: %s", brain_perm_name(cat)); }
    run_action(PD.act, PD.arg);
}

/* ---------------------------------------------------------------- the example buttons on the welcome screen */
void brain_sample(int i)
{
    static const char *TRQ[4] = { "Merhaba", "Dosyalarımı göster", "hello.as örneği ver", "Python nedir?" };
    static const char *ENQ[4] = { "Hello", "Show my files", "give me hello.as", "What is Python?" };
    if (i < 0 || i > 3) return;
    brain_say(S.lang_en ? ENQ[i] : TRQ[i]);
}

void project_open(int i)
{
    if (i < 0 || i >= S.nproj) return;
    char b[80];
    snprintf(b, sizeof b, "%s %s", S.proj[i], tr("dosyasına bak", "look at this file"));
    chat_add(R_USER, b);
    need(P_FILES, ACT_READ, S.proj[i], "Bu dosyayı okumak için dosyalara erişebilir miyim?", "May I access your files to read this file?");
}

/* ---------------------------------------------------------------- the main entry */
static int word_file(const char *t, char *out, size_t cap)        /* a file name in the text: something.ext */
{
    const char *p = t;
    while (*p) {
        while (*p == ' ') p++;
        const char *s = p;
        while (*p && *p != ' ') p++;
        size_t n = (size_t)(p - s);
        if (n > 3 && n < cap) {
            char w[40]; snprintf(w, sizeof w, "%.*s", (int)(n < 39 ? n : 39), s);
            char *d = strrchr(w, '.');
            if (d && (!strcmp(d, ".as") || !strcmp(d, ".txt") || !strcmp(d, ".c") || !strcmp(d, ".cpp"))) { snprintf(out, cap, "%s", w); return 1; }
        }
    }
    return 0;
}

void brain_say(const char *user_text)
{
    chat_add(R_USER, user_text);
    char low[INP_LEN];
    lower(low, user_text, sizeof low);

    if (PD.pending) PD.pending = 0;                               /* a new message replaces an unanswered card */
    {
        Chat *c = chat_cur();
        for (int i = 0; i < c->n - 1; i++) if (c->m[i].role == R_ASK) { memmove(&c->m[i], &c->m[i + 1], sizeof(Msg) * (size_t)(c->n - i - 1)); c->n--; i--; }
    }

    if (S.teach_q[0]) {                                           /* Froggy asked, this is the answer */
        mem_add(S.teach_q, user_text);
        char b[MSG_LEN];
        snprintf(b, sizeof b, tr("Teşekkürler! \"%s\" bilgisini belleğime kaydettim (Ayarlar > Bellek).", "Thanks! I saved \"%s\" in my memory (Settings > Memory)."), S.teach_q);
        say(b);
        S.teach_q[0] = 0;
        return;
    }

    char file[40];
    int has_file = word_file(user_text, file, sizeof file);

    if (has(low, "merhaba") || has(low, "selam") || !strcmp(low, "hello") || !strcmp(low, "hi") || has(low, "hello ") || has(low, "hey")) {
        say(tr("Merhaba! Ben Froggy. Dosyalarını listeleyebilir, örnek program yazabilir, komut çalıştırabilir ve bilmediğim şeyleri internette arayabilirim.",
               "Hello! I am Froggy. I can list your files, write example programs, run commands and look up things I do not know on the web."));
        return;
    }
    if (has(low, "saat") || has(low, "tarih") || has(low, "time") || has(low, "date")) {
        char t[40]; fmt_time(t, sizeof t);
        sayf("Şu an: %s", "Now: %s", t, "");
        return;
    }
    if (has(low, "ayar") || has(low, "settings")) { S.settings_open = 1; say(tr("Ayarlar penceresini açtım.", "I opened the settings.")); return; }
    if (has(low, "teşekkür") || has(low, "sağ ol") || has(low, "thanks") || has(low, "thank you")) { say(tr("Rica ederim!", "You are welcome!")); return; }

    if (has(low, "eklenti") || has(low, "plugin") || has(low, "paket")) {
        need(P_PLUG, ACT_PLUG, "", "Eklentilerine (ASPI paketlerine) bakabilir miyim?", "May I look at your plugins (ASPI packages)?");
        return;
    }
    if ((has(low, "çalıştır") || has(low, "calistir") || has(low, "run ") || has(low, "başlat")) && has_file) {
        need(P_TERM, ACT_RUN, file, "Bu dosyayı çalıştırmak için terminali kullanabilir miyim?", "May I use the terminal to run this file?");
        return;
    }
    if (low[0] == '$' || !strncmp(low, "as ", 3) || !strncmp(low, "komut ", 6) || !strncmp(low, "command ", 8)) {
        const char *cmd = user_text;
        if (cmd[0] == '$') cmd++;
        else if (!strncmp(low, "komut ", 6)) cmd += 6;
        else if (!strncmp(low, "command ", 8)) cmd += 8;
        while (*cmd == ' ') cmd++;
        need(P_TERM, ACT_TERM, cmd, "Bu komutu çalıştırmak için terminali kullanabilir miyim?", "May I use the terminal to run this command?");
        return;
    }
    if (has(low, "örnek") || has(low, "ornek") || has(low, "example") || has(low, "give me") || has(low, "yaz bana") || has(low, "oluştur") || has(low, "create")) {
        if (find_tpl(low) >= 0) {
            need(P_FILES, ACT_MAKE, user_text, "Bu örneği klasörüne yazmak için dosyalara erişebilir miyim?", "May I access your files to write this example into your folder?");
        } else {
            char b[MSG_LEN]; int n = snprintf(b, sizeof b, "%s", tr("Şu örnekleri yazabilirim:", "I can write these examples:"));
            for (int i = 0; i < NTPLS; i++) n += snprintf(b + n, sizeof b - (size_t)n, "\n- %s: %s", TPLS[i].file, S.lang_en ? TPLS[i].den : TPLS[i].dtr);
            say(b);
        }
        return;
    }
    if ((has(low, "göster") || has(low, "dosya") || has(low, "files") || has(low, "list") || has(low, "listele")) && !has_file) {
        need(P_FILES, ACT_FILES, "", "Klasöründeki dosyalara bakabilir miyim?", "May I look at the files in your folder?");
        return;
    }
    if (has_file && (has(low, "bak") || has(low, "oku") || has(low, "read") || has(low, "show") || has(low, "göster") || has(low, "look"))) {
        need(P_FILES, ACT_READ, file, "Bu dosyayı okumak için dosyalara erişebilir miyim?", "May I access your files to read this file?");
        return;
    }

    /* anything else is a question: memory first, then the web, then the user */
    const char *m = mem_find(low);
    if (m) { say(m); log_con(L_OK, "memory hit"); return; }
    char subj[INP_LEN];
    subject(low, subj, sizeof subj);
    if (!subj[0] || strlen(subj) < 2) { say(tr("Bunu anlamadım. \"dosyalarımı göster\", \"hello.as örneği ver\" ya da \"Python nedir?\" gibi şeyler yazabilirsin.", "I did not get that. Try \"show my files\", \"give me hello.as\" or \"What is Python?\".")); return; }
    need(P_WEB, ACT_WEB, subj, "Bunu bilmiyorum. İnternette aramamı ister misin?", "I do not know this. May I search the web?");
}
