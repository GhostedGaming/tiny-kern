/*
 * sh.c - TinyKern small POSIX shell
 *
 * Plain cooked-mode line input (the kernel tty provides echo + editing),
 * pipes, redirection, background jobs, word expansion, and builtins.
 *
 * mlibc's tiny_kern port lacks Tcgetattr/Tcsetattr/waitpid sysdeps, so
 * those three go straight to the kernel (see k_* helpers below).
 */

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>

#include <abi/syscalls.h>

extern char **environ;

/* ---------- raw syscall shims (mlibc lacks these sysdeps) ---------- */

typedef long scw;
extern long __do_syscall_ret(unsigned long);
extern scw __do_syscall2(long, scw, scw);
extern scw __do_syscall3(long, scw, scw, scw);

#ifndef WNOHANG
#define WNOHANG 1
#endif
#ifndef WUNTRACED
#define WUNTRACED 2
#endif

static int k_tcgetattr(struct termios *t) {
    return (int)__do_syscall_ret(
        (unsigned long)__do_syscall2(SYS_TCGETATTR, 0, (scw)t));
}

static int k_tcsetattr(const struct termios *t) {
    return (int)__do_syscall_ret(
        (unsigned long)__do_syscall2(SYS_TCSETATTR, 0, (scw)t));
}

static int k_waitpid(int pid, int *status, int options) {
    return (int)__do_syscall_ret(
        (unsigned long)__do_syscall3(SYS_WAITPID, pid, (scw)status, (scw)options));
}

/* ---------- globals ---------- */

#define MAXLINE 4096
#define MAX_TOK 512
#define MAX_CMD 32
#define MAX_REDIR 8
#define MAX_JOBS 16
#define MAX_JPROC 64

static int interactive;
static int shell_pgid;
static int last_status;
static int want_exit = -1;

static const char *home_dir(void) {
    const char *h = getenv("HOME");
    return (h && *h) ? h : "/";
}

static int is_tty_fd(int fd) {
    struct stat st;
    if (fstat(fd, &st) != 0)
        return 0;
    return (st.st_mode & S_IFMT) == S_IFCHR;
}

static void init_environment(void) {
    setenv("PATH", "/ram/bin:/bin", 1);
    setenv("HOME", "/", 1);
    if (!getenv("PWD")) {
        char cwd[512];
        if (getcwd(cwd, sizeof(cwd)) != NULL)
            setenv("PWD", cwd, 1);
        else
            setenv("PWD", "/", 1);
    }
    char shlvl[16];
    const char *l = getenv("SHLVL");
    snprintf(shlvl, sizeof(shlvl), "%d", (l && *l) ? atoi(l) + 1 : 1);
    setenv("SHLVL", shlvl, 1);
}

/* ---------- terminal ---------- */

static void tm_cooked(void) {
    struct termios t;
    if (k_tcgetattr(&t) != 0)
        return;
    t.c_iflag |= ICRNL;
    t.c_iflag &= ~(IXON | BRKINT);
    t.c_oflag |= OPOST;
    t.c_cflag |= CS8;
    t.c_lflag |= ECHO | ECHOE | ICANON | ISIG;
    t.c_lflag &= ~(IEXTEN | ECHOK | ECHONL);
    t.c_cc[VMIN] = 1;
    t.c_cc[VTIME] = 0;
    (void)k_tcsetattr(&t);
}

/* ---------- line input ---------- */

/* Line-buffered reader for fd 0. Handles tty and regular files/pipes, where
 * one read() may return many lines: bytes after the newline are kept for the
 * next call. Returns length (0 = empty line), -1 on error, -2 on EOF. */
static char lbuf[MAXLINE];
static int lb_pos, lb_len;

