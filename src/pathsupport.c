#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "include/pathsupport.h"

static char launchPath[256];

static void copy_str(char *dst, const char *src, size_t size)
{
    if (!size)
        return;

    if (!src)
        src = "";

    strncpy(dst, src, size - 1);
    dst[size - 1] = '\0';
}

static int path_starts_with_device(const char *path, const char *device)
{
    const char *suffix;
    size_t len;

    if (!path || !device)
        return 0;

    len = strlen(device);

    if (strncmp(path, device, len))
        return 0;

    suffix = path + len;

    while (*suffix >= '0' && *suffix <= '9')
        suffix++;

    return *suffix == ':';
}

void pathSetLaunchPath(const char *path)
{
    copy_str(launchPath, path, sizeof(launchPath));
}

const char *pathGetLaunchPath(void)
{
    return launchPath[0] ? launchPath : NULL;
}

int pathIsDevicePath(const char *path)
{
    static const char *devices[] = {
        "mc",
        "usb",
        "mx4sio",
        "ilink",
        "ata",
        "hdd",
        "mmce",
        NULL};

    int i;

    if (!path || !path[0])
        return 0;

    for (i = 0; devices[i] != NULL; i++) {
        if (path_starts_with_device(path, devices[i]))
            return 1;
    }

    return 0;
}

void pathNormaliseDir(char *dir, size_t dir_len)
{
    size_t len;

    if (!dir_len)
        return;

    dir[dir_len - 1] = '\0';
    len = strlen(dir);

    if (len > 0 && dir[len - 1] != '/') {
        if (len + 1 < dir_len) {
            dir[len] = '/';
            dir[len + 1] = '\0';
        }
    }
}

static int path_get_dirname(const char *path, char *dir_out, size_t dir_len)
{
    const char *slash;

    if (!path || !path[0] || !dir_len)
        return 0;

    if (!pathIsDevicePath(path))
        return 0;

    slash = strrchr(path, '/');

    if (!slash) {
        copy_str(dir_out, path, dir_len);
        pathNormaliseDir(dir_out, dir_len);
        return 1;
    }

    if ((size_t)(slash - path + 1) >= dir_len)
        return 0;

    memcpy(dir_out, path, slash - path + 1);
    dir_out[slash - path + 1] = '\0';

    return 1;
}

int pathGetBootDir(char *dir_out, size_t dir_len)
{
    char pwd[128];

    if (!dir_out || !dir_len)
        return 0;

    if (path_get_dirname(launchPath, dir_out, dir_len))
        return 1;

    pwd[0] = '\0';

    if (getcwd(pwd, sizeof(pwd)) == NULL)
        return 0;

    if (!pathIsDevicePath(pwd))
        return 0;

    copy_str(dir_out, pwd, dir_len);
    pathNormaliseDir(dir_out, dir_len);

    return 1;
}

int pathResolveToTrue(char *out, size_t out_len, const char *path)
{
    if (!out || !out_len || !path)
        return 0;

    copy_str(out, path, out_len);

    return 1;
}

int pathJoin(char *out, size_t out_len, const char *dir, const char *name)
{
    int len;

    if (!out || !out_len || !dir || !dir[0] || !name)
        return 0;

    len = snprintf(out, out_len, "%s%s", dir, name);

    if (len < 0 || len >= out_len) {
        out[0] = '\0';
        return 0;
    }

    return 1;
}
