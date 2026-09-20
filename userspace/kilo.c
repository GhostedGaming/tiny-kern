/* kilo -- a text editor in less than 1000 lines
 *
 * Based on antirez's kilo (public domain). Ported to tiny-kern:
 *   - No ioctl: lone-ESC detection uses the tty's VTIME timed reads
 *     (a read returning 0 = no more bytes in the window).
 *   - Window size comes from SYS_TTYINFO instead of TIOCGWINSZ.
 *   - No SIGWINCH (fixed-size tty).
 */

#define _DEFAULT_SOURCE 1
#define _GNU_SOURCE 1

#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>
#include <termios.h>
#include <abi/syscalls.h>

#define KILO_VERSION "0.0.1"
#define KILO_TAB_STOP 8
#define KILO_QUIT_TIMES 3

#define ctrlKey(k) ((k) & 0x1f)

enum editorKey {
    BACKSPACE = 127,
    ARROW_LEFT = 1000,
    ARROW_RIGHT,
    ARROW_UP,
    ARROW_DOWN,
    DEL_KEY,
    HOME_KEY,
    END_KEY,
    PAGE_UP,
    PAGE_DOWN
};

/* ---- tiny-kern platform shim: direct termios / ttyinfo syscalls ---- */

typedef long scw;
extern long __do_syscall_ret(unsigned long);
extern scw __do_syscall1(long, scw);
extern scw __do_syscall2(long, scw, scw);

static int shim_getattr(struct termios *t) {
    return (int)__do_syscall_ret(
        (unsigned long)__do_syscall2(SYS_TCGETATTR, (scw)t, 0));
}

static int shim_setattr(struct termios *t) {
    return (int)__do_syscall_ret(
        (unsigned long)__do_syscall2(SYS_TCSETATTR, (scw)t, 0));
}

static int shim_getinfo(unsigned int *rows, unsigned int *cols) {
    struct { unsigned int rows; unsigned int cols; } ti;
    if ((int)__do_syscall_ret(
            (unsigned long)__do_syscall1(SYS_TTYINFO, (scw)&ti)) != 0)
        return -1;
    *rows = ti.rows;
    *cols = ti.cols;
    return 0;
}

#define tcgetattr(fd, t) shim_getattr((t))
#define tcsetattr(fd, a, t) shim_setattr((t))

/* ---- data ---- */

typedef struct erow {
    int size;
    int rsize;
    char *chars;
    char *render;
    unsigned char *hl;
    int hl_oc;
} erow;

struct editorSyntax {
    const char **filematch;
    const char **keywords;
    char singleline_comment_start[2];
    char multiline_comment_start[3];
    char multiline_comment_end[3];
    int flags;
};

typedef struct editorConfig {
    int cx, cy;
    int rx;
    int rowoff;
    int coloff;
    int screenrows;
    int screencols;
    int numrows;
    erow *row;
    int dirty;
    char *filename;
    char statusmsg[80];
    time_t statusmsg_time;
    struct editorSyntax *syntax;
} editorConfig;

static editorConfig E;

struct abuf {
    char *b;
    int len;
};

#define ABUF_INIT {NULL, 0}

/* ---- ASCII-only character classes (mlibc's locale-aware ctype needs a
 * locale; avoid it entirely) ---- */

static int k_is_space(int c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\v' || c == '\f' || c == '\r';
}

static int k_is_digit(int c) {
    return c >= '0' && c <= '9';
}

static int k_is_ctrl(int c) {
    return (c >= 0 && c <= 31) || c == 127;
}

/* ---- syntax highlighting constants ---- */

#define HL_NORMAL     0
#define HL_COMMENT    1
#define HL_MLCOMMENT  2
#define HL_KEYWORD    3
#define HL_STRING     4
#define HL_NUMBER     5

#define HL_HIGHLIGHT_NUMBERS (1<<0)
#define HL_HIGHLIGHT_STRINGS (1<<1)

static const char *C_HL_extensions[] = {
    ".c", ".h", ".cpp", ".hpp", ".cc", ".hh", ".m", ".mm",
    NULL
};

static const char *C_HL_keywords[] = {
    "switch", "if", "while", "for", "break", "continue", "return",
    "else", "struct", "union", "typedef", "static", "enum", "class",
    "case", "int", "long", "double", "float", "char", "unsigned",
    "signed", "void", "const", "size_t", "uint8_t", "uint16_t",
    "uint32_t", "uint64_t", "int8_t", "int16_t", "int32_t", "int64_t",
    NULL
};

