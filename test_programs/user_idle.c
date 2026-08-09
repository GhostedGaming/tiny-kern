#include <stdio.h>
#include <unistd.h>

int main(void) {
    printf("User idle thread\n");
    for (;;) {
        pause();
    }
}
