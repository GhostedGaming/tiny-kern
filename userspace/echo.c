#include <stdio.h>
#include <string.h>

int main(int argc, char *argv[]) {
    int nl = 1;
    int start = 1;
    if (argc > 1 && strcmp(argv[1], "-n") == 0) {
        nl = 0;
        start = 2;
    }
    for (int i = start; i < argc; i++) {
        if (i > start) {
            printf(" ");
        }
        printf("%s", argv[i]);
    }
    if (nl) {
        printf("\n");
    }
    return 0;
}
