#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>

static int copy_fd(int in) {
    char buf[4096];
    for (;;) {
        long n = read(in, buf, sizeof(buf));
        if (n <= 0) {
            break;
        }
        long off = 0;
        while (off < n) {
            long w = write(1, buf + off, n - off);
            if (w <= 0) {
                return 1;
            }
            off += w;
        }
    }
    return 0;
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        return copy_fd(0);
    }
    for (int i = 1; i < argc; i++) {
        int fd = open(argv[i], O_RDONLY);
        if (fd < 0) {
            printf("cat: %s: %s\n", argv[i], strerror(errno));
            continue;
        }
        copy_fd(fd);
        close(fd);
    }
    return 0;
}
