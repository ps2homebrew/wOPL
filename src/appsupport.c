#include "include/common.h"
#include "include/lang.h"
#include "include/gui.h"
#include "include/appsupport.h"
#include "include/themes.h"
#include "include/system.h"
#include "include/ioman.h"
#include "include/util.h"
#include "include/module.h"
#include "include/bdmsupport.h"
#include "include/ethsupport.h"
#include "include/hddsupport.h"
#include "include/initializer.h"

#include <fcntl.h>
#include <stdlib.h>
#include <elf-loader.h>
#include <stdio.h>
#include <dirent.h>
#include <unistd.h>

#define APP_MODE_UPDATE_DELAY 240

#define APP_TITLE_MAX 128
#define APP_PATH_MAX  128
#define APP_BOOT_MAX  64
#define APP_ARGV1_MAX 128

#define APP_CONFIG_TITLE "title"
#define APP_CONFIG_BOOT  "boot"
#define APP_CONFIG_ARGV1 "argv1"

typedef struct
{
    char title[APP_TITLE_MAX + 1];
    char path[APP_PATH_MAX + 1];
    char boot[APP_BOOT_MAX + 1];
    char argv1[APP_ARGV1_MAX + 1];
} app_info_t;

static int appForceUpdate = 1;
static int appItemCount = 0;

static app_info_t *appsList;

struct app_info_linked
{
    struct app_info_linked *next;
    app_info_t app;
};

// forward declaration
static item_list_t appItemList;

int gAPPStartMode;

// App support stuff.
unsigned char shouldAppsUpdate;

static void appFreeList(void);

static int oplScanApps(int (*callback)(const char *path, const char *cfgPath, void *arg), void *arg);

static int oplGetAppImage(const char *device, char *folder, int isRelative, char *value, char *suffix, GSTEXTURE *resultTex, short psm);
static int oplShouldAppsUpdate(void);

static float appGetELFSize(char *path)
{
    int fd, size;
    float bytesInMiB = 1048576.0f;

    fd = open(path, O_RDONLY);
    if (fd < 0) {
        LOG("Failed to open APP %s\n", path);
        return 0.0f;
    }

    size = sbGetFileSize(fd);
    close(fd);

    // Return size in MiB
    return (size / bytesInMiB);
}

static char *appGetBoot(char *device, int max, char *path)
{
    char *pos, *filenamesep;

    // Looking for the boot device & filename from the path
    pos = strrchr(path, ':');
    if (pos != NULL) {
        int len = (int)(pos + 1 - path);
        if (len + 1 > max)
            len = max - 1;
        strncpy(device, path, len);
        device[len] = '\0';
    }

    filenamesep = strchr(path, '/');
    if (filenamesep != NULL)
        return filenamesep + 1;

    if (pos) {
        return pos + 1;
    }

    return path;
}

static void appInit(item_list_t *itemList)
{
    LOG("APPSUPPORT Init\n");
    appForceUpdate = 1;
    appItemList.delay = gAPPFramesDelay;
    appsList = NULL;
    appItemList.enabled = 1;
}

item_list_t *appGetObject(int initOnly)
{
    if (initOnly && !appItemList.enabled)
        return NULL;
    return &appItemList;
}

static int appNeedsUpdate(item_list_t *itemList)
{
    int update = 0;
    if (appForceUpdate) {
        appForceUpdate = 0;
        update = 1;
    }
    if (oplShouldAppsUpdate())
        update = 1;

    return update;
}

static void appCopyStr(char *dst, const char *src, size_t size)
{
    if (!dst || !size)
        return;

    if (!src)
        src = "";

    strncpy(dst, src, size - 1);
    dst[size - 1] = '\0';
}

static void appStripValue(char *value)
{
    char *comment;
    int len;

    if (!value)
        return;

    comment = strchr(value, '#');
    if (comment)
        *comment = '\0';

    len = strlen(value);
    while (len > 0 && (value[len - 1] == '\r' || value[len - 1] == '\n' || value[len - 1] == ' ' || value[len - 1] == '\t'))
        value[--len] = '\0';
}

typedef struct
{
    const char *key;
    char *out;
    size_t out_len;
    int found;
} app_key_value_t;

