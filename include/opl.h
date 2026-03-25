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

int loadConfig(int types);
int saveConfig(int types, int showUI);
void applyConfig(int themeID, int langID, int skipDeviceRefresh);
void handleLwnbdSrv();


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


void _loadConfig();

void setDefaultColors(void);

item_list_t *getFavouritesOwnerPointer(short int mode);
void loadFavourites(void);

#define MENU_ITEM_HEIGHT 19

int checkLoadConfigBDM(int types);

int checkLoadConfigHDD(int types);

/*
BLURT output char blurttext[128];
#define BLURT                                                                           \
    snprintf(blurttext, sizeof(blurttext), "%s\\%s(%d)", __FILE__, __func__, __LINE__); \
    delay(10);
#define BLURT snprintf(blurttext, sizeof(blurttext), "%s(%d)", blurttext, __LINE__);
*/
#endif
