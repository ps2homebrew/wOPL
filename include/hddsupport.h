#ifndef __HDD_SUPPORT_H
#define __HDD_SUPPORT_H

#include "include/iosupport.h"
#include <hdd-ioctl.h>

#define HDL_GAME_NAME_MAX 64

typedef struct
{
    char partition_name[APA_IDMAX + 1];
    char name[HDL_GAME_NAME_MAX + 1];
    char startup[8 + 1 + 3 + 1];
    u8 hdl_compat_flags;
    u8 ops2l_compat_flags;
    u8 dma_type;
    u8 dma_mode;
    u8 disctype;
    u32 layer_break;
    u32 start_sector;
    u32 total_size_in_kb;
} hdl_game_info_t;

typedef struct
{
    u32 count;
    hdl_game_info_t *games;
} hdl_games_list_t;

int hddCheck(void);
int hddReadSectors(u32 lba, u32 nsectors, void *buf);
int hddSetTransferMode(int type, int mode);
void hddSetIdleTimeout(int timeout);

item_list_t *hddGetObject(int initOnly);
void hddLoadModules(void);
void hddLoadSupportModules(void);
void hddLaunchGame(item_list_t *itemList, int id, config_set_t *configSet);

#endif
