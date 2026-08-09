#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <abi/syscalls.h>

typedef long scw;
extern long __do_syscall_ret(unsigned long);
extern scw __do_syscall1(long, scw);
extern scw __do_syscall2(long, scw, scw);

#define ICRNL  0x0100
#define IXON   0x0400
#define ISIG   0x0001
#define ICANON 0x0002
#define ECHO   0x0008

struct termios_user {
    unsigned int c_iflag;
    unsigned int c_oflag;
    unsigned int c_cflag;
    unsigned int c_lflag;
    unsigned char c_line;
    unsigned char c_cc[32];
    unsigned int c_ibaud;
    unsigned int c_obaud;
};

struct ttyinfo {
    unsigned int rows;
    unsigned int cols;
};

static int tty_getattr(struct termios_user *t) {
    return (int)__do_syscall_ret(
        (unsigned long)__do_syscall2(SYS_TCGETATTR, (scw)t, 0));
}

static int tty_setattr(struct termios_user *t) {
    return (int)__do_syscall_ret(
        (unsigned long)__do_syscall2(SYS_TCSETATTR, (scw)t, 0));
}

static int tty_getinfo(struct ttyinfo *i) {
    return (int)__do_syscall_ret(
        (unsigned long)__do_syscall1(SYS_TTYINFO, (scw)i));
}

static char **lines;
static int nlines;
static int cap_lines;

static void out(const char *s) {
    write(1, s, strlen(s));
}

static void move_cursor(int r, int c) {
    char buf[32];
    int n = snprintf(buf, sizeof(buf), "\x1b[%d;%dH", r, c);
    write(1, buf, n);
}

static void add_line(const char *s, int len) {
    if (nlines >= cap_lines) {
        int ncap = cap_lines ? cap_lines * 2 : 64;
        char **nl = (char **)realloc(lines, ncap * sizeof(char *));
        if (!nl) {
            return;
        }
        lines = nl;
        cap_lines = ncap;
    }
    char *p = (char *)malloc(len + 1);
    if (!p) {
        return;
    }
    memcpy(p, s, len);
    p[len] = 0;
    lines[nlines++] = p;
}

static void insert_line(int idx, char *s) {
    if (nlines >= cap_lines) {
        int ncap = cap_lines ? cap_lines * 2 : 64;
        char **nl = (char **)realloc(lines, ncap * sizeof(char *));
        if (!nl) {
            return;
        }
        lines = nl;
        cap_lines = ncap;
    }
    memmove(&lines[idx + 1], &lines[idx], (nlines - idx) * sizeof(char *));
    lines[idx] = s;
    nlines++;
}

static void remove_line(int idx) {
    free(lines[idx]);
    memmove(&lines[idx], &lines[idx + 1], (nlines - idx - 1) * sizeof(char *));
    nlines--;
}

static int load_file(const char *path) {
    int fd = open(path, O_RDONLY);
    if (fd < 0) {
        return -1;
    }
    char *buf = NULL;
    long total = 0;
    char tmp[4096];
    for (;;) {
        long r = read(fd, tmp, sizeof(tmp));
        if (r <= 0) {
            break;
        }
        char *nb = (char *)realloc(buf, total + r);
        if (!nb) {
            if (buf) {
                free(buf);
            }
            close(fd);
            return -1;
        }
        buf = nb;
        memcpy(buf + total, tmp, r);
        total += r;
    }
    close(fd);

    long start = 0;
    for (long i = 0; i <= total; i++) {
        if (i == total || buf[i] == '\n') {
            add_line(buf + start, (int)(i - start));
            start = i + 1;
        }
    }
    if (buf) {
        free(buf);
    }
    if (nlines == 0) {
        add_line("", 0);
    }
    return 0;
}

static int save_file(const char *path) {
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) {
        return -1;
    }
    for (int i = 0; i < nlines; i++) {
        write(fd, lines[i], strlen(lines[i]));
        write(fd, "\n", 1);
    }
    close(fd);
    return 0;
}

static int scr_rows = 24;
static int scr_cols = 80;

static void draw(const char *fname, int cur_line, int cur_col, int insert,
                 int dirty, int scroll, const char *status_extra) {
    out("\x1b[2J\x1b[H");
    for (int r = 0; r < scr_rows - 1; r++) {
        int li = scroll + r;
        move_cursor(r + 1, 1);
        if (li < nlines) {
            write(1, lines[li], strlen(lines[li]));
        }
        out("\x1b[K");
    }
    move_cursor(scr_rows, 1);
    out("\x1b[7m");
    char st[256];
    if (status_extra[0]) {
        snprintf(st, sizeof(st), " %s ", status_extra);
    } else {
        snprintf(st, sizeof(st), " %s %d/%d %s%s %d:%d ",
                 fname, cur_line + 1, nlines,
                 insert ? "INSERT" : "NORMAL",
                 dirty ? " *" : "",
                 cur_line + 1, cur_col + 1);
    }
    write(1, st, strlen(st));
    out("\x1b[K\x1b[0m");
    int row = cur_line - scroll + 1;
    move_cursor(row, cur_col + 1);
}