static struct editorSyntax HLDB[] = {
    {
        C_HL_extensions,
        C_HL_keywords,
        "//", "/*", "*/",
        HL_HIGHLIGHT_NUMBERS | HL_HIGHLIGHT_STRINGS
    },
};

#define HLDB_ENTRIES (sizeof(HLDB)/sizeof(HLDB[0]))

/* ---- prototypes ---- */

static void editorSetStatusMessage(const char *fmt, ...);
static void editorRefreshScreen(void);
static void editorUpdateRow(erow *row);
static void editorUpdateSyntax(erow *row);
static char *editorPrompt(char *prompt, void (*callback)(char *, int));

/* ---- terminal ---- */

static void die(const char *s) {
    write(STDOUT_FILENO, "\x1b[2J\x1b[H", 7);
    write(STDOUT_FILENO, "\x1b[H", 3);
    perror(s);
    exit(1);
}

static void editorAtExit(void) {
    struct termios raw;
    if (shim_getattr(&raw) == 0) {
        raw.c_lflag |= (ISIG | ICANON | ECHO);
        raw.c_iflag |= (IXON | ICRNL);
        shim_setattr(&raw);
    }
    write(STDOUT_FILENO, "\x1b[?25h", 6);
    write(STDOUT_FILENO, "\x1b[H", 3);
    write(STDOUT_FILENO, "\x1b[2J", 4);
}

static int enableRawMode(int fd) {
    (void)fd;
    struct termios raw;
    if (tcgetattr(fd, &raw) == -1) goto fatal;
    if (atexit(editorAtExit) != 0) goto fatal;
    raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
    raw.c_oflag &= ~(OPOST);
    raw.c_cflag |= (CS8);
    raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 1;
    if (tcsetattr(fd, TCSAFLUSH, &raw) < 0) goto fatal;
    return 0;
fatal:
    return -1;
}

static int editorReadKey(void) {
    int nread;
    char c, seq[3];
    while ((nread = read(STDIN_FILENO, &c, 1)) != 1) {
        if (nread == -1 && errno != EAGAIN) die("read");
    }

    if (c == '\x1b') {
        /* No FIONREAD here: raw mode sets VTIME, so the continuation
         * reads below return 0 (timeout) when the ESC was alone. */
        if (read(STDIN_FILENO, &seq[0], 1) != 1) return '\x1b';
        if (read(STDIN_FILENO, &seq[1], 1) != 1) return '\x1b';

        if (seq[0] == '[') {
            if (seq[1] >= '0' && seq[1] <= '9') {
                if (read(STDIN_FILENO, &seq[2], 1) != 1) return '\x1b';
                if (seq[2] == '~') {
                    switch (seq[1]) {
                        case '3': return DEL_KEY;
                        case '5': return PAGE_UP;
                        case '6': return PAGE_DOWN;
                    }
                }
            } else {
                switch (seq[1]) {
                    case 'A': return ARROW_UP;
                    case 'B': return ARROW_DOWN;
                    case 'C': return ARROW_RIGHT;
                    case 'D': return ARROW_LEFT;
                    case 'H': return HOME_KEY;
                    case 'F': return END_KEY;
                }
            }
        } else if (seq[0] == 'O') {
            switch (seq[1]) {
                case 'H': return HOME_KEY;
                case 'F': return END_KEY;
            }
        }
        return '\x1b';
    }

    return c;
}

static int getWindowSize(int ifd, int ofd, int *rows, int *cols) {
    (void)ifd;
    (void)ofd;
    unsigned int r = 0, c = 0;
    if (shim_getinfo(&r, &c) != 0 || c == 0 || r == 0)
        return -1;
    *cols = (int)c;
    *rows = (int)r;
    return 0;
}

/* ---- syntax highlighting ---- */

static int is_separator(int c) {
    return c == '\0' || k_is_space(c) || strchr(",.()+-/*=~%<>;[]", c) != NULL;
}

static int editorSyntaxToColor(int hl) {
    switch (hl) {
        case HL_COMMENT:
        case HL_MLCOMMENT: return 36;
        case HL_KEYWORD:   return 33;
        case HL_STRING:    return 32;
        case HL_NUMBER:    return 31;
        default:           return 37;
    }
}

