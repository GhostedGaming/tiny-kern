#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>

static int head_fd(int fd, int n) {
    char buf[4096];
    int lines = 0;
    for (;;) {
        long r = read(fd, buf, sizeof(buf));
        if (r <= 0) {
            break;
        }
        for (long i = 0; i < r; i++) {
            if (write(1, &buf[i], 1) != 1) {
                return 1;
            }
            if (buf[i] == '\n') {
                lines++;
                if (lines >= n) {
                    return 0;
                }
            }
        }
    }
    return 0;
}

int main(int argc, char *argv[]) {
    int n = 10;
    int start = 1;
    if (argc > 2 && strcmp(argv[1], "-n") == 0) {
        n = atoi(argv[2]);
        start = 3;
    }
    if (start >= argc) {
        return head_fd(0, n);
    }
    for (int i = start; i < argc; i++) {
        int fd = open(argv[i], O_RDONLY);
        if (fd < 0) {
            printf("head: %s: %s\n", argv[i], strerror(errno));
            continue;
        }
        if (start + 1 < argc) {
            printf("==> %s <==\n", argv[i]);
        }
        head_fd(fd, n);
        close(fd);
    }
    return 0;
}