static int read_line(char *out, int max) {
    int n = 0;
    for (;;) {
        if (lb_pos < lb_len) {
            char c = lbuf[lb_pos++];
            if (c == '\n')
                break;
            if (n >= max - 1)
                break;
            out[n++] = c;
            continue;
        }
        ssize_t r = read(0, lbuf, sizeof(lbuf));
        if (r < 0) {
            if (errno == EINTR)
                continue;
            return -1;
        }
        if (r == 0) {
            if (n > 0 || lb_pos < lb_len) {
                while (lb_pos < lb_len && n < max - 1)
                    out[n++] = lbuf[lb_pos++];
                break;
            }
            return -2; /* EOF on clean line start */
        }
        lb_pos = 0;
        lb_len = (int)r;
    }
    out[n] = '\0';
    return n;
}

/* ---------- signals ---------- */

static void set_handler(int sig, void (*fn)(int)) {
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = fn;
    sigaction(sig, &sa, NULL);
}

static void shell_ignore_signals(void) {
    set_handler(SIGINT, SIG_IGN);
    set_handler(SIGQUIT, SIG_IGN);
    set_handler(SIGTSTP, SIG_IGN);
    set_handler(SIGTTIN, SIG_IGN);
    set_handler(SIGTTOU, SIG_IGN);
    set_handler(SIGPIPE, SIG_IGN);
}

static void child_reset_signals(void) {
    set_handler(SIGINT, SIG_DFL);
    set_handler(SIGQUIT, SIG_DFL);
    set_handler(SIGTSTP, SIG_DFL);
    set_handler(SIGTTIN, SIG_DFL);
    set_handler(SIGTTOU, SIG_DFL);
    set_handler(SIGPIPE, SIG_DFL);
}

/* ---------- tokenizer ---------- */

enum {
    T_WORD = 0,
    T_PIPE,   /* |  */
    T_SEMI,   /* ;  */
    T_AND,    /* && */
    T_OR,     /* || */
    T_BG,     /* &  */
    T_RIN,    /* <  */
    T_ROUT,   /* >  */
    T_RAPP,   /* >> */
    T_R2OUT,  /* 2> */
    T_R2APP   /* 2>> */
};

struct tok {
    int type;
    const char *s; /* for T_WORD: start of the raw slice into the line */
    int len;
};

static int tok_is_redir(int type) {
    return type == T_RIN || type == T_ROUT || type == T_RAPP ||
           type == T_R2OUT || type == T_R2APP;
}

static int tokenize(const char *line, struct tok *toks, int max) {
    int n = 0;
    const char *p = line;

    while (*p && n < max) {
        while (*p == ' ' || *p == '\t')
            p++;
        if (!*p)
            break;

        const char *start = p;
        int type = T_WORD;

        if (*p == ';') {
            type = T_SEMI;
            p++;
        } else if (*p == '&' && p[1] == '&') {
            type = T_AND;
            p += 2;
        } else if (*p == '&') {
            type = T_BG;
            p++;
        } else if (*p == '|' && p[1] == '|') {
            type = T_OR;
            p += 2;
        } else if (*p == '|') {
            type = T_PIPE;
            p++;
        } else if (*p == '<') {
            type = T_RIN;
            p++;
        } else if (*p == '>' && p[1] == '>') {
            type = T_RAPP;
            p += 2;
        } else if (*p == '>') {
            type = T_ROUT;
            p++;
        } else if (*p == '2' && p[1] == '>') {
            if (p[2] == '>') {
                type = T_R2APP;
                p += 3;
            } else {
                type = T_R2OUT;
                p += 2;
            }
        } else {
            /* word: quotes keep scanning; backslash escapes next char */
            char quote = 0;
            while (*p) {
                char c = *p;
                if (quote == '\'') {
                    if (c == '\'')
                        quote = 0;
                    p++;
                    continue;
                }
                if (quote == '"') {
                    if (c == '"')
                        quote = 0;
                    p++;
                    continue;
                }
                if (c == '\'' || c == '"') {
                    quote = c;
                    p++;
                    continue;
                }
                if (c == '\\') {
                    if (p[1])
                        p += 2;
                    else
                        p++;
                    continue;
                }
                if (c == ';' || c == '&' || c == '|' || c == '<' || c == '>')
                    break;
                if (c == ' ' || c == '\t')
                    break;
                p++;
            }
        }

        toks[n].type = type;
        toks[n].s = start;
        toks[n].len = (int)(p - start);
        n++;
    }

    /* a redirection operator with no target is dropped */
    for (int i = 0; i < n; i++) {
        if (tok_is_redir(toks[i].type) &&
            (i + 1 >= n || toks[i + 1].type != T_WORD)) {
            memmove(&toks[i], &toks[i + 1],
                    (size_t)(n - i - 1) * sizeof(struct tok));
            n--;
            i--;
        }
    }
    return n;
}