static void editorUpdateSyntax(erow *row) {
    row->hl = (unsigned char *)realloc(row->hl, row->rsize);
    memset(row->hl, 0, row->rsize);

    if (E.syntax == NULL) return;

    const char **keywords = E.syntax->keywords;
    const char *scs = E.syntax->singleline_comment_start;
    const char *mcs = E.syntax->multiline_comment_start;
    const char *mce = E.syntax->multiline_comment_end;
    int scs_len = (int)strlen(scs);
    int mcs_len = (int)strlen(mcs);
    int mce_len = (int)strlen(mce);

    int prev_sep = 1;
    int in_string = 0;
    int in_comment = row->hl_oc;
    int i = 0;

    while (i < row->rsize) {
        char c = row->render[i];
        unsigned char prev_hl = (i > 0) ? row->hl[i - 1] : 0;

        if (in_comment) {
            row->hl[i] = HL_COMMENT;
            if (mce_len && strncmp(&row->render[i], mce, mce_len) == 0) {
                if (mce_len == 1)
                    row->hl[i] = HL_COMMENT;
                else
                    row->hl[i] = row->hl[i + 1] = HL_COMMENT;
                i += mce_len;
                in_comment = 0;
                prev_sep = 1;
                continue;
            } else {
                i++;
                continue;
            }
        }

        if (E.syntax->flags & HL_HIGHLIGHT_STRINGS) {
            if (in_string) {
                row->hl[i] = HL_STRING;
                if (c == '\\' && i + 1 < row->rsize) {
                    row->hl[i + 1] = HL_STRING;
                    i += 2;
                    continue;
                }
                if (c == in_string) in_string = 0;
                i++;
                prev_sep = 0;
                continue;
            } else {
                if (c == '"' || c == '\'') {
                    in_string = c;
                    row->hl[i] = HL_STRING;
                    i++;
                    continue;
                }
            }
        }

        if (E.syntax->flags & HL_HIGHLIGHT_NUMBERS) {
            if ((k_is_digit(c) && (prev_sep || prev_hl == HL_NUMBER)) ||
                (c == '.' && prev_hl == HL_NUMBER)) {
                row->hl[i] = HL_NUMBER;
                i++;
                prev_sep = 0;
                continue;
            }
        }

        if (prev_sep) {
            int j;
            for (j = 0; keywords[j]; j++) {
                int klen = (int)strlen(keywords[j]);
                if (keywords[j][0] == c &&
                    strncmp(&row->render[i], keywords[j], klen) == 0 &&
                    is_separator(row->render[i + klen])) {
                    memset(&row->hl[i], HL_KEYWORD, klen);
                    i += klen;
                    break;
                }
            }
            if (keywords[j] != NULL) {
                prev_sep = 0;
                continue;
            }
        }

        if (scs_len && strncmp(&row->render[i], scs, scs_len) == 0) {
            memset(&row->hl[i], HL_COMMENT, row->rsize - i);
            break;
        }

        if (mcs_len && strncmp(&row->render[i], mcs, mcs_len) == 0) {
            memset(&row->hl[i], HL_MLCOMMENT, mcs_len);
            i += mcs_len;
            in_comment = 1;
            continue;
        }

        prev_sep = is_separator(c);
        i++;
    }

    int changed = (row->hl_oc != in_comment);
    row->hl_oc = in_comment;
    if (changed && E.numrows > 1 && row != &E.row[E.numrows - 1])
        editorUpdateSyntax(&row[1]);
}

static void editorSelectSyntaxHighlight(void) {
    E.syntax = NULL;
    if (E.filename == NULL) return;
    char *ext = strrchr(E.filename, '.');
    size_t i;
    for (i = 0; i < HLDB_ENTRIES; i++) {
        struct editorSyntax *s = &HLDB[i];
        size_t j = 0;
        while (s->filematch[j]) {
            int is_ext = (s->filematch[j][0] == '.');
            if ((is_ext && ext && strcmp(ext, s->filematch[j]) == 0) ||
                (!is_ext && strstr(E.filename, s->filematch[j]))) {
                E.syntax = s;
                for (int filerow = 0; filerow < E.numrows; filerow++)
                    editorUpdateSyntax(&E.row[filerow]);
                return;
            }
            j++;
        }
    }
}

/* ---- row operations ---- */

