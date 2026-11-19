/*
 * FroggyCode 2.0 - a native C program for AstraSageOS (uses native/as_api.h and the AstraSage libc).
 * Files: main.c (events, chats, saving), ui.c (everything that is drawn), brain.c (what Froggy answers and does), templates.h (example programs).
 */
#ifndef FG_H
#define FG_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "as_api.h"

extern const as_api *A;

/* ---------------------------------------------------------------- limits */
#define MAX_CHATS   10
#define MAX_MSGS    40
#define MSG_LEN     520
#define MAX_MEM     40
#define MEM_KEY     56
#define MEM_VAL     200
#define TERM_LINES  60
#define CON_LINES   60
#define LINE_LEN    120
#define INP_LEN     230
#define MAX_PLUG    16

/* ---------------------------------------------------------------- data */
enum { R_USER = 0, R_FROG = 1, R_ASK = 2 };           /* message roles (R_ASK = a permission question with buttons) */
typedef struct { char role; char text[MSG_LEN]; } Msg;
typedef struct { int id; char title[40]; Msg m[MAX_MSGS]; int n; int perm_chat[4]; } Chat;

enum { P_FILES = 0, P_TERM = 1, P_WEB = 2, P_PLUG = 3, P_COUNT = 4 };
enum { V_CHAT = 0, V_PROJECTS = 1, V_PLUGINS = 2 };
enum { TH_DARK = 0, TH_LIGHT = 1, TH_GREEN = 2 };
enum { L_OK = 0, L_ERR = 1, L_INFO = 2 };             /* console line kinds */

typedef struct { char key[MEM_KEY]; char val[MEM_VAL]; } MemEnt;

typedef struct {
    int theme, big, lang_en, panel, dock_open, dock_tab;          /* settings */
    int always[P_COUNT];                                          /* "Her zaman" grants */
    MemEnt mem[MAX_MEM]; int nmem;
    Chat chats[MAX_CHATS]; int nchats, cur;
    int view, settings_open, scroll, focus;                       /* focus 0 = chat input, 1 = terminal input */
    char inp[INP_LEN]; char tinp[INP_LEN];
    char term[TERM_LINES][LINE_LEN]; int nterm;
    char con[CON_LINES][LINE_LEN]; char conk[CON_LINES]; int ncon;
    int plug_sel[MAX_PLUG]; char plug_name[MAX_PLUG][32]; int nplug;
    char proj[16][32]; int nproj;
    int thinking;
    char teach_q[MEM_KEY];                                        /* Froggy asked the user to explain this */
} State;

extern State S;

/* ---------------------------------------------------------------- main.c */
void log_con(int kind, const char *fmt, ...);
void term_add(const char *s);
void chat_add(int role, const char *text);
Chat *chat_cur(void);
void chat_new(void);
void chat_open(int i);
void chat_save(int i);
void mem_save(void);
void settings_save(void);
void refresh_lists(void);
void redraw_now(void);                                           /* draws and shows the window once (before a slow job) */
int  exec_cmd(const char *cmd, char *out, int cap);              /* runs an AST command through the kernel (output captured) */

/* ---------------------------------------------------------------- ui.c */
typedef struct { int x, y, w, h, id, arg; } Btn;
enum { B_NONE = 0, B_VIEW, B_SETTINGS, B_PANEL, B_DOCK, B_TAB, B_NEWCHAT, B_OPENCHAT, B_PERM, B_PLUG, B_THEME, B_SIZE, B_LANG,
       B_REVOKE, B_MEMDEL, B_CLOSE, B_SCROLL, B_PROJ, B_INPUT, B_TINPUT, B_SEND, B_SAMPLE };
void ui_draw(void);
int  ui_hit(int x, int y, int *arg);
void ui_tick(void);
extern int ui_W, ui_H;

/* ---------------------------------------------------------------- brain.c */
void brain_say(const char *user_text);                           /* the user sent a message */
void brain_perm(int cat, int mode);                              /* answer of a permission card: 0 = reject, 1 = this chat, 2 = always */
void brain_sample(int i);                                        /* "give me an example" buttons */
const char *brain_perm_name(int cat);
const char *tr(const char *tr_text, const char *en_text);        /* UI text in the chosen language */
void fmt_time(char *out, int cap);
void project_open(int i);

#endif