/* ---------- expansion ---------- */

static void expand_var(const char **pp, const char *end, char *out, int *op, int maxout) {
    const char *p = *pp;
    p++; /* skip '$' */

    if (p < end && *p == '{') {
        char name[64];
        int i = 0;
        p++;
        while (p < end && *p != '}' && i < 63)
            name[i++] = *p++;
        name[i] = '\0';
        if (p < end)
            p++;
        const char *v = getenv(name);
        if (!v)
            v = "";
        while (*v && *op < maxout - 1)
            out[(*op)++] = *v++;
        *pp = p;
        return;
    }

    if (p < end && *p == '?') {
        char nb[16];
        snprintf(nb, sizeof(nb), "%d", last_status);
        for (int i = 0; nb[i] && *op < maxout - 1; i++)
            out[(*op)++] = nb[i];
        p++;
        *pp = p;
        return;
    }

    char name[64];
    int i = 0;
    while (p < end && (isalnum((unsigned char)*p) || *p == '_') && i < 63)
        name[i++] = *p++;
    name[i] = '\0';
    if (i == 0) {
        if (*op < maxout - 1)
            out[(*op)++] = '$';
        *pp = p;
        return;
    }
    const char *v = getenv(name);
    if (!v)
        v = "";
    while (*v && *op < maxout - 1)
        out[(*op)++] = *v++;
    *pp = p;
}

/* expand a raw token slice into a malloc'd string */
static char *expand_word(const char *s, int len) {
    const char *end = s + len;
    const char *p = s;
    size_t maxout = MAXLINE * 2;
    char *out = malloc(maxout);
    if (!out)
        return strdup("");
    int op = 0;
    int first = 1;

    while (p < end) {
        char c = *p;
        if (c == '\'') {
            first = 0;
            p++;
            while (p < end && *p != '\'') {
                if (op < (int)maxout - 1)
                    out[op++] = *p;
                p++;
            }
            if (p < end)
                p++;
            continue;
        }
        if (c == '"') {
            first = 0;
            p++;
            while (p < end && *p != '"') {
                if (*p == '\\' && p + 1 < end &&
                    (p[1] == '"' || p[1] == '\\' || p[1] == '$')) {
                    p++;
                    if (op < (int)maxout - 1)
                        out[op++] = *p;
                    p++;
                    continue;
                }
                if (*p == '$') {
                    expand_var(&p, end, out, &op, (int)maxout);
                    continue;
                }
                if (op < (int)maxout - 1)
                    out[op++] = *p;
                p++;
            }
            if (p < end)
                p++;
            continue;
        }
        if (c == '\\') {
            first = 0;
            if (p + 1 < end) {
                p++;
                if (op < (int)maxout - 1)
                    out[op++] = *p;
                p++;
            } else {
                p++;
            }
            continue;
        }
        if (c == '~' && first && op == 0) {
            first = 0;
            const char *h = home_dir();
            while (*h && op < (int)maxout - 1)
                out[op++] = *h++;
            p++;
            continue;
        }
        if (c == '$') {
            first = 0;
            expand_var(&p, end, out, &op, (int)maxout);
            continue;
        }
        first = 0;
        if (op < (int)maxout - 1)
            out[op++] = *p;
        p++;
    }
    out[op] = '\0';
    return out;
}

/* ---------- parsing ---------- */

