#pragma once

#include <stddef.h>

#ifndef _SSIZE_T_DEFINED
#define _SSIZE_T_DEFINED
typedef long ssize_t;
#endif

#define MAX_NAME_LEN 256

typedef struct devfs_dev {
    char     name[MAX_NAME_LEN];
    ssize_t (*read) (struct devfs_dev *dev, void *buf, size_t count);
    ssize_t (*write)(struct devfs_dev *dev, const void *buf, size_t count);
    void    *priv;
} devfs_dev_t;

void devfs_init();
int devfs_register(const char *name,
                   ssize_t (*read) (devfs_dev_t *, void *,       size_t),
                   ssize_t (*write)(devfs_dev_t *, const void *, size_t),
                   void *priv);
int devfs_unregister(const char *name);
devfs_dev_t *devfs_get(const char *name);