static void editorAppendRow(char *s, size_t len) {
    E.row = (erow *)realloc(E.row, sizeof(erow) * (E.numrows + 1));
    int at = E.numrows;
    E.row[at].size = (int)len;
    E.row[at].chars = (char *)malloc(len + 1);
    memcpy(E.row[at].chars, s, len);
    E.row[at].chars[len] = '\0';
    E.row[at].rsize = 0;
    E.row[at].render = NULL;
    E.row[at].hl = NULL;
    E.row[at].hl_oc = 0;
    E.numrows++;
    editorUpdateRow(&E.row[at]);
}

static void editorUpdateRow(erow *row) {
    int tabs = 0;
    int j;
    for (j = 0; j < row->size; j++)
        if (row->chars[j] == '\t') tabs++;
    free(row->render);
    row->render = (char *)malloc(row->size + tabs * (KILO_TAB_STOP - 1) + 1);
    int idx = 0;
    for (j = 0; j < row->size; j++) {
        if (row->chars[j] == '\t') {
            do {
                row->render[idx++] = ' ';
            } while (idx % KILO_TAB_STOP != 0);
        } else {
            row->render[idx++] = row->chars[j];
        }
    }
    row->render[idx] = '\0';
    row->rsize = idx;
    editorUpdateSyntax(row);
}

static void editorFreeRow(erow *row) {
    free(row->render);
    free(row->chars);
    free(row->hl);
    row->render = NULL;
    row->chars = NULL;
    row->hl = NULL;
}

static void editorDelRow(int at) {
    if (at < 0 || at >= E.numrows) return;
    editorFreeRow(&E.row[at]);
    memmove(&E.row[at], &E.row[at + 1], sizeof(erow) * (E.numrows - at - 1));
    E.numrows--;
}

static void editorRowInsertChar(erow *row, int at, int c) {
    if (at < 0 || at > row->size) at = row->size;
    row->chars = (char *)realloc(row->chars, row->size + 2);
    memmove(&row->chars[at + 1], &row->chars[at], row->size - at + 1);
    row->size++;
    row->chars[at] = (char)c;
    editorUpdateRow(row);
}

static void editorRowAppendString(erow *row, char *s, size_t len) {
    row->chars = (char *)realloc(row->chars, row->size + len + 1);
    memcpy(&row->chars[row->size], s, len);
    row->size += (int)len;
    row->chars[row->size] = '\0';
    editorUpdateRow(row);
}

static void editorRowDelChar(erow *row, int at) {
    if (at < 0 || at >= row->size) return;
    memmove(&row->chars[at], &row->chars[at + 1], row->size - at);
    row->size--;
    editorUpdateRow(row);
}

/* ---- editor operations ---- */

static void editorInsertChar(int c) {
    if (E.cy == E.numrows) {
        editorAppendRow("", 0);
    }
    editorRowInsertChar(&E.row[E.cy], E.cx, c);
    E.cx++;
}

static void editorInsertNewline(void) {
    if (E.cx == 0) {
        editorAppendRow("", 0);
    } else {
        erow *row = &E.row[E.cy];
        editorAppendRow(&row->chars[E.cx], row->size - E.cx);
        row = &E.row[E.cy];
        row->size = E.cx;
        row->chars[row->size] = '\0';
        editorUpdateRow(row);
    }
    E.cy++;
    E.cx = 0;
}

static void editorDelChar(void) {
    if (E.cy == E.numrows) return;
    if (E.cx == 0 && E.cy == 0) return;
    if (E.cx > 0) {
        editorRowDelChar(&E.row[E.cy], E.cx - 1);
        E.cx--;
    } else {
        E.cx = E.row[E.cy - 1].size;
        editorRowAppendString(&E.row[E.cy - 1], E.row[E.cy].chars,
                              E.row[E.cy].size);
        editorDelRow(E.cy);
        E.cy--;
    }
}

/* ---- file I/O ---- */

static void editorOpen(char *filename) {
    free(E.filename);
    E.filename = strdup(filename);
    editorSelectSyntaxHighlight();
    FILE *fp = fopen(filename, "r");
    if (!fp) return;
    char *line = NULL;
    size_t linecap = 0;
    ssize_t linelen;
    while ((linelen = getline(&line, &linecap, fp)) != -1) {
        while (linelen > 0 && (line[linelen - 1] == '\n' ||
                               line[linelen - 1] == '\r'))
            linelen--;
        editorAppendRow(line, (size_t)linelen);
    }
    free(line);
    fclose(fp);
    E.dirty = 0;
}