struct redir {
    int fd;
    int flags; /* open flags */
    char *target;
};

struct cmd {
    char **argv;
    int nargv;
    struct redir redirs[MAX_REDIR];
    int nredir;
};

struct pipeline {
    struct cmd cmds[MAX_CMD];
    int ncmd;
    int bg;
};

static int cmd_add_arg(struct cmd *c, const char *s) {
    char **na = realloc(c->argv, sizeof(char *) * (size_t)(c->nargv + 2));
    if (!na)
        return -1;
    c->argv = na;
    c->argv[c->nargv] = strdup(s);
    c->argv[c->nargv + 1] = NULL;
    c->nargv++;
    return 0;
}

static int cmd_add_redir(struct cmd *c, int type, const char *target) {
    if (c->nredir >= MAX_REDIR)
        return -1;
    struct redir *r = &c->redirs[c->nredir++];
    r->fd = 1;
    r->flags = O_WRONLY | O_CREAT | O_TRUNC;
    if (type == T_RIN) {
        r->fd = 0;
        r->flags = O_RDONLY;
    } else if (type == T_RAPP) {
        r->flags = O_WRONLY | O_CREAT | O_APPEND;
    } else if (type == T_R2OUT) {
        r->fd = 2;
    } else if (type == T_R2APP) {
        r->fd = 2;
        r->flags = O_WRONLY | O_CREAT | O_APPEND;
    }
    r->target = strdup(target);
    return 0;
}

static void cmd_free(struct cmd *c) {
    for (int i = 0; i < c->nargv; i++)
        free(c->argv[i]);
    free(c->argv);
    c->argv = NULL;
    c->nargv = 0;
    for (int i = 0; i < c->nredir; i++)
        free(c->redirs[i].target);
    c->nredir = 0;
}

static void pipe_free(struct pipeline *p) {
    for (int i = 0; i < p->ncmd; i++)
        cmd_free(&p->cmds[i]);
    p->ncmd = 0;
}

/* parse one pipeline from toks[*ip]; advances *ip past pipe/sep/bg tokens.
 * returns: 1 parsed a non-empty segment, 0 empty segment, -1 syntax error */
static int parse_pipeline(struct tok *toks, int nt, int *ip, struct pipeline *out) {
    memset(out, 0, sizeof(*out));

    for (;;) {
        if (out->ncmd >= MAX_CMD)
            return -1;
        struct cmd *c = &out->cmds[out->ncmd];
        memset(c, 0, sizeof(*c));

        int has_more = 0;
        for (;;) {
            if (*ip >= nt)
                break;
            struct tok *t = &toks[*ip];

            if (t->type == T_WORD) {
                char *w = expand_word(t->s, t->len);
                cmd_add_arg(c, w);
                free(w);
                (*ip)++;
                continue;
            }
            if (tok_is_redir(t->type)) {
                int ty = t->type;
                (*ip)++;
                if (*ip < nt && toks[*ip].type == T_WORD) {
                    char *w = expand_word(toks[*ip].s, toks[*ip].len);
                    cmd_add_redir(c, ty, w);
                    free(w);
                    (*ip)++;
                } else {
                    pipe_free(out);
                    return -1;
                }
                continue;
            }
            if (t->type == T_PIPE) {
                (*ip)++;
                has_more = 1;
                break;
            }
            break;
        }

        out->ncmd++;
        if (!has_more)
            break;
    }

    /* empty segment (e.g. stray ';') */
    if (out->ncmd == 1 && out->cmds[0].nargv == 0 && out->cmds[0].nredir == 0) {
        out->ncmd = 0;
        return 0;
    }
    /* a middle command in a pipe must have a program */
    for (int i = 0; i < out->ncmd; i++)
        if (out->cmds[i].nargv == 0 && out->cmds[i].nredir == 0) {
            pipe_free(out);
            return -1;
        }
    return 1;
}

/* ---------- job table ---------- */

