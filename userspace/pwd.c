#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>

int main(void) {
    char buf[512];
    if (getcwd(buf, sizeof(buf)) != NULL) {
        printf("%s\n", buf);
        return 0;
    }
    printf("pwd: %s\n", strerror(errno));
    return 1;
}