static void editorSave(void) {
    if (E.filename == NULL) return;
    size_t buflen = 0;
    for (size_t i = 0; i < (size_t)E.numrows; i++)
        buflen += (size_t)E.row[i].size + 1;
    char *buf = (char *)malloc(buflen ? buflen : 1);
    size_t off = 0;
    for (size_t i = 0; i < (size_t)E.numrows; i++) {
        memcpy(buf + off, E.row[i].chars, E.row[i].size);
        off += (size_t)E.row[i].size;
        buf[off++] = '\n';
    }
    int fd = open(E.filename, O_RDWR | O_CREAT | O_TRUNC, 0644);
    if (fd != -1) {
        if (write(fd, buf, off) != (ssize_t)off) {
            editorSetStatusMessage("Can't save! I/O error: %s", strerror(errno));
        } else {
            editorSetStatusMessage("%d bytes written on disk", (int)off);
        }
        close(fd);
    } else {
        editorSetStatusMessage("Can't save! I/O error: %s", strerror(errno));
    }
    free(buf);
    E.dirty = 0;
}

/* ---- find ---- */

static void editorFindCallback(char *query, int key) {
    static int last_match = -1;
    static int direction = 1;
    static int saved_hl_line = -1;
    static char *saved_hl = NULL;

    if (saved_hl) {
        memcpy(E.row[saved_hl_line].hl, saved_hl, E.row[saved_hl_line].rsize);
        free(saved_hl);
        saved_hl = NULL;
        saved_hl_line = -1;
    }

    if (key == '\r' || key == '\x1b') {
        last_match = -1;
        direction = 1;
        return;
    } else if (key == ARROW_RIGHT || key == ARROW_DOWN) {
        direction = 1;
    } else if (key == ARROW_LEFT || key == ARROW_UP) {
        direction = -1;
    } else {
        last_match = -1;
        direction = 1;
    }

    if (last_match == -1) direction = 1;
    int current = last_match;
    for (int i = 0; i < E.numrows; i++) {
        current += direction;
        if (current == -1) current = E.numrows - 1;
        else if (current == E.numrows) current = 0;

        erow *row = &E.row[current];
        char *match = strstr(row->render, query);
        if (match) {
            last_match = current;
            E.cy = current;
            E.cx = (int)(match - row->render);
            E.rowoff = E.numrows;
            saved_hl_line = current;
            saved_hl = (char *)malloc(row->rsize);
            memcpy(saved_hl, row->hl, row->rsize);
            memset(&row->hl[E.cx], HL_MLCOMMENT, strlen(query));
            break;
        }
    }
}

static void editorFind(void) {
    char *query = editorPrompt("Search: %s (Use ESC/Arrows/Enter)",
                               editorFindCallback);
    if (query) {
        free(query);
    }
}

/* ---- append buffer ---- */

static void abAppend(struct abuf *ab, const char *s, int len) {
    char *newb = (char *)realloc(ab->b, ab->len + len);
    if (newb == NULL) return;
    memcpy(newb + ab->len, s, len);
    ab->b = newb;
    ab->len += len;
}

static void abFree(struct abuf *ab) {
    free(ab->b);
}

/* ---- output ---- */

static void editorScroll(void) {
    E.rx = 0;
    if (E.cy < E.numrows) {
        for (int i = 0; i < E.cx; i++) {
            if (E.row[E.cy].chars[i] == '\t') {
                E.rx += (KILO_TAB_STOP - 1) - (E.rx % KILO_TAB_STOP);
            } else {
                E.rx++;
            }
        }
    }

    if (E.cy < E.rowoff) {
        E.rowoff = E.cy;
    }
    if (E.cy >= E.rowoff + E.screenrows) {
        E.rowoff = E.cy - E.screenrows + 1;
    }
    if (E.rx < E.coloff) {
        E.coloff = E.rx;
    }
    if (E.rx >= E.coloff + E.screencols) {
        E.coloff = E.rx - E.screencols + 1;
    }
}

