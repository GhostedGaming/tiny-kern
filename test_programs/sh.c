#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <abi/syscalls.h>

typedef long scw;
extern long __do_syscall_ret(unsigned long);
extern scw __do_syscall2(long, scw, scw);
extern scw __do_syscall3(long, scw, scw, scw);

#define MAX_JOBS 16
#define WNOHANG 1

struct job {
    int used;
    int pid;
    int done;
};

static struct job jobs[MAX_JOBS];

static int kwait(int pid, int *st, int opts) {
    return (int)__do_syscall_ret(
        (unsigned long)__do_syscall3(SYS_WAITPID, pid, (scw)st, (scw)opts));
}

static int add_job(int pid) {
    for (int i = 0; i < MAX_JOBS; i++) {
        if (!jobs[i].used) {
            jobs[i].used = 1;
            jobs[i].pid = pid;
            jobs[i].done = 0;
            return i + 1;
        }
    }
    return -1;
}

static void reap_jobs(void) {
    for (int i = 0; i < MAX_JOBS; i++) {
        if (jobs[i].used && !jobs[i].done) {
            int st = 0;
            int r = kwait(jobs[i].pid, &st, WNOHANG);
            if (r == jobs[i].pid) {
                jobs[i].done = 1;
                printf("sh: [%d] job %d done status=%d\n", i + 1, r, st);
            }
        }
    }
}

static int read_line(char *buf, int max) {
    int n = 0;
    while (n < max - 1) {
        char c;
        long r = read(0, &c, 1);
        if (r <= 0) {
            return -1;
        }
        if (c == '\n') {
            break;
        }
        buf[n++] = c;
    }
    buf[n] = 0;
    return n;
}

static int parse_line(char *line, char **argv) {
    int n = 0;
    char *p = line;
    while (*p) {
        while (*p == ' ' || *p == '\t') {
            p++;
        }
        if (!*p) {
            break;
        }
        argv[n++] = p;
        while (*p && *p != ' ' && *p != '\t') {
            p++;
        }
        if (*p) {
            *p = 0;
            p++;
        }
    }
    argv[n] = 0;
    return n;
}

static void run_external(char **argv, int bg) {
    char path[256];
    snprintf(path, sizeof(path), "/ram/bins/%s", argv[0]);

    pid_t pid = fork();
    if (pid == 0) {
        char *envp[] = { NULL };
        execve(path, argv, envp);
        printf("sh: exec %s: %s\n", path, strerror(errno));
        _exit(127);
    }
    if (pid < 0) {
        printf("sh: fork: %s\n", strerror(errno));
        return;
    }
    if (bg) {
        int jn = add_job(pid);
        printf("sh: [%d] %d\n", jn, pid);
    } else {
        int st = 0;
        int r = kwait(pid, &st, 0);
        if (r > 0) {
            printf("sh: %s exited status=%d\n", argv[0], st);
        } else {
            printf("sh: wait %s: %s\n", argv[0], strerror(errno));
        }
    }
}

static void cmd_wait(char **argv) {
    if (argv[1]) {
        int which = atoi(argv[1]);
        if (argv[1][0] == '%') {
            which = atoi(argv[1] + 1);
        }
        if (which >= 1 && which <= MAX_JOBS && jobs[which - 1].used) {
            int st = 0;
            int r = kwait(jobs[which - 1].pid, &st, 0);
            if (r == jobs[which - 1].pid) {
                jobs[which - 1].done = 1;
                printf("sh: job %d (%d) done status=%d\n", which, r, st);
            }
            return;
        }
        printf("sh: no such job\n");
        return;
    }
    for (int i = 0; i < MAX_JOBS; i++) {
        if (jobs[i].used && !jobs[i].done) {
            int st = 0;
            int r = kwait(jobs[i].pid, &st, 0);
            if (r == jobs[i].pid) {
                jobs[i].done = 1;
                printf("sh: job %d (%d) done status=%d\n", i + 1, r, st);
            }
        }
    }
}

static void cmd_jobs(void) {
    for (int i = 0; i < MAX_JOBS; i++) {
        if (jobs[i].used) {
            printf("sh: [%d] %d %s\n", i + 1, jobs[i].pid,
                   jobs[i].done ? "done" : "running");
        }
    }
}

static void print_help(void) {
    printf("builtins: cd <dir> | exit | jobs | wait [pid|%%n] | help\n");
    printf("programs: ls, pwd, cat, echo, head, wc, edit, background, tcc\n");
    printf("append & to run a program in the background\n");
}

int main(void) {
    chdir("/ram");
    printf("sh: TinyKern shell pid=%d\n", getpid());

    char line[256];
    for (;;) {
        reap_jobs();
        write(1, "sh$ ", 4);
        int n = read_line(line, sizeof(line));
        if (n < 0) {
            write(1, "\n", 1);
            break;
        }

        int bg = 0;
        int len = strlen(line);
        while (len > 0 && (line[len - 1] == ' ' || line[len - 1] == '\t')) {
            line[--len] = 0;
        }
        if (len > 0 && line[len - 1] == '&') {
            bg = 1;
            line[--len] = 0;
            while (len > 0 && (line[len - 1] == ' ' || line[len - 1] == '\t')) {
                line[--len] = 0;
            }
        }

        char *argv[16];
        int argc = parse_line(line, argv);
        if (argc == 0) {
            continue;
        }

        if (strcmp(argv[0], "exit") == 0) {
            break;
        } else if (strcmp(argv[0], "help") == 0) {
            print_help();
        } else if (strcmp(argv[0], "cd") == 0) {
            const char *dir = argv[1] ? argv[1] : "/";
            if (chdir(dir) != 0) {
                printf("sh: cd %s: %s\n", dir, strerror(errno));
            }
        } else if (strcmp(argv[0], "jobs") == 0) {
            cmd_jobs();
        } else if (strcmp(argv[0], "wait") == 0) {
            cmd_wait(argv);
        } else {
            run_external(argv, bg);
        }
    }

    printf("sh: exiting\n");
    return 0;
}
