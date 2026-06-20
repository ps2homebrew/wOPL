#include "include/common.h"
#include "include/lang.h"
#include "include/gui.h"
#include "include/appsupport.h"
#include "include/themes.h"
#include "include/system.h"
#include "include/ioman.h"
#include "include/util.h"
#include "include/module.h"
#include "include/config_wopl.h"
#include "include/config_migration.h" // DELETE_WITH_MIGRATION

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
#include <libconfig.h>

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

static int oplScanApps(int (*callback)(const char *path, config_t *appConfig, void *arg), void *arg);

static int oplGetAppImage(const char *device, char *folder, int isRelative, char *value, char *suffix, GSTEXTURE *resultTex, short psm);
static int oplShouldAppsUpdate(void);
static int oplPath2Mode(const char *path);

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

static int appScanCallback(const char *path, config_t *appConfig, void *arg)
{
    struct app_info_linked **appsLinkedList = (struct app_info_linked **)arg;
    struct app_info_linked *app;
    const char *title = NULL, *boot = NULL, *argv1 = NULL;

    if (cfgGetStr(appConfig, "title", &title) == CONFIG_TRUE && cfgGetStr(appConfig, "boot", &boot) == CONFIG_TRUE) {
        if (*appsLinkedList == NULL) {
            *appsLinkedList = malloc(sizeof(struct app_info_linked));
            app = *appsLinkedList;
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

        strncpy(app->app.title, title, APP_TITLE_MAX);
        app->app.title[APP_TITLE_MAX] = '\0';
        strncpy(app->app.boot, boot, APP_BOOT_MAX);
        app->app.boot[APP_BOOT_MAX] = '\0';
        strncpy(app->app.path, path, APP_PATH_MAX);
        app->app.path[APP_PATH_MAX] = '\0';
        app->app.argv1[0] = '\0';
        if (cfgGetStr(appConfig, "argv1", &argv1) == CONFIG_TRUE) {
            strncpy(app->app.argv1, argv1, APP_ARGV1_MAX);
            app->app.argv1[APP_ARGV1_MAX] = '\0';
        }
        return 0;
    } else {
        LOG("APPSUPPORT item has no boot/title.\n");
        return 1;
    }

    return -1;
}

static int appUpdateItemList(item_list_t *itemList)
{
    struct app_info_linked *appsLinkedList, *appNext;

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

static void appRenameItem(item_list_t *itemList, int id, char *newName)
{
    char cfgPath[256];
    snprintf(cfgPath, sizeof(cfgPath), "%s/%s", appsList[id].path, APP_TITLE_CONFIG_FILE);

    config_t cfg;
    config_init(&cfg);
    config_read_file(&cfg, cfgPath); // ok if missing.. we just add the key

    config_setting_t *root = config_root_setting(&cfg);
    config_setting_t *s = config_lookup(&cfg, "title");
    if (s)
        config_setting_set_string(s, newName);
    else {
        s = config_setting_add(root, "title", CONFIG_TYPE_STRING);
        if (s)
            config_setting_set_string(s, newName);
    }
    config_write_file(&cfg, cfgPath);
    config_destroy(&cfg);

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

        strcpy(partition, "");
        mode = oplPath2Mode(filename);
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
    memset(gi, 0, sizeof(*gi));

    char cfgPath[256];
    snprintf(cfgPath, sizeof(cfgPath), "%s/%s", appsList[id].path, APP_TITLE_CONFIG_FILE);

    config_t cfg;
    config_init(&cfg);
    if (config_read_file(&cfg, cfgPath)) {
        cfgValidateBegin(cfgPath);
        const char *str;
        if (cfgGetStr(&cfg, "Title", &str)) {
            strncpy(gi->title, str, sizeof(gi->title) - 1);
            gi->title[sizeof(gi->title) - 1] = '\0';
        }
        if (cfgGetStr(&cfg, "Description", &str)) {
            strncpy(gi->description, str, sizeof(gi->description) - 1);
            gi->description[sizeof(gi->description) - 1] = '\0';
        }
        if (cfgGetStr(&cfg, "Developer", &str)) {
            strncpy(gi->developer, str, sizeof(gi->developer) - 1);
            gi->developer[sizeof(gi->developer) - 1] = '\0';
        }
        if (cfgGetStr(&cfg, "Release", &str)) {
            strncpy(gi->release, str, sizeof(gi->release) - 1);
            gi->release[sizeof(gi->release) - 1] = '\0';
        }
        if (cfgGetStr(&cfg, "Version", &str)) {
            strncpy(gi->version, str, sizeof(gi->version) - 1);
            gi->version[sizeof(gi->version) - 1] = '\0';
        }
        if (cfgGetStr(&cfg, "Package", &str)) {
            strncpy(gi->package, str, sizeof(gi->package) - 1);
            gi->package[sizeof(gi->package) - 1] = '\0';
        }
        if (cfgGetStr(&cfg, "Source", &str)) {
            strncpy(gi->source, str, sizeof(gi->source) - 1);
            gi->source[sizeof(gi->source) - 1] = '\0';
        }
        cfgValidateEnd();
    } else
        log_config_error(cfgPath, &cfg);

    config_destroy(&cfg);

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

static int scanApps(int (*callback)(const char *path, config_t *appConfig, void *arg), void *arg, char *appsPath, int exception)
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

            config_t lcfg;
            config_init(&lcfg);

            if (!config_read_file(&lcfg, path)) {
                config_destroy(&lcfg);

                // DELETE_WITH_MIGRATION v
                if (!cfgMigrateLegacyAppTitleCfg(path))
                    continue; // not found or not parseable at all

                // re read the now migrated file
                config_init(&lcfg);
                if (!config_read_file(&lcfg, path)) {
                    config_destroy(&lcfg);
                    continue;
                }
            }
            // DELETE_WITH_MIGRATION ^

            cfgValidateBegin(path);
            ret = callback(dir, &lcfg, arg);
            cfgValidateEnd();
            config_destroy(&lcfg);

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

static int oplScanApps(int (*callback)(const char *path, config_t *appConfig, void *arg), void *arg)
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
        elfbootmode = oplPath2Mode(device);
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

// For resolving the mode, given an app's path
static int oplPath2Mode(const char *path)
{
    char appsPath[64];
    const char *blkdevnameend;
    int i, blkdevnamelen;
    item_list_t *listSupport;

    for (i = 0; i < MODE_COUNT; i++) {
        listSupport = list_support[i].support;
        if ((listSupport != NULL) && (listSupport->itemGetPrefix != NULL)) {
            char *prefix = listSupport->itemGetPrefix(listSupport);
            snprintf(appsPath, sizeof(appsPath), "%sAPPS", prefix);

            blkdevnameend = strchr(appsPath, ':');
            if (blkdevnameend != NULL) {
                blkdevnamelen = (int)(blkdevnameend - appsPath);

                if (strncmp(path, appsPath, blkdevnamelen) == 0)
                    return listSupport->mode;
            }
        }
    }

    return -1;
}