static void editorDrawRows(struct abuf *ab) {
    for (int y = 0; y < E.screenrows; y++) {
        int filerow = y + E.rowoff;
        if (filerow >= E.numrows) {
            if (E.numrows == 0 && y == E.screenrows / 3) {
                char welcome[80];
                int welcomelen = snprintf(welcome, sizeof(welcome),
                    "Kilo editor -- version %s", KILO_VERSION);
                if (welcomelen > E.screencols) welcomelen = E.screencols;
                int padding = (E.screencols - welcomelen) / 2;
                if (padding) {
                    abAppend(ab, "~", 1);
                    padding--;
                }
                while (padding--) abAppend(ab, " ", 1);
                abAppend(ab, welcome, welcomelen);
            } else {
                abAppend(ab, "~", 1);
            }
        } else {
            int len = E.row[filerow].rsize - E.coloff;
            if (len < 0) len = 0;
            if (len > E.screencols) len = E.screencols;
            char *c = E.row[filerow].render + E.coloff;
            unsigned char *hl = E.row[filerow].hl + E.coloff;
            int current_color = -1;
            for (int j = 0; j < len; j++) {
                if (k_is_ctrl((unsigned char)c[j])) {
                    char sym = (c[j] <= 26) ? '@' + c[j] : '?';
                    abAppend(ab, "\x1b[7m", 4);
                    abAppend(ab, &sym, 1);
                    abAppend(ab, "\x1b[m", 3);
                    if (current_color != -1) {
                        char buf[16];
                        int clen = snprintf(buf, sizeof(buf),
                            "\x1b[%dm", current_color);
                        abAppend(ab, buf, clen);
                    }
                } else {
                    int color = editorSyntaxToColor(hl[j]);
                    if (color != current_color) {
                        current_color = color;
                        char buf[16];
                        int clen = snprintf(buf, sizeof(buf),
                            "\x1b[%dm", color);
                        abAppend(ab, buf, clen);
                    }
                    abAppend(ab, &c[j], 1);
                }
            }
            abAppend(ab, "\x1b[39m", 5);
        }

        abAppend(ab, "\x1b[K", 3);
        abAppend(ab, "\r\n", 2);
    }
}

static void editorDrawStatusBar(struct abuf *ab) {
    abAppend(ab, "\x1b[7m", 4);
    char status[80], rstatus[80];
    int len = snprintf(status, sizeof(status), "%.20s - %d lines %s",
        E.filename ? E.filename : "[No Name]", E.numrows,
        E.dirty ? "(modified)" : "");
    int rlen = snprintf(rstatus, sizeof(rstatus), "%d/%d",
        E.cy + 1, E.numrows);
    if (len > E.screencols) len = E.screencols;
    abAppend(ab, status, len);
    while (len < E.screencols) {
        if (E.screencols - len == rlen) {
            abAppend(ab, rstatus, rlen);
            break;
        } else {
            abAppend(ab, " ", 1);
            len++;
        }
    }
    abAppend(ab, "\x1b[m", 3);
    abAppend(ab, "\r\n", 2);
}

static void editorDrawMessageBar(struct abuf *ab) {
    abAppend(ab, "\x1b[K", 3);
    int msglen = (int)strlen(E.statusmsg);
    if (msglen > E.screencols) msglen = E.screencols;
    if (msglen && time(NULL) - E.statusmsg_time < 5)
        abAppend(ab, E.statusmsg, msglen);
}

static void editorRefreshScreen(void) {
    editorScroll();
    struct abuf ab = ABUF_INIT;
    abAppend(&ab, "\x1b[?25l", 6);
    abAppend(&ab, "\x1b[H", 3);
    editorDrawRows(&ab);
    editorDrawStatusBar(&ab);
    editorDrawMessageBar(&ab);
    char buf[32];
    snprintf(buf, sizeof(buf), "\x1b[%d;%dH", (E.cy - E.rowoff) + 1,
             (E.rx - E.coloff) + 1);
    abAppend(&ab, buf, (int)strlen(buf));
    abAppend(&ab, "\x1b[?25h", 6);
    write(STDOUT_FILENO, ab.b, ab.len);
    abFree(&ab);
}

static void editorSetStatusMessage(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(E.statusmsg, sizeof(E.statusmsg), fmt, ap);
    va_end(ap);
    E.statusmsg_time = time(NULL);
}

/* ---- input ---- */

