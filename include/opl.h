#ifndef __OPL_H
#define __OPL_H


#include "include/iosupport.h"

// Master password for disabling the parental lock.
#define OPL_PARENTAL_LOCK_MASTER_PASS "989765"


// Codes have been planned to fit the design of the GUI functions within gui.c.
#define OPL_COMPAT_UPDATE_STAT_WIP        0
#define OPL_COMPAT_UPDATE_STAT_DONE       1
#define OPL_COMPAT_UPDATE_STAT_ERROR      -1
#define OPL_COMPAT_UPDATE_STAT_CONN_ERROR -2
#define OPL_COMPAT_UPDATE_STAT_ABORTED    -3

#define OPL_VMODE_CHANGE_CONFIRMATION_TIMEOUT_MS 10000

int oplPath2Mode(const char *path);
int oplGetAppImage(const char *device, char *folder, int isRelative, char *value, char *suffix, GSTEXTURE *resultTex, short psm);
int oplScanApps(int (*callback)(const char *path, config_set_t *appConfig, void *arg), void *arg);
int oplShouldAppsUpdate(void);
config_set_t *oplGetLegacyAppsConfig(void);
config_set_t *oplGetLegacyAppsInfo(char *name);

void setErrorMessage(int strId);
void setErrorMessageWithCode(int strId, int error);
int loadConfig(int types);
int saveConfig(int types, int showUI);
void applyConfig(int themeID, int langID, int skipDeviceRefresh);
void menuDeferredUpdate(void *data);
void moduleUpdateMenu(int mode, int themeChanged, int langChanged);
void handleLwnbdSrv();
void deinit(int exception, int modeSelected);

// Shutdown minimal services initiated for auto loading.
void miniDeinit(config_set_t *configSet);


enum ETH_OP_MODES {
    ETH_OP_MODE_AUTO = 0,
    ETH_OP_MODE_100M_FDX,
    ETH_OP_MODE_100M_HDX,
    ETH_OP_MODE_10M_FDX,
    ETH_OP_MODE_10M_HDX,

    ETH_OP_MODE_COUNT
};
extern int gEnableUSB;

extern int gAutosort;
extern int gAutoRefresh;
extern int gEnableArt;
extern int gEnableArchivedArt;

// ------------------------------------------------------------------------------------------------------------------------

extern int gPS2Logo;

// Default device
extern int gDefaultDevice;

extern int gEnableWrite;

extern int gRememberLastPlayed;


void initSupport(item_list_t *itemList, int mode, int force_reinit);

void setDefaultColors(void);

item_list_t *getFavouritesOwnerPointer(short int mode);
void loadFavourites(void);

#define MENU_ITEM_HEIGHT 19

#include "include/menusys.h"

typedef struct
{
    item_list_t *support;

    /// menu item used with this list support
    menu_item_t menuItem;

    /// submenu list
    submenu_list_t *subMenu;
} opl_io_module_t;

/*
BLURT output char blurttext[128];
#define BLURT                                                                           \
    snprintf(blurttext, sizeof(blurttext), "%s\\%s(%d)", __FILE__, __func__, __LINE__); \
    delay(10);
#define BLURT snprintf(blurttext, sizeof(blurttext), "%s(%d)", blurttext, __LINE__);
*/
#endif