struct jproc {
    int pid;
    int state; /* 0 running, 1 stopped, 2 done */
};

struct job {
    int used;
    int isbg;
    int pgid;
    int running;
    int stopped;
    struct jproc procs[MAX_JPROC];
    int nprocs;
    char cmdline[128];
};

static struct job jobs[MAX_JOBS];

static int job_number(const struct job *jb) {
    for (int i = 0; i < MAX_JOBS; i++)
        if (&jobs[i] == jb)
            return i + 1;
    return 0;
}

static void job_record(struct job *jb, int pid) {
    if (jb->nprocs >= MAX_JPROC)
        return;
    jb->procs[jb->nprocs].pid = pid;
    jb->procs[jb->nprocs].state = 0;
    jb->nprocs++;
    jb->running++;
}

static void job_apply(struct job *jb, int pid, int st) {
    for (int i = 0; i < jb->nprocs; i++) {
        if (jb->procs[i].pid != pid)
            continue;
        if (WIFSTOPPED(st)) {
            if (jb->procs[i].state != 1) {
                jb->stopped++;
                jb->running--;
            }
            jb->procs[i].state = 1;
        } else if (WIFCONTINUED(st)) {
            if (jb->procs[i].state == 1) {
                jb->stopped--;
                jb->running++;
            }
            jb->procs[i].state = 0;
        } else {
            if (jb->procs[i].state != 2)
                jb->running--;
            jb->procs[i].state = 2;
        }
        break;
    }
}

static void job_print(const struct job *jb) {
    if (jb->stopped > 0)
        printf("[%d]  Stopped  %s\n", job_number(jb), jb->cmdline);
    else if (jb->running > 0)
        printf("[%d]  Running  %s\n", job_number(jb), jb->cmdline);
    else
        printf("[%d]  Done     %s\n", job_number(jb), jb->cmdline);
}

static void job_remove(struct job *jb) {
    jb->used = 0;
}

/* non-blocking reap of finished/stopped background jobs */
static void check_background(void) {
    for (int i = 0; i < MAX_JOBS; i++) {
        struct job *jb = &jobs[i];
        if (!jb->used || !jb->isbg)
            continue;
        for (;;) {
            int st = 0;
            int r = k_waitpid(-jb->pgid, &st, WNOHANG | WUNTRACED);
            if (r < 0 && errno == EINTR)
                continue;
            if (r <= 0)
                break;
            if (WIFSTOPPED(st)) {
                job_apply(jb, r, st);
            } else {
                job_apply(jb, r, st);
                if (WIFSIGNALED(st))
                    printf("\r\n[%d]  Killed   %s\n", job_number(jb), jb->cmdline);
            }
        }
        if (jb->running <= 0 && jb->stopped <= 0) {
            printf("\r\n[%d]  Done     %s\n", job_number(jb), jb->cmdline);
            job_remove(jb);
        } else if (jb->running <= 0 && jb->stopped > 0) {
            jb->isbg = 0;
            printf("\r\n[%d]  Stopped  %s\n", job_number(jb), jb->cmdline);
        }
    }
}

/* ---------- builtins ---------- */

static void cmd_echo(char **argv) {
    int nflag = 0;
    int i = 1;
    if (argv[1] && strcmp(argv[1], "-n") == 0) {
        nflag = 1;
        i = 2;
    }
    for (; argv[i]; i++) {
        if (i > (nflag ? 2 : 1))
            write(1, " ", 1);
        write(1, argv[i], strlen(argv[i]));
    }
    if (!nflag)
        write(1, "\n", 1);
}

static void cmd_cd(char **argv) {
    const char *dir;
    if (argv[1] && strcmp(argv[1], "-") == 0) {
        const char *old = getenv("OLDPWD");
        dir = (old && *old) ? old : "/";
    } else if (argv[1]) {
        dir = argv[1];
    } else {
        dir = home_dir();
    }
    char prev[512];
    int have_prev = getcwd(prev, sizeof(prev)) != NULL;
    if (chdir(dir) != 0) {
        fprintf(stderr, "sh: cd %s: %s\n", dir, strerror(errno));
        return;
    }
    if (have_prev)
        setenv("OLDPWD", prev, 1);
    char cwd[512];
    if (getcwd(cwd, sizeof(cwd)) != NULL)
        setenv("PWD", cwd, 1);
}