static char *editorPrompt(char *prompt, void (*callback)(char *, int)) {
    size_t bufsize = 128;
    char *buf = (char *)malloc(bufsize);
    size_t buflen = 0;
    buf[0] = '\0';
    while (1) {
        editorSetStatusMessage(prompt, buf);
        editorRefreshScreen();
        int c = editorReadKey();
        if (c == DEL_KEY || c == ctrlKey('h') || c == BACKSPACE) {
            if (buflen != 0) buf[--buflen] = '\0';
        } else if (c == '\x1b') {
            editorSetStatusMessage("");
            if (callback) callback(buf, c);
            free(buf);
            return NULL;
        } else if (c == '\r') {
            if (callback) callback(buf, c);
            return buf;
        } else if (!k_is_ctrl(c) && c < 128) {
            if (buflen == bufsize - 1) {
                bufsize *= 2;
                buf = (char *)realloc(buf, bufsize);
            }
            buf[buflen++] = (char)c;
            buf[buflen] = '\0';
        }
        if (callback) callback(buf, c);
    }
}

static void editorMoveCursor(int key) {
    erow *row = (E.cy >= E.numrows) ? NULL : &E.row[E.cy];
    switch (key) {
        case ARROW_LEFT:
            if (E.cx != 0) {
                E.cx--;
            } else if (E.cy > 0) {
                E.cy--;
                E.cx = E.row[E.cy].size;
            }
            break;
        case ARROW_RIGHT:
            if (row && E.cx < row->size) {
                E.cx++;
            } else if (row && E.cx == row->size) {
                E.cy++;
                E.cx = 0;
            }
            break;
        case ARROW_UP:
            if (E.cy != 0) E.cy--;
            break;
        case ARROW_DOWN:
            if (E.cy < E.numrows) E.cy++;
            break;
    }
    row = (E.cy >= E.numrows) ? NULL : &E.row[E.cy];
    int rowlen = row ? row->size : 0;
    if (E.cx > rowlen) E.cx = rowlen;
}

static void editorProcessKeypress(void) {
    static int quit_times = KILO_QUIT_TIMES;
    int c = editorReadKey();

    switch (c) {
        case '\r':
            editorInsertNewline();
            break;
        case ctrlKey('q'):
            if (E.dirty && quit_times > 0) {
                editorSetStatusMessage(
                    "WARNING!!! File has unsaved changes. "
                    "Press Ctrl-Q %d more times to quit.", quit_times);
                quit_times--;
                return;
            }
            write(STDOUT_FILENO, "\x1b[2J", 4);
            write(STDOUT_FILENO, "\x1b[H", 3);
            exit(0);
            break;
        case ctrlKey('s'):
            editorSave();
            break;
        case HOME_KEY:
            E.cx = 0;
            break;
        case END_KEY:
            if (E.cy < E.numrows)
                E.cx = E.row[E.cy].size;
            break;
        case ctrlKey('f'):
            editorFind();
            break;
        case BACKSPACE:
        case ctrlKey('h'):
        case DEL_KEY:
            if (c == DEL_KEY) editorMoveCursor(ARROW_RIGHT);
            editorDelChar();
            break;
        case PAGE_UP:
        case PAGE_DOWN:
        {
            if (c == PAGE_UP) {
                E.cy -= E.screenrows;
            } else if (c == PAGE_DOWN) {
                E.cy += E.screenrows;
            }
            int times = E.screenrows;
            while (times--)
                editorMoveCursor(c == PAGE_UP ? ARROW_UP : ARROW_DOWN);
        }
            break;
        case ARROW_UP:
        case ARROW_DOWN:
        case ARROW_LEFT:
        case ARROW_RIGHT:
            editorMoveCursor(c);
            break;
        case ctrlKey('l'):
        case '\x1b':
            break;
        default:
            editorInsertChar(c);
            break;
    }
    quit_times = KILO_QUIT_TIMES;
}

/* ---- init ---- */

static void initEditor(void) {
    E.cx = 0;
    E.cy = 0;
    E.rx = 0;
    E.rowoff = 0;
    E.coloff = 0;
    E.numrows = 0;
    E.row = NULL;
    E.dirty = 0;
    E.filename = NULL;
    E.statusmsg[0] = '\0';
    E.statusmsg_time = 0;
    E.syntax = NULL;
    if (getWindowSize(STDIN_FILENO, STDOUT_FILENO, &E.screenrows,
                      &E.screencols) == -1)
        die("getWindowSize");
    E.screenrows -= 2;
}

int main(int argc, char *argv[]) {
    enableRawMode(STDIN_FILENO);
    initEditor();
    if (argc >= 2) {
        editorOpen(argv[1]);
    }
    editorSetStatusMessage("HELP: Ctrl-S = save | Ctrl-Q = quit | Ctrl-F = find");
    while (1) {
        editorRefreshScreen();
        editorProcessKeypress();
    }
    return 0;
}