static int appReadKeyValueLine(char *line, app_key_value_t *value)
{
    size_t key_len;
    char *text;

    key_len = strlen(value->key);
    if (strncmp(line, value->key, key_len) != 0 || line[key_len] != '=')
        return 0;

    text = line + key_len + 1;
    appStripValue(text);
    appCopyStr(value->out, text, value->out_len);
    value->found = 1;

    return 1;
}

static int appReadKeyValueFile(const char *path, app_key_value_t *values, int count)
{
    int fd, size, i, found;
    char buffer[4096];
    char *line, *next;

    if (!path || !values || count <= 0)
        return 0;

    fd = open(path, O_RDONLY);
    if (fd < 0)
        return 0;

    size = read(fd, buffer, sizeof(buffer) - 1);
    close(fd);

    if (size <= 0)
        return 0;

    buffer[size] = '\0';
    found = 0;
    line = buffer;

    // Skip a leading UTF-8 BOM (EF BB BF) if a text editor left one..
    if (size >= 3 && (unsigned char)line[0] == 0xEF && (unsigned char)line[1] == 0xBB && (unsigned char)line[2] == 0xBF)
        line += 3;

    while (line && *line) {
        next = strchr(line, '\n');
        if (next)
            *next++ = '\0';

        for (i = 0; i < count; i++) {
            if (!values[i].found && appReadKeyValueLine(line, &values[i])) {
                found++;
                break;
            }
        }

        if (found == count)
            break;

        line = next;
    }

    return found;
}

static int appReadTitleCfg(const char *cfgPath, const char *path, app_info_t *info)
{
    app_key_value_t values[3];

    if (!cfgPath || !path || !info)
        return 0;

    memset(info, 0, sizeof(*info));

    values[0].key = APP_CONFIG_TITLE;
    values[0].out = info->title;
    values[0].out_len = sizeof(info->title);
    values[0].found = 0;

    values[1].key = APP_CONFIG_BOOT;
    values[1].out = info->boot;
    values[1].out_len = sizeof(info->boot);
    values[1].found = 0;

    values[2].key = APP_CONFIG_ARGV1;
    values[2].out = info->argv1;
    values[2].out_len = sizeof(info->argv1);
    values[2].found = 0;

    appReadKeyValueFile(cfgPath, values, 3);

    if (!values[0].found || !values[1].found) {
        LOG("APPSUPPORT item has no boot/title.\n");
        return 0;
    }

    appCopyStr(info->path, path, sizeof(info->path));

    return 1;
}

static int appScanCallback(const char *path, const char *cfgPath, void *arg)
{
    struct app_info_linked **appsLinkedList = (struct app_info_linked **)arg;
    struct app_info_linked *app;
    app_info_t info;

    if (!appReadTitleCfg(cfgPath, path, &info))
        return 1;

    if (*appsLinkedList == NULL) {
        *appsLinkedList = malloc(sizeof(struct app_info_linked));
        app = *appsLinkedList;
        if (app)
            app->next = NULL;
    } else {
        app = malloc(sizeof(struct app_info_linked));
        if (app != NULL) {
            app->next = *appsLinkedList;
            *appsLinkedList = app;
        }
    }

    if (app == NULL) {
        LOG("APPSUPPORT unable to allocate memory.\n");
        return -1;
    }

    memcpy(&app->app, &info, sizeof(app_info_t));

    return 0;
}

static int appUpdateItemList(item_list_t *itemList)
{
    struct app_info_linked *appsLinkedList, *appNext;

    guiSetBootStatusIfActive("Scanning apps...");

    appFreeList();
    appsLinkedList = NULL;

    appItemCount += oplScanApps(&appScanCallback, &appsLinkedList);

    if (appItemCount > 0) {
        appsList = malloc(appItemCount * sizeof(app_info_t));

        if (appsList != NULL) {
            int i;
            for (i = 0; appsLinkedList != NULL; i++) {
                memcpy(&appsList[appItemCount - i - 1], &appsLinkedList->app, sizeof(app_info_t));
                appNext = appsLinkedList->next;
                free(appsLinkedList);
                appsLinkedList = appNext;
            }
        } else {
            LOG("APPSUPPORT unable to allocate memory.\n");
            while (appsLinkedList != NULL) {
                appNext = appsLinkedList->next;
                free(appsLinkedList);
                appsLinkedList = appNext;
            }
            appItemCount = 0;
        }
    }

    LOG("APPSUPPORT %d apps loaded\n", appItemCount);
    return appItemCount;
}