static void cmd_export(char **argv) {
    if (!argv[1]) {
        for (char **e = environ; e && *e; e++)
            printf("export %s\n", *e);
        return;
    }
    for (int i = 1; argv[i]; i++) {
        char *eq = strchr(argv[i], '=');
        if (eq) {
            *eq = '\0';
            setenv(argv[i], eq + 1, 1);
            *eq = '=';
        } else {
            setenv(argv[i], getenv(argv[i]) ? getenv(argv[i]) : "", 1);
        }
    }
}

static int run_builtin(char **argv) {
    if (!argv || !argv[0])
        return -1;

    if (strcmp(argv[0], "exit") == 0) {
        want_exit = argv[1] ? atoi(argv[1]) : 0;
        return want_exit;
    }
    if (strcmp(argv[0], "cd") == 0) {
        cmd_cd(argv);
        return 0;
    }
    if (strcmp(argv[0], "pwd") == 0) {
        char cwd[512];
        if (getcwd(cwd, sizeof(cwd)) != NULL)
            printf("%s\n", cwd);
        return 0;
    }
    if (strcmp(argv[0], "echo") == 0) {
        cmd_echo(argv);
        return 0;
    }
    if (strcmp(argv[0], "export") == 0) {
        cmd_export(argv);
        return 0;
    }
    if (strcmp(argv[0], "env") == 0 || strcmp(argv[0], "set") == 0) {
        for (char **e = environ; e && *e; e++)
            printf("%s\n", *e);
        return 0;
    }
    if (strcmp(argv[0], "unset") == 0) {
        for (int i = 1; argv[i]; i++)
            unsetenv(argv[i]);
        return 0;
    }
    if (strcmp(argv[0], "clear") == 0) {
        write(1, "\x1b[2J\x1b[H", 6);
        return 0;
    }
    return -1;
}

/* ---------- execution ---------- */

static void spawn_child(struct cmd *c, int pgid, int (*pipes)[2], int npipe,
                        int idx, int ncmd, int jc) {
    if (jc) {
        int grp = (pgid == 0) ? (int)getpid() : pgid;
        setpgid(0, grp);
        child_reset_signals();
    } else {
        set_handler(SIGPIPE, SIG_DFL);
    }

    if (pipes) {
        if (idx > 0)
            dup2(pipes[idx - 1][0], 0);
        if (idx < ncmd - 1)
            dup2(pipes[idx][1], 1);
    }

    for (int i = 0; i < c->nredir; i++) {
        struct redir *r = &c->redirs[i];
        int fd = -1;
        if (r->target)
            fd = open(r->target, r->flags, 0644);
        if (fd < 0) {
            fprintf(stderr, "sh: %s: %s\n", r->target ? r->target : "?",
                    strerror(errno));
            _exit(1);
        }
        if (fd != r->fd) {
            dup2(fd, r->fd);
            close(fd);
        }
    }

    if (pipes) {
        for (int i = 0; i < npipe; i++) {
            if (i != idx - 1)
                close(pipes[i][0]);
            if (i != idx)
                close(pipes[i][1]);
        }
    }

    if (c->nargv == 0)
        _exit(0);

    execvp(c->argv[0], c->argv);
    if (errno == ENOENT || errno == ENOTDIR)
        fprintf(stderr, "sh: %s: command not found\n", c->argv[0]);
    else
        fprintf(stderr, "sh: %s: %s\n", c->argv[0], strerror(errno));
    _exit(127);
}

