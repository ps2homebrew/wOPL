#ifndef __BDM_SUPPORT_H
#define __BDM_SUPPORT_H

#include "include/supportbase.h"
#include "include/iosupport.h"

typedef struct
{
    int massDeviceIndex; // Underlying device index backing the mass fs partition, ex: usb0 = 0, usb1 = 1, etc.
    char bdmPrefix[40];  // Contains the full path to the folder where all the games are.
    int bdmULSizePrev;
    time_t bdmModifiedCDPrev;
    time_t bdmModifiedDVDPrev;
    int bdmGameCount;
    base_game_info_t *bdmGames;
    char bdmDriver[32];
    int bdmDeviceType;      // Type of BDM device, see BDM_TYPE_* above
    int bdmDeviceTick;      // Used alongside BdmGeneration to tell if device data needs to be refreshed
    int bdmHddIsLBA48;      // 1 if the HDD supports LBA48, 0 if the HDD only supports LBA28
    int ataHighestUDMAMode; // Highest UDMA mode supported by the HDD
    unsigned char ThemesLoaded;
    unsigned char LanguagesLoaded;
    unsigned char ForceRefresh;
} bdm_device_data_t;

#define MAX_BDM_DEVICES 5

#define BDM_TYPE_UNKNOWN -1
#define BDM_TYPE_USB     0
#define BDM_TYPE_ILINK   1
#define BDM_TYPE_SDC     2
#define BDM_TYPE_ATA     3

extern int gBDMStartMode;
extern int bdmCacheSize;
extern char gBDMPrefix[32];

extern int gEnableILK;
extern int gEnableMX4SIO;
extern int gEnableBdmHDD;

extern base_game_info_t *gAutoLaunchBDMGame;
extern bdm_device_data_t *gAutoLaunchDeviceData;


int bdmFindPartition(char *target, const char *name, int write);
void bdmLoadModules(void);
void bdmLaunchGame(item_list_t *itemList, int id, config_set_t *configSet);

void bdmInitSemaphore();
void bdmEnumerateDevices();

void bdmResolveLBA_UDMA(bdm_device_data_t *pDeviceData);
int bdmWaitForDevice(int deviceId, u32 timeoutMs);
int bdmDeviceIsPresent(int deviceId);
int bdmHDDIsPresent();

void autoLaunchBDMGame(char *argv[]);

#endif
