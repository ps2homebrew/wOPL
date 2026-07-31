#ifndef __MMCE_SUPPORT_H
#define __MMCE_SUPPORT_H

#include "include/iosupport.h"
#include "include/mcemu.h"

#define MMCE_MODE_UPDATE_DELAY MENU_UPD_DELAY_GENREFRESH

typedef struct
{
    int active;       /* Activation flag */
    int fd;           /* VMC fd */
    int flags;        /* Card flag */
    vmc_spec_t specs; /* Card specifications */
} mmce_vmc_infos_t;

extern int gMMCEIGRSlot;
extern int gMMCESlot;
extern int gMMCEAckWaitCycles;
extern int gMMCEUseAlarms;
extern int gMMCEEnableGameID;
extern int gMMCEStartMode;
extern char gMMCEPrefix[32];

void mmceInit(item_list_t *itemList);
item_list_t *mmceGetObject(int initOnly);
void mmceSendGameId(const char *gameId);
void mmceSendGameIdToDevice(int device, const char *gameId);
void mmceLoadModules(void);
void mmceLaunchGame(item_list_t *itemList, int id, per_game_cfg_t *pgcfg);

#endif