static int run_pipeline(struct pipeline *p, int jc) {
    int ncmd = p->ncmd;
    int npipe = ncmd - 1;
    int pipes[31][2];

    if (npipe > 0) {
        for (int i = 0; i < npipe; i++)
            if (pipe(pipes[i]) != 0) {
                fprintf(stderr, "sh: pipe: %s\n", strerror(errno));
                return 1;
            }
    }

    int pgid = 0;
    int lastpid = 0;
    struct job *jb = NULL;
    struct job local;
    memset(&local, 0, sizeof(local));

    if (jc || p->bg) {
        jb = jc ? &jobs[0] : NULL;
        if (jc) {
            for (int i = 0; i < MAX_JOBS; i++)
                if (!jobs[i].used) {
                    jb = &jobs[i];
                    break;
                }
            if (!jb) {
                if (npipe > 0)
                    for (int i = 0; i < npipe; i++) {
                        close(pipes[i][0]);
                        close(pipes[i][1]);
                    }
                return 1;
            }
            memset(jb, 0, sizeof(*jb));
            jb->used = 1;
        } else {
            jb = &local; /* non-interactive bg: track but never poll */
        }
        const char *cl = ncmd == 1 && p->cmds[0].nargv > 0
                             ? p->cmds[0].argv[0]
                             : "?";
        snprintf(jb->cmdline, sizeof(jb->cmdline), "%s", cl);
    }

    for (int i = 0; i < ncmd; i++) {
        pid_t pid = fork();
        if (pid == 0) {
            spawn_child(&p->cmds[i], pgid, npipe > 0 ? pipes : NULL, npipe, i, ncmd, jc);
            _exit(0);
        }
        if (pid < 0) {
            fprintf(stderr, "sh: fork: %s\n", strerror(errno));
            break;
        }
        lastpid = (int)pid;
        if (pgid == 0)
            pgid = (int)pid;
        if (jb) {
            setpgid(pid, pgid);
            job_record(jb, pid);
        }
    }

    if (npipe > 0)
        for (int i = 0; i < npipe; i++) {
            close(pipes[i][0]);
            close(pipes[i][1]);
        }

    if (p->bg) {
        if (jb && jb->used)
            printf("[%d] %d\n", job_number(jb), pgid);
        return 0;
    }

    /* foreground */
    if (jc) {
        tcsetpgrp(0, pgid);
        tm_cooked();
    }

    int st = 0;
    int ret = 1;
    if (jc) {
        for (;;) {
            int ws = 0;
            int r = k_waitpid(-pgid, &ws, WUNTRACED);
            if (r < 0 && errno == EINTR)
                continue;
            if (r <= 0)
                break;
            if (r == lastpid && WIFEXITED(ws))
                ret = WEXITSTATUS(ws);
            if (r == lastpid && WIFSIGNALED(ws))
                ret = 128 + WTERMSIG(ws);
            job_apply(jb, r, ws);
            if (jb->running <= 0)
                break;
        }
        tcsetpgrp(0, shell_pgid);
        tm_cooked();
        if (jb->stopped > 0) {
            job_print(jb);
            ret = 128;
        } else {
            job_remove(jb);
        }
    } else {
        int r = k_waitpid(lastpid > 0 ? lastpid : -1, &st, 0);
        if (r > 0) {
            if (WIFEXITED(st))
                ret = WEXITSTATUS(st);
            else if (WIFSIGNALED(st))
                ret = 128 + WTERMSIG(st);
            else
                ret = 1;
        }
    }

    return ret;
}

/* ---------- top-level line runner ---------- */

enum { SEP_NONE, SEP_AND, SEP_OR };

