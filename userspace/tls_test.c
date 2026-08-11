#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <wchar.h>
#include <string.h>

int main(void) {
    int ok = 1;

    time_t t = 0;
    struct tm *tm = gmtime(&t);
    if (tm && tm->tm_year == 70 && tm->tm_mon == 0 && tm->tm_mday == 1) {
        printf("tls_test: gmtime OK\n");
    } else {
        ok = 0;
        printf("tls_test: gmtime FAIL tm=%p year=%d mon=%d mday=%d\n",
               (void *)tm, tm ? tm->tm_year : -1, tm ? tm->tm_mon : -1, tm ? tm->tm_mday : -1);
    }

    wchar_t wc = 0;
    int n = mbtowc(&wc, "A", 1);
    if (n == 1 && wc == L'A') {
        printf("tls_test: mbtowc OK\n");
    } else {
        ok = 0;
        printf("tls_test: mbtowc FAIL n=%d wc=%d\n", n, (int)wc);
    }

    char *s = strerror(2);
    if (s) {
        printf("tls_test: strerror(2) = '%s'\n", s);
    } else {
        ok = 0;
        printf("tls_test: strerror FAIL\n");
    }

    printf("tls_test: %s\n", ok ? "ALL OK" : "FAILURES");
    return ok ? 0 : 1;
}
