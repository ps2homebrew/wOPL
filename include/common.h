#ifndef __COMMON_H
#define __COMMON_H


#include "include/iosupport.h"

#include <stdio.h>
#include <stdlib.h>
#include "include/module.h"
#include "include/supportbase.h"
#include "include/bdmsupport.h"
#include "include/ethsupport.h"
#include "include/hddsupport.h"
#include "include/appsupport.h"
#include "include/favsupport.h"
#include "include/mmcesupport.h"
#include "include/initializer.h"

// Master password for disabling the parental lock.
#define PARENTAL_LOCK_MASTER_PASS "989765"


enum ETH_OP_MODES {
    ETH_OP_MODE_AUTO = 0,
    ETH_OP_MODE_100M_FDX,
    ETH_OP_MODE_100M_HDX,
    ETH_OP_MODE_10M_FDX,
    ETH_OP_MODE_10M_HDX,

    ETH_OP_MODE_COUNT
};

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

extern char gExportName[32];


void setDefaultColors(void);

item_list_t *getFavouritesOwnerPointer(short int mode);
void loadFavourites(void);

#define MENU_ITEM_HEIGHT 19

/*
BLURT output char blurttext[128];
#define BLURT                                                                           \
    snprintf(blurttext, sizeof(blurttext), "%s\\%s(%d)", __FILE__, __func__, __LINE__); \
    delay(10);
#define BLURT snprintf(blurttext, sizeof(blurttext), "%s(%d)", blurttext, __LINE__);
*/

#ifdef __EESIO_DEBUG
#include "SIOCookie.h"
#define LOG_INIT() ee_sio_start(38400, 0, 0, 0, 0, 1)
#define LOG_ENABLE() \
    do {             \
    } while (0)
#else
#ifdef __DEBUG
#include "include/debug.h"
#define LOG_INIT() \
    do {           \
    } while (0)
#define LOG_ENABLE() ioPutRequest(IO_CUSTOM_SIMPLEACTION, &debugSetActive)
#else
#define LOG_INIT() \
    do {           \
    } while (0)
#define LOG_ENABLE() \
    do {             \
    } while (0)
#endif
#endif


#endif