static void run_line(const char *line) {
    struct tok toks[MAX_TOK];
    int nt = tokenize(line, toks, MAX_TOK);
    if (nt == 0)
        return;

    int i = 0;
    int prev_status = 0;
    int prev_sep = SEP_NONE;
    int first = 1;

    while (i < nt) {
        /* skip stray separators so an empty segment can't loop forever */
        while (i < nt && (toks[i].type == T_SEMI || toks[i].type == T_BG ||
                          toks[i].type == T_AND || toks[i].type == T_OR))
            i++;
        if (i >= nt)
            break;

        struct pipeline P;
        int ok = parse_pipeline(toks, nt, &i, &P);
        if (ok < 0) {
            fprintf(stderr, "sh: syntax error\n");
            last_status = 2;
            return;
        }
        if (ok == 0)
            continue;

        int bg = 0;
        int sep = SEP_NONE;
        if (i < nt && toks[i].type == T_BG) {
            bg = 1;
            i++;
        }
        if (!bg && i < nt) {
            if (toks[i].type == T_SEMI) {
                sep = SEP_NONE;
                i++;
            } else if (toks[i].type == T_AND) {
                sep = SEP_AND;
                i++;
            } else if (toks[i].type == T_OR) {
                sep = SEP_OR;
                i++;
            } else {
                fprintf(stderr, "sh: syntax error\n");
                last_status = 2;
                pipe_free(&P);
                return;
            }
            if (i >= nt && sep != SEP_NONE) {
                fprintf(stderr, "sh: syntax error\n");
                last_status = 2;
                pipe_free(&P);
                return;
            }
        }
        P.bg = bg;

        int should_run = 1;
        if (!first) {
            if (prev_sep == SEP_AND && prev_status != 0)
                should_run = 0;
            if (prev_sep == SEP_OR && prev_status == 0)
                should_run = 0;
        }

        if (should_run) {
            /* builtins run inline: only when single, un-redirected, fg */
            if (!P.bg && P.ncmd == 1 && P.cmds[0].nredir == 0 &&
                P.cmds[0].nargv > 0) {
                int st = run_builtin(P.cmds[0].argv);
                if (st >= 0) {
                    prev_status = st;
                    last_status = st;
                    pipe_free(&P);
                    if (want_exit >= 0)
                        break;
                    prev_sep = sep;
                    first = 0;
                    continue;
                }
            }
            prev_status = run_pipeline(&P, interactive);
            last_status = prev_status;
        } else {
            prev_status = last_status;
        }

        pipe_free(&P);
        prev_sep = bg ? SEP_NONE : sep;
        first = 0;

        if (want_exit >= 0)
            break;
    }
}

/* ---------- main ---------- */

static void print_prompt(void) {
    char cwd[512];
    char pr[MAXLINE + 8];
    int len;
    if (getcwd(cwd, sizeof(cwd)) != NULL)
        len = snprintf(pr, sizeof(pr), "%s$ ", cwd);
    else
        len = snprintf(pr, sizeof(pr), "$ ");
    write(1, pr, (size_t)(len > 0 ? len : 0));
}

int main(int argc, char **argv) {
    init_environment();

    int run_c = argc > 1 && strcmp(argv[1], "-c") == 0;
    char *script = NULL;
    if (!run_c && argc > 1)
        script = argv[1];

    if (run_c && argc > 2) {
        interactive = 0;
        run_line(argv[2]);
        return last_status;
    }

    if (script) {
        int fd = open(script, O_RDONLY);
        if (fd < 0) {
            fprintf(stderr, "sh: %s: %s\n", script, strerror(errno));
            return 126;
        }
        dup2(fd, 0);
        if (fd != 0)
            close(fd);
        interactive = is_tty_fd(0);
    } else {
        interactive = is_tty_fd(0);
    }

    if (interactive) {
        shell_ignore_signals();
        tm_cooked();
        shell_pgid = (int)getpgrp();
        printf("TinyKern sh\n");
    } else {
        set_handler(SIGPIPE, SIG_IGN);
    }

    char line[MAXLINE];
    for (;;) {
        if (interactive) {
            check_background();
            print_prompt();
        }
        int n = read_line(line, sizeof(line));
        if (n < 0)
            break; /* -1 error, -2 EOF */
        if (n == 0)
            continue; /* empty line (Enter) or blank script line */
        run_line(line);
        if (want_exit >= 0)
            break;
    }

    return want_exit >= 0 ? want_exit : last_status;
}