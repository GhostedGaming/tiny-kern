#include <stdio.h>
#include <dirent.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>

static void sort_names(char **names, int count) {
    for (int i = 1; i < count; i++) {
        char *tmp = names[i];
        int j = i - 1;
        while (j >= 0 && strcmp(names[j], tmp) > 0) {
            names[j + 1] = names[j];
            j--;
        }
        names[j + 1] = tmp;
    }
}

static int list_dir(const char *path, int show_all) {
    DIR *d = opendir(path);
    if (!d) {
        printf("ls: %s: %s\n", path, strerror(errno));
        return 1;
    }

    int cap = 32;
    int count = 0;
    char **names = malloc(cap * sizeof(char *));
    if (!names) {
        closedir(d);
        return 1;
    }

    struct dirent *ent;
    while ((ent = readdir(d)) != NULL) {
        if (!show_all && ent->d_name[0] == '.') {
            continue;
        }
        if (count == cap) {
            cap *= 2;
            char **n = realloc(names, cap * sizeof(char *));
            if (!n) {
                break;
            }
            names = n;
        }
        names[count] = malloc(strlen(ent->d_name) + 1);
        if (names[count]) {
            strcpy(names[count], ent->d_name);
            count++;
        }
    }
    closedir(d);

    sort_names(names, count);
    for (int i = 0; i < count; i++) {
        printf("%s\n", names[i]);
        free(names[i]);
    }
    free(names);
    return 0;
}

int main(int argc, char *argv[]) {
    int show_all = 0;
    int i = 1;
    if (argc > 1 && strcmp(argv[1], "-a") == 0) {
        show_all = 1;
        i = 2;
    }
    if (i >= argc) {
        return list_dir(".", show_all);
    }
    for (; i < argc; i++) {
        if (argc - (show_all ? 2 : 1) > 1) {
            printf("%s:\n", argv[i]);
        }
        list_dir(argv[i], show_all);
    }
    return 0;
}