static int read_key(void) {
    unsigned char c;
    if (read(0, &c, 1) != 1) {
        return 0;
    }
    if (c != 0x1B) {
        return c;
    }
    unsigned char d, e;
    if (read(0, &d, 1) != 1) {
        return 0x1B;
    }
    if (d != '[') {
        return 0x1B;
    }
    if (read(0, &e, 1) != 1) {
        return 0x1B;
    }
    switch (e) {
        case 'A': return -1;
        case 'B': return -2;
        case 'C': return -3;
        case 'D': return -4;
        default: return 0x1B;
    }
}

static int line_len(int li) {
    return (int)strlen(lines[li]);
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        printf("usage: edit <file>\n");
        return 1;
    }
    const char *fname = argv[1];

    struct termios_user old, raw;
    if (tty_getattr(&old) != 0) {
        printf("edit: tcgetattr failed\n");
        return 1;
    }
    raw = old;
    raw.c_lflag &= ~(ICANON | ECHO | ISIG);
    raw.c_iflag &= ~(ICRNL | IXON);
    if (tty_setattr(&raw) != 0) {
        printf("edit: tcsetattr failed\n");
        return 1;
    }

    struct ttyinfo ti;
    if (tty_getinfo(&ti) == 0 && ti.rows > 2 && ti.cols > 4) {
        scr_rows = (int)ti.rows;
        scr_cols = (int)ti.cols;
    }

    if (load_file(fname) != 0) {
        add_line("", 0);
    }

    int cur_line = 0;
    int cur_col = 0;
    int insert = 0;
    int dirty = 0;
    int scroll = 0;
    int last_norm = 0;
    int quit = 0;

    out("\x1b[2J");
    for (;;) {
        draw(fname, cur_line, cur_col, insert, dirty, scroll, "");
        int key = read_key();

        if (key == 0) {
            continue;
        }

        if (insert) {
            if (key == 0x1B) {
                insert = 0;
                if (cur_col > 0) {
                    cur_col--;
                }
            } else if (key == 0x7F || key == 8) {
                if (cur_col > 0) {
                    int n = line_len(cur_line);
                    memmove(&lines[cur_line][cur_col - 1],
                            &lines[cur_line][cur_col], n - cur_col + 1);
                    cur_col--;
                    dirty = 1;
                } else if (cur_line > 0) {
                    int pl = line_len(cur_line - 1);
                    int nl = line_len(cur_line);
                    char *merged = (char *)malloc(pl + nl + 1);
                    if (merged) {
                        memcpy(merged, lines[cur_line - 1], pl);
                        memcpy(merged + pl, lines[cur_line], nl);
                        merged[pl + nl] = 0;
                        free(lines[cur_line - 1]);
                        free(lines[cur_line]);
                        lines[cur_line - 1] = merged;
                        memmove(&lines[cur_line], &lines[cur_line + 1],
                                (nlines - cur_line - 1) * sizeof(char *));
                        nlines--;
                        cur_line--;
                        cur_col = pl;
                        dirty = 1;
                    }
                }
            } else if (key == '\n' || key == '\r') {
                int n = line_len(cur_line);
                char *rest = (char *)malloc(n - cur_col + 1);
                if (rest) {
                    memcpy(rest, &lines[cur_line][cur_col], n - cur_col);
                    rest[n - cur_col] = 0;
                    lines[cur_line][cur_col] = 0;
                    insert_line(cur_line + 1, rest);
                    cur_line++;
                    cur_col = 0;
                    dirty = 1;
                }
            } else if (key >= 0x20 && key <= 0x7E) {
                int n = line_len(cur_line);
                char *nb = (char *)malloc(n + 2);
                if (nb) {
                    memcpy(nb, lines[cur_line], cur_col);
                    nb[cur_col] = (char)key;
                    memcpy(nb + cur_col + 1, &lines[cur_line][cur_col],
                           n - cur_col + 1);
                    free(lines[cur_line]);
                    lines[cur_line] = nb;
                    cur_col++;
                    dirty = 1;
                }
            } else if (key == -4 || key == -3) {
                if (key == -3) {
                    if (cur_col < line_len(cur_line)) {
                        cur_col++;
                    }
                } else if (cur_col > 0) {
                    cur_col--;
                }
            } else if (key == -2) {
                if (cur_line + 1 < nlines) {
                    cur_line++;
                    if (cur_col > line_len(cur_line)) {
                        cur_col = line_len(cur_line);
                    }
                }
            } else if (key == -1) {
                if (cur_line > 0) {
                    cur_line--;
                    if (cur_col > line_len(cur_line)) {
                        cur_col = line_len(cur_line);
                    }
                }
            }
        } else {
            if (key == 'i') {
                insert = 1;
            } else if (key == 'a') {
                insert = 1;
                if (cur_col < line_len(cur_line)) {
                    cur_col++;
                }
            } else if (key == 'A') {
                cur_col = line_len(cur_line);
                insert = 1;
            } else if (key == 'I') {
                cur_col = 0;
                insert = 1;
            } else if (key == 'o' || key == 'O') {
                char *n = (char *)malloc(1);
                if (n) {
                    n[0] = 0;
                    if (key == 'o') {
                        insert_line(cur_line + 1, n);
                        cur_line++;
                    } else {
                        insert_line(cur_line, n);
                    }
                    cur_col = 0;
                    dirty = 1;
                    insert = 1;
                }
            } else if (key == 'x') {
                int n = line_len(cur_line);
                if (cur_col < n) {
                    memmove(&lines[cur_line][cur_col],
                            &lines[cur_line][cur_col + 1], n - cur_col);
                    dirty = 1;
                    if (cur_col > line_len(cur_line) && cur_col > 0) {
                        cur_col--;
                    }
                }
            } else if (key == 'X') {
                if (cur_col > 0) {
                    int n = line_len(cur_line);
                    memmove(&lines[cur_line][cur_col - 1],
                            &lines[cur_line][cur_col], n - cur_col + 1);
                    cur_col--;
                    dirty = 1;
                }
            } else if (key == 'd' && last_norm == 'd') {
                remove_line(cur_line);
                if (nlines == 0) {
                    add_line("", 0);
                }
                if (cur_line >= nlines) {
                    cur_line = nlines - 1;
                }
                if (cur_col > line_len(cur_line)) {
                    cur_col = line_len(cur_line);
                }
                dirty = 1;
            } else if (key == 'G') {
                cur_line = nlines - 1;
                cur_col = line_len(cur_line);
            } else if (key == 'g' && last_norm == 'g') {
                cur_line = 0;
                cur_col = 0;
            } else if (key == '0') {
                cur_col = 0;
            } else if (key == '$') {
                cur_col = line_len(cur_line);
            } else if (key == -4) {
                if (cur_col > 0) {
                    cur_col--;
                }
            } else if (key == -3) {
                if (cur_col < line_len(cur_line)) {
                    cur_col++;
                }
            } else if (key == -2) {
                if (cur_line + 1 < nlines) {
                    cur_line++;
                    if (cur_col > line_len(cur_line)) {
                        cur_col = line_len(cur_line);
                    }
                }
            } else if (key == -1) {
                if (cur_line > 0) {
                    cur_line--;
                    if (cur_col > line_len(cur_line)) {
                        cur_col = line_len(cur_line);
                    }
                }
            } else if (key == ':' || key == 'q') {
                char cmd[64];
                int ci = 0;
                if (key == 'q') {
                    cmd[ci++] = 'q';
                } else {
                    cmd[ci++] = ':';
                }
                cmd[ci] = 0;
                for (;;) {
                    draw(fname, cur_line, cur_col, insert, dirty, scroll, cmd);
                    int k = read_key();
                    if (k == '\n' || k == '\r') {
                        break;
                    }
                    if (k == 0x7F || k == 8) {
                        if (ci > 0) {
                            cmd[--ci] = 0;
                        }
                        continue;
                    }
                    if (k == 0x1B) {
                        ci = 0;
                        cmd[0] = 0;
                        goto command_abort;
                    }
                    if (k >= 0x20 && k <= 0x7E && ci < (int)sizeof(cmd) - 1) {
                        cmd[ci++] = (char)k;
                        cmd[ci] = 0;
                    }
                }
                if (cmd[0] == ':') {
                    memmove(cmd, cmd + 1, strlen(cmd));
                }
                if (strcmp(cmd, "w") == 0) {
                    int ok = save_file(fname);
                    if (ok == 0) {
                        dirty = 0;
                    }
                    draw(fname, cur_line, cur_col, insert, dirty, scroll,
                         ok == 0 ? "saved" : "write failed");
                } else if (strcmp(cmd, "q") == 0) {
                    if (!dirty) {
                        quit = 1;
                    } else {
                        draw(fname, cur_line, cur_col, insert, dirty, scroll,
                             "unsaved changes");
                    }
                } else if (strcmp(cmd, "wq") == 0) {
                    if (save_file(fname) == 0) {
                        quit = 1;
                    } else {
                        draw(fname, cur_line, cur_col, insert, dirty, scroll,
                             "write failed");
                    }
                } else if (strcmp(cmd, "q!") == 0) {
                    quit = 1;
                }
            command_abort:
                continue;
            }
        }

        last_norm = (insert ? 0 : key);
        if (cur_line < scroll) {
            scroll = cur_line;
        }
        if (cur_line >= scroll + (scr_rows - 1)) {
            scroll = cur_line - (scr_rows - 1) + 1;
        }

        if (quit) {
            break;
        }
    }

    tty_setattr(&old);
    out("\x1b[2J\x1b[H\x1b[0m");
    printf("edit: %s %s\n", fname, dirty ? "unsaved changes" : "closed");
    return 0;
}