static void appFreeList(void)
{
    if (appsList != NULL) {
        free(appsList);
        appsList = NULL;
        appItemCount = 0;
    }
}

static int appGetItemCount(item_list_t *itemList)
{
    return appItemCount;
}

static char *appGetItemName(item_list_t *itemList, int id)
{
    return appsList[id].title;
}

static int appGetItemNameLength(item_list_t *itemList, int id)
{
    return APP_TITLE_MAX;
}

static char *appGetItemStartup(item_list_t *itemList, int id)
{
    return appsList[id].boot;
}

static void appDeleteItem(item_list_t *itemList, int id)
{
    sysDeleteFolder(appsList[id].path);
    appForceUpdate = 1;
}

static int appAppendText(char *out, size_t out_len, size_t *pos, const char *text)
{
    size_t len;

    if (!out || !pos || !text)
        return 0;

    len = strlen(text);
    if (*pos + len >= out_len)
        return 0;

    memcpy(out + *pos, text, len);
    *pos += len;
    out[*pos] = '\0';

    return 1;
}

static int appUpdateTitleCfgTitle(const char *cfgPath, const char *newName)
{
    int fd, size, found;
    char inbuf[1024];
    char outbuf[1024];
    char tmpPath[300];
    char *line, *next;
    size_t pos;

    if (!cfgPath || !newName)
        return 0;

    fd = open(cfgPath, O_RDONLY);
    if (fd >= 0) {
        size = read(fd, inbuf, sizeof(inbuf) - 1);
        close(fd);
        if (size < 0)
            size = 0;
    } else
        size = 0;

    inbuf[size] = '\0';
    outbuf[0] = '\0';
    pos = 0;
    found = 0;
    line = inbuf;

    while (line && *line) {
        next = strchr(line, '\n');
        if (next)
            *next++ = '\0';

        if (strncmp(line, APP_CONFIG_TITLE "=", sizeof(APP_CONFIG_TITLE)) == 0) {
            if (!appAppendText(outbuf, sizeof(outbuf), &pos, APP_CONFIG_TITLE "=") || !appAppendText(outbuf, sizeof(outbuf), &pos, newName) || !appAppendText(outbuf, sizeof(outbuf), &pos, "\n"))
                return 0;
            found = 1;
        } else {
            if (!appAppendText(outbuf, sizeof(outbuf), &pos, line) || !appAppendText(outbuf, sizeof(outbuf), &pos, "\n"))
                return 0;
        }

        line = next;
    }

    if (!found) {
        if (!appAppendText(outbuf, sizeof(outbuf), &pos, APP_CONFIG_TITLE "=") || !appAppendText(outbuf, sizeof(outbuf), &pos, newName) || !appAppendText(outbuf, sizeof(outbuf), &pos, "\n"))
            return 0;
    }

    snprintf(tmpPath, sizeof(tmpPath), "%s.tmp", cfgPath);
    fd = open(tmpPath, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (fd < 0)
        return 0;

    if (write(fd, outbuf, pos) != (int)pos) {
        close(fd);
        remove(tmpPath);
        return 0;
    }

    close(fd);

    if (rename(tmpPath, cfgPath) != 0) {
        remove(cfgPath);
        if (rename(tmpPath, cfgPath) != 0) {
            remove(tmpPath);
            return 0;
        }
    }

    return 1;
}

static void appRenameItem(item_list_t *itemList, int id, char *newName)
{
    char cfgPath[256];
    snprintf(cfgPath, sizeof(cfgPath), "%s/%s", appsList[id].path, APP_TITLE_CONFIG_FILE);

    appUpdateTitleCfgTitle(cfgPath, newName);

    appForceUpdate = 1;
}

static void appLaunchItem(item_list_t *itemList, int id, per_game_cfg_t *pgcfg)
{
    int fd;
    char filename[256];

    snprintf(filename, sizeof(filename), "%s/%s", appsList[id].path, appsList[id].boot);
    filename[sizeof(filename) - 1] = '\0';

    fd = open(filename, O_RDONLY);
    if (fd >= 0) {
        int mode, argc = 0;
        char partition[128];
        char *argv[1];
        close(fd);

        partition[0] = '\0';

        mode = sbGetPathMode(filename);
        if (mode < 0)
            mode = APP_MODE;

        if (mode == HDD_MODE)
            snprintf(partition, sizeof(partition), "%s:", gOPLPart);

        if (appsList[id].argv1[0]) {
            argv[0] = appsList[id].argv1;
            argc = 1;
        }

        deinit(UNMOUNT_EXCEPTION, mode);
        LoadELFFromFileWithPartition(filename, partition, argc, argv);
    } else
        guiMsgBox(_l(_STR_ERR_FILE_INVALID), 0, NULL);
}

static void appGetInfo(item_list_t *itemList, int id, game_info_t *gi)
{
    char cfgPath[256];
    app_key_value_t values[7];

    memset(gi, 0, sizeof(*gi));

    snprintf(cfgPath, sizeof(cfgPath), "%s/%s", appsList[id].path, APP_TITLE_CONFIG_FILE);

    values[0].key = "Title";
    values[0].out = gi->title;
    values[0].out_len = sizeof(gi->title);
    values[0].found = 0;

    values[1].key = "Description";
    values[1].out = gi->description;
    values[1].out_len = sizeof(gi->description);
    values[1].found = 0;

    values[2].key = "Developer";
    values[2].out = gi->developer;
    values[2].out_len = sizeof(gi->developer);
    values[2].found = 0;

    values[3].key = "Release";
    values[3].out = gi->release;
    values[3].out_len = sizeof(gi->release);
    values[3].found = 0;

    values[4].key = "Version";
    values[4].out = gi->version;
    values[4].out_len = sizeof(gi->version);
    values[4].found = 0;

    values[5].key = "Package";
    values[5].out = gi->package;
    values[5].out_len = sizeof(gi->package);
    values[5].found = 0;

    values[6].key = "Source";
    values[6].out = gi->source;
    values[6].out_len = sizeof(gi->source);
    values[6].found = 0;

    appReadKeyValueFile(cfgPath, values, 7);

    // fall back to menu title if no display Title set
    if (!gi->title[0]) {
        strncpy(gi->title, appsList[id].title, sizeof(gi->title) - 1);
        gi->title[sizeof(gi->title) - 1] = '\0';
    }
}

static void appGetPgCfg(item_list_t *itemList, int id, per_game_cfg_t *cfg)
{
    memset(cfg, 0, sizeof(*cfg));
    cfg->dma = 7;
    strcpy(cfg->format, "ELF");
    strcpy(cfg->media, "APP");
    char path[256];
    snprintf(path, sizeof(path), "%s/%s", appsList[id].path, appsList[id].boot);
    cfg->size_mb = (int)appGetELFSize(path);
}

static int appSavePgCfg(item_list_t *itemList, int id, const per_game_cfg_t *cfg)
{
    return 1;
}

static int appGetImage(item_list_t *itemList, char *folder, int isRelative, char *value, char *suffix, GSTEXTURE *resultTex, short psm)
{
    char device[8], *startup;

    startup = appGetBoot(device, sizeof(device), value);

    if (!strcmp(folder, "ART"))
        return oplGetAppImage(device, folder, isRelative, startup, suffix, resultTex, psm);
    else
        return oplGetAppImage(device, folder, isRelative, value, suffix, resultTex, psm);
}

static int appGetArchivedImage(item_list_t *itemList, char *folder, char *value, char *suffix, GSTEXTURE *resultTex, short psm)
{
    char device[8], *startup;

    startup = appGetBoot(device, sizeof(device), value);

    if (!strcmp(folder, "ART"))
        return oplGetAppImage(device, folder, 0, startup, suffix, resultTex, psm);
    else
        return oplGetAppImage(device, folder, 0, value, suffix, resultTex, psm);
}

static int appGetTextId(item_list_t *itemList)
{
    return _STR_APPS;
}

static int appGetIconId(item_list_t *itemList)
{
    return APP_ICON;
}

// This may be called, even if appInit() was not.
static void appCleanUp(item_list_t *itemList, int exception)
{
    if (appItemList.enabled) {
        LOG("APPSUPPORT CleanUp\n");

        appFreeList();
    }
}

// This may be called, even if appInit() was not.
static void appShutdown(item_list_t *itemList)
{
    if (appItemList.enabled) {
        LOG("APPSUPPORT Shutdown\n");

        appFreeList();
    }
}

static item_list_t appItemList = {
    APP_MODE, -1, 0, MODE_FLAG_NO_COMPAT | MODE_FLAG_NO_UPDATE, MENU_MIN_INACTIVE_FRAMES, APP_MODE_UPDATE_DELAY, NULL, NULL, &appGetTextId, NULL, &appInit, &appNeedsUpdate, &appUpdateItemList,
    &appGetItemCount, NULL, &appGetItemName, &appGetItemNameLength, &appGetItemStartup, &appDeleteItem, &appRenameItem, &appLaunchItem,
    &appGetInfo, &appGetPgCfg, &appSavePgCfg, &appGetImage, &appGetArchivedImage, &appCleanUp, &appShutdown, NULL, &appGetIconId};

static int scanApps(int (*callback)(const char *path, const char *cfgPath, void *arg), void *arg, char *appsPath, int exception)
{
    struct dirent *pdirent;
    DIR *pdir;
    int count, ret;
    char dir[128];
    char path[128];

    count = 0;
    if ((pdir = opendir(appsPath)) != NULL) {
        while ((pdirent = readdir(pdir)) != NULL) {
            if (exception && strchr(pdirent->d_name, '_') == NULL)
                continue;

            if (strcmp(pdirent->d_name, ".") == 0 || strcmp(pdirent->d_name, "..") == 0)
                continue;

            snprintf(dir, sizeof(dir), "%s/%s", appsPath, pdirent->d_name);
            if (pdirent->d_type != DT_DIR)
                continue;

            snprintf(path, sizeof(path), "%s/%s", dir, APP_TITLE_CONFIG_FILE);
            ret = callback(dir, path, arg);

            if (ret == 0)
                count++;
            else if (ret < 0)
                break;
        }

        closedir(pdir);
    } else
        LOG("APPS failed to open dir %s\n", appsPath);

    return count;
}

static int oplScanApps(int (*callback)(const char *path, const char *cfgPath, void *arg), void *arg)
{
    int i, count;
    item_list_t *listSupport;
    char appsPath[64];

    count = 0;
    for (i = 0; i < MODE_COUNT; i++) {
        listSupport = list_support[i].support;
        if ((listSupport != NULL) && (listSupport->enabled) && (listSupport->itemGetPrefix != NULL)) {
            char *prefix = listSupport->itemGetPrefix(listSupport);
            snprintf(appsPath, sizeof(appsPath), "%sAPPS", prefix);
            count += scanApps(callback, arg, appsPath, 0);
        }
    }

    for (i = 0; i < 2; i++) {
        snprintf(appsPath, sizeof(appsPath), "mc%d:", i);
        count += scanApps(callback, arg, appsPath, 1);
    }

    return count;
}

static int oplGetAppImage(const char *device, char *folder, int isRelative, char *value, char *suffix, GSTEXTURE *resultTex, short psm)
{
    int i, remaining, elfbootmode;
    char priority;
    item_list_t *listSupport;

    elfbootmode = -1;
    if (device != NULL) {
        elfbootmode = sbGetPathMode(device);
        if (elfbootmode >= 0) {
            listSupport = list_support[elfbootmode].support;

            if ((listSupport != NULL) && (listSupport->enabled)) {
                if (listSupport->itemGetImage(listSupport, folder, isRelative, value, suffix, resultTex, psm) >= 0)
                    return 0;
            }
        }
    }

    // We search on ever devices from fatest to slowest.
    for (remaining = MODE_COUNT, priority = 0; remaining > 0 && priority < 4; priority++) {
        for (i = 0; i < MODE_COUNT; i++) {
            listSupport = list_support[i].support;

            if (i == elfbootmode)
                continue;

            if ((listSupport != NULL) && (listSupport->enabled) && (listSupport->appsPriority == priority)) {
                if (listSupport->itemGetImage(listSupport, folder, isRelative, value, suffix, resultTex, psm) >= 0)
                    return 0;
                remaining--;
            }
        }
    }

    return -1;
}

static int oplShouldAppsUpdate(void)
{
    int result;

    result = (int)shouldAppsUpdate;
    shouldAppsUpdate = 0;

    return result;
}
