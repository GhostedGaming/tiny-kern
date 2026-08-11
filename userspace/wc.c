#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>

static void wc_fd(int fd, long *lines, long *words, long *bytes) {
    char buf[4096];
    int inword = 0;
    *lines = *words = *bytes = 0;
    for (;;) {
        long r = read(fd, buf, sizeof(buf));
        if (r <= 0) {
            break;
        }
        for (long i = 0; i < r; i++) {
            (*bytes)++;
            char c = buf[i];
            if (c == '\n') {
                (*lines)++;
            }
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
                inword = 0;
            } else if (!inword) {
                inword = 1;
                (*words)++;
            }
        }
    }
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        long l, w, b;
        wc_fd(0, &l, &w, &b);
        printf("%8ld %7ld %8ld\n", l, w, b);
        return 0;
    }
    long tl = 0, tw = 0, tb = 0;
    for (int i = 1; i < argc; i++) {
        int fd = open(argv[i], O_RDONLY);
        if (fd < 0) {
            printf("wc: %s: %s\n", argv[i], strerror(errno));
            continue;
        }
        long l, w, b;
        wc_fd(fd, &l, &w, &b);
        printf("%8ld %7ld %8ld %s\n", l, w, b, argv[i]);
        tl += l;
        tw += w;
        tb += b;
        close(fd);
    }
    if (argc > 2) {
        printf("%8ld %7ld %8ld total\n", tl, tw, tb);
    }
    return 0;
}
