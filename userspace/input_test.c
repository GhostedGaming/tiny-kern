#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

int main(void) {
    printf("input_test: start\n");
    printf("input_test: type a line of text then press enter\n");

    char buf[256];
    ssize_t n = read(0, buf, sizeof(buf) - 1);
    if (n <= 0) {
        printf("input_test: FAIL no input (errno=%d)\n", errno);
        return 1;
    }

    buf[n] = 0;
    printf("input_test: received: %s", buf);
    printf("input_test: input OK\n");
    return 0;
}
