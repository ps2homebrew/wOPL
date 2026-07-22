#include "include/common.h"
#include "include/lang.h"
#include "include/gui.h"
#include "include/supportbase.h"
#include "include/bdmsupport.h"
#include "include/hddsupport.h"
#include "include/util.h"
#include "include/themes.h"
#include "include/textures.h"
#include "include/ioman.h"
#include "include/system.h"
#include "include/extern_irx.h"
#include "include/cheatman.h"
#include "include/sound.h"
#include "modules/iopcore/common/cdvd_config.h"
#include "include/module.h"
#include "include/initializer.h"
#include "include/config_wopl.h"
#include "include/pathsupport.h"
#include <fcntl.h>
#include <stdlib.h>
#include <ps2sdkapi.h>

#include <usbhdfsd-common.h>
#include <sifrpc.h>
#include <kernel.h>
#include "opl-hdd-ioctl.h"
#include <errno.h>
#include <stdio.h>

#define NEWLIB_PORT_AWARE
#include <fileXio_rpc.h> // fileXioIoctl, fileXioDevctl
#include <libcdvd-common.h>
#include <delaythread.h>

#define BDM_MODE_UPDATE_DELAY MENU_UPD_DELAY_GENREFRESH

#define MAX_BDM_TRUE_DEVICES MAX_BDM_DEVICES

#include "include/mcemu.h"

typedef struct
{
    int active;       /* Activation flag */
    u64 start_sector; /* Start sector of vmc file */
    int flags;        /* Card flag */
    vmc_spec_t specs; /* Card specifications */
} bdm_vmc_infos_t;

static int bdmModulesLoaded = 0;
static int usbModLoaded = 0;
static int iLinkModLoaded = 0;
static int mx4sioModLoaded = 0;
static int hddModLoaded = 0;
static s32 bdmLoadModuleLock;

static item_list_t bdmDeviceList[MAX_BDM_TRUE_DEVICES];
static int bdmDeviceListInitialized = 0;

int bdmDeviceModeStarted;
int gBDMStartMode;
int bdmCacheSize;
char gBDMPrefix[32];
int gEnableUSB;
int gEnableILK;
int gEnableMX4SIO;
int gEnableBdmHDD;
base_game_info_t *gAutoLaunchBDMGame;
bdm_device_data_t *gAutoLaunchDeviceData;

void bdmInitDevicesData();
int bdmUpdateDeviceData(item_list_t *itemList);

static const char *bdmGetDevicePrefix(int deviceType);
static int bdmBuildTruePath(char *path, size_t pathSize, int deviceType, int deviceIndex, int withSlash);
static int bdmOpenTrueDevice(int deviceType, int deviceIndex);

static unsigned int BdmGeneration = 0;

static void bdmEventHandler(void *packet, void *opt)
{
    BdmGeneration++;
}

static void bdmLoadUSBModules(void)
{
    if (!usbModLoaded) {
        guiSetBootStatusIfActive("Loading USB modules...");

        // Load USB Block Device drivers
        LOG("[USBD]:\n");
        sysLoadModuleBuffer(&usbd_irx, size_usbd_irx, 0, NULL);

        LOG("[USBMASS_BD]:\n");
        sysLoadModuleBuffer(&usbmass_bd_irx, size_usbmass_bd_irx, 0, NULL);

        usbModLoaded = 1;
    }
}

static void bdmLoadiLinkModules(void)
{
    if (!iLinkModLoaded) {
        guiSetBootStatusIfActive("Loading iLink modules...");

        // Load iLink Block Device drivers
        LOG("[ILINKMAN]:\n");
        sysLoadModuleBuffer(&iLinkman_irx, size_iLinkman_irx, 0, NULL);

        LOG("[IEEE1394_BD]:\n");
        sysLoadModuleBuffer(&IEEE1394_bd_irx, size_IEEE1394_bd_irx, 0, NULL);

        iLinkModLoaded = 1;
    }
}

static void bdmLoadMX4SIOModules(void)
{
    if (!mx4sioModLoaded) {
        guiSetBootStatusIfActive("Loading MX4SIO modules...");

        // Load MX4SIO Block Device drivers
        LOG("[MX4SIO_BD]:\n");
        sysLoadModuleBuffer(&mx4sio_bd_irx, size_mx4sio_bd_irx, 0, NULL);

        mx4sioModLoaded = 1;
    }
}

static void bdmLoadBdmHDDModules(void)
{
    if (!hddModLoaded) {
        guiSetBootStatusIfActive("Loading BDM HDD modules...");

        // Load dev9 and atad device drivers.
        LOG("bdmLoadBlockDeviceModules loading hdd drivers...\n");
        hddLoadModules();

        hddModLoaded = 1;
    }
}

static void bdmLoadBlockDeviceModules(void)
{
    WaitSema(bdmLoadModuleLock);

    if (gEnableUSB)
        bdmLoadUSBModules();

    if (gEnableILK)
        bdmLoadiLinkModules();

    if (gEnableMX4SIO)
        bdmLoadMX4SIOModules();

    if (gEnableBdmHDD)
        bdmLoadBdmHDDModules();

    SignalSema(bdmLoadModuleLock);
}

static int bdmGetDeviceTypeFromPath(const char *path)
{
    if (pathHasDevicePrefix(path, "usb"))
        return BDM_TYPE_USB;
    if (pathHasDevicePrefix(path, "ilink"))
        return BDM_TYPE_ILINK;
    if (pathHasDevicePrefix(path, "mx4sio"))
        return BDM_TYPE_SDC;
    if (pathHasDevicePrefix(path, "ata"))
        return BDM_TYPE_ATA;

    return BDM_TYPE_UNKNOWN;
}

static void bdmLoadBaseModules(void)
{
    if (!bdmModulesLoaded) {
        guiSetBootStatusIfActive("Loading block device modules...");

        // Load Block Device Manager (BDM)
        LOG("[BDM]:\n");
        sysLoadModuleBuffer(&bdm_irx, size_bdm_irx, 0, NULL);

        // Load BDM FATFS driver
        LOG("[BDMFS_FATFS]:\n");
        sysLoadModuleBuffer(&bdmfs_fatfs_irx, size_bdmfs_fatfs_irx, 0, NULL);

        LOG("[BDMEVENT]:\n");
        sysLoadModuleBuffer(&bdmevent_irx, size_bdmevent_irx, 0, NULL);

        SifAddCmdHandler(0, &bdmEventHandler, NULL);

        bdmModulesLoaded = 1;
    }
}

void bdmLoadModules(void)
{
    LOG("BDMSUPPORT LoadModules\n");

    bdmLoadBaseModules();

    // Load Optional Block Device drivers
    ioPutRequest(IO_CUSTOM_SIMPLEACTION, &bdmLoadBlockDeviceModules);

    LOG("BDMSUPPORT Modules loaded\n");
}

void bdmLoadModulesForPath(const char *path)
{
    int deviceType = bdmGetDeviceTypeFromPath(path);

    if (deviceType == BDM_TYPE_UNKNOWN)
        return;

    LOG("BDMSUPPORT LoadModulesForPath %s\n", path);

    bdmLoadBaseModules();

    WaitSema(bdmLoadModuleLock);

    switch (deviceType) {
        case BDM_TYPE_USB:
            bdmLoadUSBModules();
            break;
        case BDM_TYPE_ILINK:
            bdmLoadiLinkModules();
            break;
        case BDM_TYPE_SDC:
            bdmLoadMX4SIOModules();
            break;
        case BDM_TYPE_ATA:
            bdmLoadBdmHDDModules();
            break;
    }

    SignalSema(bdmLoadModuleLock);
}

void bdmLoadModulesForUsbMassCompat(void)
{
    LOG("BDMSUPPORT LoadModulesForUsbMassCompat\n");

    bdmLoadBaseModules();

    WaitSema(bdmLoadModuleLock);
    bdmLoadUSBModules();
    SignalSema(bdmLoadModuleLock);
}

void bdmLoadEnabledDeviceModules(void)
{
    LOG("BDMSUPPORT LoadEnabledDeviceModules\n");

    if (!gEnableUSB && !gEnableILK && !gEnableMX4SIO && !gEnableBdmHDD) {
        LOG("BDMSUPPORT no enabled BDM devices\n");
        return;
    }

    bdmLoadBaseModules();

    // Settings have now been loaded.. so load enabled BDM drivers synchronously before bdmEnumerateDevices() runs
    bdmLoadBlockDeviceModules();
}

static void bdmInit(item_list_t *itemList)
{
    LOG("BDMSUPPORT Init\n");

    bdm_device_data_t *pDeviceData = (bdm_device_data_t *)itemList->priv;
    if (!pDeviceData) {
        itemList->enabled = 0;
        return;
    }

    pDeviceData->bdmULSizePrev = -2;
    pDeviceData->bdmModifiedCDPrev = 0;
    pDeviceData->bdmModifiedDVDPrev = 0;
    pDeviceData->bdmGameCount = 0;
    pDeviceData->bdmGames = NULL;
    bdmLoadModules();
    itemList->delay = gBDMFramesDelay;
    itemList->enabled = 1;
}

static int bdmNeedsUpdate(item_list_t *itemList)
{
    char path[256];
    int result = 0;
    struct stat st;

    // If we made it here then BDM device mode has been started.
    bdmDeviceModeStarted = 1;

    // If bdm mode is disabled bail out as we don't want to update the visibility state of the device pages.
    if (gBDMStartMode == START_MODE_DISABLED)
        return 0;

    bdm_device_data_t *pDeviceData = (bdm_device_data_t *)itemList->priv;
    if (!pDeviceData)
        return 0;

    opl_io_module_t *pOwner = (opl_io_module_t *)itemList->owner;
    int visible = pOwner != NULL && pOwner->menuItem.visible == 1;

    ioPutRequest(IO_CUSTOM_SIMPLEACTION, &bdmLoadBlockDeviceModules);

    // Check for forced refresh from deleting or renaming a game.
    if (pDeviceData->ForceRefresh != 0) {
        pDeviceData->ForceRefresh = 0;
        return 1;
    }

    // If the device menu is visible double check the device type and if support for this device type is enabled. If the user switches device support
    // to off for a bdm device we want to hide the menu even though the drivers are still loaded and the device is being detected by bdm.
    if (visible) {
        int deviceEnabled = 0;
        switch (pDeviceData->bdmDeviceType) {
            case BDM_TYPE_USB:
                deviceEnabled = gEnableUSB;
                break;
            case BDM_TYPE_ILINK:
                deviceEnabled = gEnableILK;
                break;
            case BDM_TYPE_SDC:
                deviceEnabled = gEnableMX4SIO;
                break;
            case BDM_TYPE_ATA:
                deviceEnabled = gEnableBdmHDD;
                break;
            default:
                deviceEnabled = 0;
                break;
        }

        // If the device page is visible but the device support is not enabled, hide the device page.
        if (deviceEnabled == 0)
            pOwner->menuItem.visible = 0;
    }

    if (pDeviceData->bdmULSizePrev != -2 && pDeviceData->bdmDeviceTick == BdmGeneration)
        return 0;

    pDeviceData->bdmDeviceTick = BdmGeneration;

    // Check if the device has been connected or removed.
    if ((result = bdmUpdateDeviceData(itemList)) == 0)
        return 0;

    // If a device was added or removed play the appropriate UI sound.
    if (result == -1) {
        sfxPlay(SFX_BD_DISCONNECT);
        return result;
    } else if (result == 1)
        sfxPlay(SFX_BD_CONNECT);

    snprintf(path, sizeof(path), "%sCD", pDeviceData->bdmPrefix);
    if (stat(path, &st) != 0)
        st.st_mtime = 0;
    if (pDeviceData->bdmModifiedCDPrev != st.st_mtime) {
        pDeviceData->bdmModifiedCDPrev = st.st_mtime;
        result = 1;
    }

    snprintf(path, sizeof(path), "%sDVD", pDeviceData->bdmPrefix);
    if (stat(path, &st) != 0)
        st.st_mtime = 0;
    if (pDeviceData->bdmModifiedDVDPrev != st.st_mtime) {
        pDeviceData->bdmModifiedDVDPrev = st.st_mtime;
        result = 1;
    }

    if (!sbIsSameSize(pDeviceData->bdmPrefix, pDeviceData->bdmULSizePrev))
        result = 1;

    // update Themes
    if (!pDeviceData->ThemesLoaded) {
        guiSetBootStatusIfActive("Loading block device themes...");

        snprintf(path, sizeof(path), "%sTHM", pDeviceData->bdmPrefix);
        if (thmAddElements(path, "/", 1) > 0)
            pDeviceData->ThemesLoaded = 1;
    }

    // update Languages
    if (!pDeviceData->LanguagesLoaded) {
        guiSetBootStatusIfActive("Loading block device languages...");

        snprintf(path, sizeof(path), "%sLNG", pDeviceData->bdmPrefix);
        if (lngAddLanguages(path, "/", itemList->mode) > 0)
            pDeviceData->LanguagesLoaded = 1;
    }

    guiSetBootStatusIfActive("Checking block device folders...");
    sbCreateFolders(pDeviceData->bdmPrefix, 1);

    return result;
}

static int bdmUpdateGameList(item_list_t *itemList)
{
    bdm_device_data_t *pDeviceData = (bdm_device_data_t *)itemList->priv;

    guiSetBootStatusIfActive("Scanning block device games...");

    sbReadList(&pDeviceData->bdmGames, pDeviceData->bdmPrefix, &pDeviceData->bdmULSizePrev, &pDeviceData->bdmGameCount);
    return pDeviceData->bdmGameCount;
}

static int bdmGetGameCount(item_list_t *itemList)
{
    bdm_device_data_t *pDeviceData = (bdm_device_data_t *)itemList->priv;

    return pDeviceData->bdmGameCount;
}

static void *bdmGetGame(item_list_t *itemList, int id)
{
    bdm_device_data_t *pDeviceData = (bdm_device_data_t *)itemList->priv;

    return (void *)&pDeviceData->bdmGames[id];
}

static char *bdmGetGameName(item_list_t *itemList, int id)
{
    bdm_device_data_t *pDeviceData = (bdm_device_data_t *)itemList->priv;

    return pDeviceData->bdmGames[id].name;
}

static int bdmGetGameNameLength(item_list_t *itemList, int id)
{
    bdm_device_data_t *pDeviceData = (bdm_device_data_t *)itemList->priv;

    return ((pDeviceData->bdmGames[id].format != GAME_FORMAT_USBLD) ? ISO_GAME_NAME_MAX + 1 : UL_GAME_NAME_MAX + 1);
}

static char *bdmGetGameStartup(item_list_t *itemList, int id)
{
    bdm_device_data_t *pDeviceData = (bdm_device_data_t *)itemList->priv;

    return pDeviceData->bdmGames[id].startup;
}

static void bdmDeleteGame(item_list_t *itemList, int id)
{
    bdm_device_data_t *pDeviceData = (bdm_device_data_t *)itemList->priv;

    sbDelete(&pDeviceData->bdmGames, pDeviceData->bdmPrefix, "/", pDeviceData->bdmGameCount, id);
    pDeviceData->bdmULSizePrev = -2;
    pDeviceData->ForceRefresh = 1;
}

static void bdmRenameGame(item_list_t *itemList, int id, char *newName)
{
    bdm_device_data_t *pDeviceData = (bdm_device_data_t *)itemList->priv;

    sbRename(&pDeviceData->bdmGames, pDeviceData->bdmPrefix, "/", pDeviceData->bdmGameCount, id, newName);
    pDeviceData->bdmULSizePrev = -2;
    pDeviceData->ForceRefresh = 1;
}

void bdmLaunchGame(item_list_t *itemList, int id, per_game_cfg_t *pgcfg)
{
    int i, fd, iop_fd, index, compatmask = 0;
    int EnablePS2Logo = 0;
#ifdef CHEAT
    int result;
#endif
    u64 startingLBA;
    unsigned int startCluster;
    char partname[256], filename[32];
    base_game_info_t *game;
    struct cdvdman_settings_bdm *settings;
    u32 layer1_start, layer1_offset;
    unsigned short int layer1_part;

    bdm_device_data_t *pDeviceData = NULL;

    if (gAutoLaunchBDMGame == NULL) {
        pDeviceData = (bdm_device_data_t *)itemList->priv;
        game = &pDeviceData->bdmGames[id];
    } else {
        pDeviceData = gAutoLaunchDeviceData;
        game = gAutoLaunchBDMGame;
    }

    int selectedCore = pgcfg->core_loader == CORE_LOADER_NEUTRINO ? CORE_LOADER_NEUTRINO : CORE_LOADER_WOPL;

    neutrino_path_t neutrinoPath;
    char neutrinoVmc0[256];
    char neutrinoVmc1[256];

    neutrinoPath.elf[0] = '\0';
    neutrinoPath.cwd[0] = '\0';
    neutrinoVmc0[0] = '\0';
    neutrinoVmc1[0] = '\0';

    if (selectedCore == CORE_LOADER_NEUTRINO) {
        if (game->format == GAME_FORMAT_USBLD || !strcasecmp(game->extension, ".zso")) {
            guiWarning("Neutrino does not support this file format, launching with <wOPL> core", 6);
            selectedCore = CORE_LOADER_WOPL;
        } else {
            if (!sbFindNeutrino(&neutrinoPath, pDeviceData->bdmPrefix)) {
                guiWarning("Neutrino ELF not found, launching with <wOPL> core", 6);
                selectedCore = CORE_LOADER_WOPL;
            }
        }
    }

    int size_mcemu_irx = 0;

    if (selectedCore == CORE_LOADER_WOPL) {
        char vmc_name[32], vmc_path[256], have_error = 0;
        int vmc_id;
        bdm_vmc_infos_t bdm_vmc_infos;
        vmc_superblock_t vmc_superblock;

        for (vmc_id = 0; vmc_id < 2; vmc_id++) {
            memset(&bdm_vmc_infos, 0, sizeof(bdm_vmc_infos_t));
            strncpy(vmc_name, vmc_id == 0 ? pgcfg->vmc1 : pgcfg->vmc2, sizeof(vmc_name) - 1);
            vmc_name[sizeof(vmc_name) - 1] = '\0';
            if (vmc_name[0]) {
                have_error = 1;
                int vmcSizeInMb = sysCheckVMC(pDeviceData->bdmPrefix, "/", vmc_name, 0, &vmc_superblock);
                if (vmcSizeInMb > 0) {
                    bdm_vmc_infos.flags = vmc_superblock.mc_flag & 0xFF;
                    bdm_vmc_infos.flags |= 0x100;
                    bdm_vmc_infos.specs.page_size = vmc_superblock.page_size;
                    bdm_vmc_infos.specs.block_size = vmc_superblock.pages_per_block;
                    bdm_vmc_infos.specs.card_size = vmc_superblock.pages_per_cluster * vmc_superblock.clusters_per_card;

                    snprintf(vmc_path, sizeof(vmc_path), "%sVMC/%s.bin", pDeviceData->bdmPrefix, vmc_name);

                    fd = open(vmc_path, O_RDONLY);
                    if (fd >= 0) {
                        iop_fd = ps2sdk_get_iop_fd(fd);
                        if (fileXioIoctl2(iop_fd, USBMASS_IOCTL_GET_LBA, NULL, 0, &startingLBA, sizeof(startingLBA)) == 0 && (startCluster = (unsigned int)fileXioIoctl(iop_fd, USBMASS_IOCTL_GET_CLUSTER, vmc_path)) != 0) {

                            // VMC only supports 32bit LBAs at the moment, so if the starting LBA + size of the VMC crosses the 32bit boundary
                            // just report the VMC as being fragmented to prevent file system corruption.
                            int vmcSectorCount = vmcSizeInMb * ((1024 * 1024) / 512); // size in MB * sectors per MB
                            if (startingLBA + vmcSectorCount > 0x100000000) {
                                LOG("BDMSUPPORT VMC bad LBA range\n");
                                have_error = 2;
                            }
                            // Check VMC cluster chain for fragmentation (write operation can cause damage to the filesystem).
                            else if (fileXioIoctl(iop_fd, USBMASS_IOCTL_CHECK_CHAIN, "") == 1) {
                                LOG("BDMSUPPORT Cluster Chain OK\n");
                                have_error = 0;
                                bdm_vmc_infos.active = 1;
                                bdm_vmc_infos.start_sector = (u32)startingLBA;
                                LOG("BDMSUPPORT VMC slot %d start: 0x%X\n", vmc_id, (u32)startingLBA);
                            } else {
                                LOG("BDMSUPPORT Cluster Chain NG\n");
                                have_error = 2;
                            }
                        }

                        close(fd);
                    }
                }
            }

            if (gAutoLaunchBDMGame == NULL) {
                if (have_error) {
                    char error[256];
                    if (have_error == 2) // VMC file is fragmented
                        snprintf(error, sizeof(error), _l(_STR_ERR_VMC_FRAGMENTED_CONTINUE), vmc_name, (vmc_id + 1));
                    else
                        snprintf(error, sizeof(error), _l(_STR_ERR_VMC_CONTINUE), vmc_name, (vmc_id + 1));
                    if (!guiMsgBox(error, 1, NULL)) {
                        return;
                    }
                }
            } else
                LOG("VMC error\n");

            for (i = 0; i < size_bdm_mcemu_irx; i++) {
                if (((u32 *)&bdm_mcemu_irx)[i] == (0xC0DEFAC0 + vmc_id)) {
                    if (bdm_vmc_infos.active)
                        size_mcemu_irx = size_bdm_mcemu_irx;
                    memcpy(&((u32 *)&bdm_mcemu_irx)[i], &bdm_vmc_infos, sizeof(bdm_vmc_infos_t));
                    break;
                }
            }
        }
    }

    void *irx = NULL;
    int irx_size = 0;
    if (pDeviceData->bdmDeviceType == BDM_TYPE_ATA) {
        irx = &bdm_ata_cdvdman_irx;
        irx_size = size_bdm_ata_cdvdman_irx;
    } else {
        irx = &bdm_cdvdman_irx;
        irx_size = size_bdm_cdvdman_irx;
    }

    compatmask = sbPrepare(game, pgcfg, irx_size, irx, &index);
    settings = (struct cdvdman_settings_bdm *)((u8 *)irx + index);
    if (settings == NULL)
        return;
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wstringop-overflow"
    memset(&settings->frags[0], 0, sizeof(bd_fragment_t) * BDM_MAX_FRAGS);
#pragma GCC diagnostic pop
    u8 iTotalFragCount = 0;

    //
    // Add ISO as fragfile[0] to fragment list
    //
    struct cdvdman_fragfile *iso_frag = &settings->fragfile[0];
    iso_frag->frag_start = 0;
    iso_frag->frag_count = 0;
    for (i = 0; i < game->parts; i++) {
        // Open file
        sbCreatePath(game, partname, pDeviceData->bdmPrefix, "/", i);
        fd = open(partname, O_RDONLY);
        iop_fd = ps2sdk_get_iop_fd(fd);
        if (fd < 0) {
            sbUnprepare(&settings->common);
            guiMsgBox(_l(_STR_ERR_FILE_INVALID), 0, NULL);
            return;
        }

        // Get fragment list
        int iFragCount = fileXioIoctl2(iop_fd, USBMASS_IOCTL_GET_FRAGLIST, NULL, 0, (void *)&settings->frags[iTotalFragCount], sizeof(bd_fragment_t) * (BDM_MAX_FRAGS - iTotalFragCount));
        if (iFragCount > BDM_MAX_FRAGS) {
            // Too many fragments
            close(fd);
            sbUnprepare(&settings->common);
            guiMsgBox(_l(_STR_ERR_FRAGMENTED), 0, NULL);
            return;
        }
        iso_frag->frag_count += iFragCount;
        iTotalFragCount += iFragCount;

        if ((gPS2Logo) && (i == 0))
            EnablePS2Logo = CheckPS2Logo(fd, 0);

        close(fd);
    }

    // Initialize layer 1 information.
    sbCreatePath(game, partname, pDeviceData->bdmPrefix, "/", 0);
    layer1_start = sbGetISO9660MaxLBA(partname);

    switch (game->format) {
        case GAME_FORMAT_USBLD:
            layer1_part = layer1_start / 0x80000;
            layer1_offset = layer1_start % 0x80000;
            sbCreatePath(game, partname, pDeviceData->bdmPrefix, "/", layer1_part);
            break;
        default: // Raw ISO9660 disc image; one part.
            layer1_part = 0;
            layer1_offset = layer1_start;
    }

    if (sbProbeISO9660(partname, game, layer1_offset) != 0) {
        layer1_start = 0;
        LOG("DVD detected.\n");
    } else {
        layer1_start -= 16;
        LOG("DVD-DL layer 1 @ part %u sector 0x%lx.\n", layer1_part, layer1_offset);
    }
    settings->common.layer1_start = layer1_start;

    // adjust ZSO cache
    settings->common.zso_cache = bdmCacheSize;
#ifdef CHEAT
    if ((result = sbLoadCheats(pDeviceData->bdmPrefix, game->startup)) < 0) {
        if (gAutoLaunchBDMGame == NULL) {
            switch (result) {
                case -ENOENT:
                    guiWarning(_l(_STR_NO_CHEATS_FOUND), 10);
                    break;
                default:
                    guiWarning(_l(_STR_ERR_CHEATS_LOAD_FAILED), 10);
            }
        } else
            LOG("Cheats error\n");
    }

    if ((result = sbLoadImage(pDeviceData->bdmPrefix, game->startup)) < 0) {
        if (gAutoLaunchBDMGame == NULL) {
            guiWarning(_l(_STR_ERR_IMAGE_LOAD_FAILED), 10);
        } else {
            LOG("Image error\n");
        }
    }
#endif

    if (gRememberLastPlayed)
        wOPLLastSave(game->startup);

    if (pgcfg->alt_startup[0]) {
        strncpy(filename, pgcfg->alt_startup, sizeof(filename) - 1);
        filename[sizeof(filename) - 1] = '\0';
    } else {
        strncpy(filename, game->startup, sizeof(filename) - 1);
        filename[sizeof(filename) - 1] = '\0';
    }

    // deinit will free per device data.. copy resolved device info before free to compare for launch
    char bdmCurrentDevice[16];
    int bdmCurrentType = pDeviceData->bdmDeviceType;

    snprintf(bdmCurrentDevice, sizeof(bdmCurrentDevice), "%s", pDeviceData->bdmTruePrefix);
    settings->bdDeviceId = pDeviceData->massDeviceIndex;

    if (bdmCurrentType != BDM_TYPE_USB && bdmCurrentType != BDM_TYPE_ILINK && bdmCurrentType != BDM_TYPE_SDC && bdmCurrentType != BDM_TYPE_ATA) {
        LOG("BDMSUPPORT: unsupported BDM device type %d (%s)\n", bdmCurrentType, bdmCurrentDevice);

        if (gAutoLaunchBDMGame == NULL)
            guiMsgBox(_l(_STR_ERR_FILE_INVALID), 0, NULL);

        return;
    }

    if (bdmCurrentType == BDM_TYPE_ATA) {
        // Get DMA settings for ATA mode.
        int dmaType = 0, dmaMode = 7;
        dmaMode = (pgcfg->dma != 7) ? pgcfg->dma : 7;

        // Set DMA mode and spindown time.
        if (dmaMode < 3)
            dmaType = 0x20;
        else {
            dmaType = 0x40;
            if (pDeviceData->ataHighestUDMAMode > 0)
                dmaMode = pDeviceData->ataHighestUDMAMode;
            else
                dmaMode -= 3;
        }

        hddSetTransferMode(dmaType, dmaMode);
        // gHDDSpindown [0..20] -> spindown [0..240] -> seconds [0..1200]
        hddSetIdleTimeout(gHDDSpindown * 12);
        settings->hddIsLBA48 = pDeviceData->bdmHddIsLBA48;
    }

    if (selectedCore == CORE_LOADER_NEUTRINO) {
        sbCreateNeutrinoVMCPath(neutrinoVmc0, sizeof(neutrinoVmc0), pDeviceData->bdmPrefix, pgcfg->vmc1);
        sbCreateNeutrinoVMCPath(neutrinoVmc1, sizeof(neutrinoVmc1), pDeviceData->bdmPrefix, pgcfg->vmc2);
    }

    if (!(selectedCore == CORE_LOADER_NEUTRINO && sbPathIsMC(neutrinoPath.elf)))
        sbMMCESendGameId(game->startup);

    int deinitException = NO_EXCEPTION;
    int deinitMode = gAutoLaunchBDMGame == NULL ? itemList->mode : BDM_MODE;

    if (selectedCore == CORE_LOADER_NEUTRINO) {
        int elfDevice = -1;
        int elfMode = sbGetPathModeAndDevice(neutrinoPath.elf, &elfDevice);

        if (elfMode >= 0) {
            deinitException = UNMOUNT_EXCEPTION;
            deinitMode = elfMode;
        }

        LOG("NEUTRINO ELF MODE=%d DEVICE=%d\n", elfMode, elfDevice);
    }

    if (gAutoLaunchBDMGame == NULL)
        deinit(deinitException, deinitMode); // CAREFUL: deinit will call bdmCleanUp, so bdmGames/game will be freed
    else {
        miniDeinit();

        free(gAutoLaunchBDMGame);
        gAutoLaunchBDMGame = NULL;

        free(gAutoLaunchDeviceData);
        gAutoLaunchDeviceData = NULL;
    }

    LOG("bdm pre sysLaunchLoaderElf\n");

    if (selectedCore == CORE_LOADER_NEUTRINO) {
        sysLaunchNeutrino(bdmCurrentDevice, partname, compatmask, EnablePS2Logo, neutrinoPath.elf, neutrinoPath.cwd, neutrinoVmc0, neutrinoVmc1);
        return;
    }

    switch (bdmCurrentType) {
        case BDM_TYPE_USB:
            settings->common.fakemodule_flags |= FAKE_MODULE_FLAG_USBD;

            if (settings->bdDeviceId == 0)
                sysLaunchLoaderElf(filename, "BDM_USB_MODE0", irx_size, irx, size_mcemu_irx, bdm_mcemu_irx, EnablePS2Logo, compatmask);
            else
                sysLaunchLoaderElf(filename, "BDM_USB_MODE1", irx_size, irx, size_mcemu_irx, bdm_mcemu_irx, EnablePS2Logo, compatmask);
            break;

        case BDM_TYPE_ILINK:
            settings->common.fakemodule_flags |= 0 /* TODO! fake ilinkman ? */;
            sysLaunchLoaderElf(filename, "BDM_ILK_MODE", irx_size, irx, size_mcemu_irx, bdm_mcemu_irx, EnablePS2Logo, compatmask);
            break;

        case BDM_TYPE_SDC:
            settings->common.fakemodule_flags |= 0;
            sysLaunchLoaderElf(filename, "BDM_M4S_MODE", irx_size, irx, size_mcemu_irx, bdm_mcemu_irx, EnablePS2Logo, compatmask);
            break;

        case BDM_TYPE_ATA:
            settings->common.fakemodule_flags |= FAKE_MODULE_FLAG_DEV9;
            settings->common.fakemodule_flags |= FAKE_MODULE_FLAG_ATAD;
            sysLaunchLoaderElf(filename, "BDM_ATA_MODE", irx_size, irx, size_mcemu_irx, bdm_mcemu_irx, EnablePS2Logo, compatmask);
            break;

        default:
            LOG("BDMSUPPORT: invalid BDM device type after deinit: %d\n", bdmCurrentType); // dont see how this would ever happen.. but..
            break;
    }
}

static void bdmGetInfo(item_list_t *itemList, int id, game_info_t *gi)
{
    bdm_device_data_t *pDeviceData = (bdm_device_data_t *)itemList->priv;
    sbPopulateConfig(&pDeviceData->bdmGames[id], pDeviceData->bdmPrefix, "/", gi, NULL);
}

static void bdmGetPgCfg(item_list_t *itemList, int id, per_game_cfg_t *cfg)
{
    bdm_device_data_t *pDeviceData = (bdm_device_data_t *)itemList->priv;
    sbPopulateConfig(&pDeviceData->bdmGames[id], pDeviceData->bdmPrefix, "/", NULL, cfg);
}

static int bdmSavePgCfg(item_list_t *itemList, int id, const per_game_cfg_t *cfg)
{
    bdm_device_data_t *pDeviceData = (bdm_device_data_t *)itemList->priv;
    return sbSaveConfig(&pDeviceData->bdmGames[id], pDeviceData->bdmPrefix, "/", cfg);
}

static int bdmGetImage(item_list_t *itemList, char *folder, int isRelative, char *value, char *suffix, GSTEXTURE *resultTex, short psm)
{
    char path[256];

    bdm_device_data_t *pDeviceData = (bdm_device_data_t *)itemList->priv;

    if (isRelative)
        snprintf(path, sizeof(path), "%s%s/%s_%s", pDeviceData->bdmPrefix, folder, value, suffix);
    else
        snprintf(path, sizeof(path), "%s%s_%s", folder, value, suffix);

    return texDiscoverLoad(resultTex, path, -1, 0);
}

static int bdmGetArchivedImage(item_list_t *itemList, char *folder, char *value, char *suffix, GSTEXTURE *resultTex, short psm)
{
    char path[256];

    snprintf(path, sizeof(path), "%s_%s", value, suffix);

    return texDiscoverLoad(resultTex, path, -1, 1);
}

static int bdmGetTextId(item_list_t *itemList)
{
    int mode = _STR_BDM_GAMES;

    bdm_device_data_t *pDeviceData = (bdm_device_data_t *)itemList->priv;

    switch (pDeviceData->bdmDeviceType) {
        case BDM_TYPE_USB:
            mode = _STR_USB_GAMES;
            break;
        case BDM_TYPE_ILINK:
            mode = _STR_ILINK_GAMES;
            break;
        case BDM_TYPE_SDC:
            mode = _STR_MX4SIO_GAMES;
            break;
        case BDM_TYPE_ATA:
            mode = _STR_HDD_GAMES;
            break;
    }

    return mode;
}

static int bdmGetIconId(item_list_t *itemList)
{
    int mode = BDM_ICON;

    bdm_device_data_t *pDeviceData = (bdm_device_data_t *)itemList->priv;

    switch (pDeviceData->bdmDeviceType) {
        case BDM_TYPE_USB:
            mode = USB_ICON;
            break;
        case BDM_TYPE_ILINK:
            mode = ILINK_ICON;
            break;
        case BDM_TYPE_SDC:
            mode = MX4SIO_ICON;
            break;
        case BDM_TYPE_ATA:
            mode = HDD_BD_ICON;
            break;
    }

    return mode;
}

// This may be called, even if bdmInit() was not.
static void bdmCleanUp(item_list_t *itemList, int exception)
{
    if (itemList->enabled) {
        LOG("BDMSUPPORT CleanUp\n");

        bdm_device_data_t *pDeviceData = (bdm_device_data_t *)itemList->priv;
        if (!pDeviceData)
            return;

        free(pDeviceData->bdmGames);
        free(pDeviceData);
        itemList->priv = NULL;

        //      if ((exception & UNMOUNT_EXCEPTION) == 0)
        //          ...
    }
}

// This may be called, even if bdmInit() was not.
static void bdmShutdown(item_list_t *itemList)
{
    char path[16];

    LOG("BDMSUPPORT Shutdown\n");

    bdm_device_data_t *pDeviceData = (bdm_device_data_t *)itemList->priv;

    if (pDeviceData && bdmBuildTruePath(path, sizeof(path), pDeviceData->bdmDeviceType, pDeviceData->massDeviceIndex, 0)) {
        // As required by some (typically 2.5") HDDs, issue the SCSI STOP UNIT command to avoid causing an emergency park.
        fileXioDevctl(path, USBMASS_DEVCTL_STOP_ALL, NULL, 0, NULL, 0);
    }

    if (itemList->enabled && pDeviceData) {
        LOG("BDMSUPPORT Shutdown free data\n");

        // Free device data.
        free(pDeviceData->bdmGames);
        free(pDeviceData);
        itemList->priv = NULL;
    }
}

static int bdmCheckVMC(item_list_t *itemList, char *name, int createSize)
{
    bdm_device_data_t *pDeviceData = (bdm_device_data_t *)itemList->priv;
    return sysCheckVMC(pDeviceData->bdmPrefix, "/", name, createSize, NULL);
}

static char *bdmGetPrefix(item_list_t *itemList)
{
    bdm_device_data_t *pDeviceData = (bdm_device_data_t *)itemList->priv;
    return pDeviceData->bdmPrefix;
}

static item_list_t bdmGameList = {
    BDM_MODE, 2, 0, 0, MENU_MIN_INACTIVE_FRAMES, BDM_MODE_UPDATE_DELAY, NULL, NULL, &bdmGetTextId, &bdmGetPrefix, &bdmInit, &bdmNeedsUpdate,
    &bdmUpdateGameList, &bdmGetGameCount, &bdmGetGame, &bdmGetGameName, &bdmGetGameNameLength, &bdmGetGameStartup, &bdmDeleteGame, &bdmRenameGame,
    &bdmLaunchGame, &bdmGetInfo, &bdmGetPgCfg, &bdmSavePgCfg, &bdmGetImage, &bdmGetArchivedImage, &bdmCleanUp, &bdmShutdown, &bdmCheckVMC, &bdmGetIconId};

void bdmInitSemaphore()
{
    // Create a semaphore so only one thread can load IOP modules at a time.
    ee_sema_t semaphore;
    semaphore.init_count = 1;
    semaphore.max_count = 1;
    semaphore.option = 0;
    bdmLoadModuleLock = CreateSema(&semaphore);
}

void bdmInitDevicesData()
{
    // If the device list hasn't been initialized do it now.
    if (bdmDeviceListInitialized == 0) {
        bdmDeviceListInitialized = 1;

        for (int i = 0; i < MAX_BDM_TRUE_DEVICES; i++) {
            // Setup the device list item.
            item_list_t *pDeviceSupport = &bdmDeviceList[i];
            memcpy(pDeviceSupport, &bdmGameList, sizeof(item_list_t));
            pDeviceSupport->mode = BDM_MODE + i;

            // Setup the per-device data.
            bdm_device_data_t *pDeviceData = (bdm_device_data_t *)malloc(sizeof(bdm_device_data_t));
            if (!pDeviceData) {
                pDeviceSupport->priv = NULL;
                continue;
            }

            memset(pDeviceData, 0, sizeof(bdm_device_data_t));
            pDeviceData->bdmDeviceType = BDM_TYPE_UNKNOWN;
            pDeviceSupport->priv = pDeviceData;
        }
    }

    // Refresh the visibility of the menu.
    for (int i = 0; i < MAX_BDM_TRUE_DEVICES; i++) {
        // Register the device structure into the UI.
        initSupport(&bdmDeviceList[i], BDM_MODE + i, 0);

        // If bdm support is set to auto then make the page invisible and reset the bdm tick counter, when a bdm device is mounted it will dynamically be made visible.
        // If bdm support is set to manual then only make the first page visible.
        if (bdmDeviceList[i].owner != NULL) {
            opl_io_module_t *pOwner = (opl_io_module_t *)bdmDeviceList[i].owner;
            bdm_device_data_t *pDeviceData = (bdm_device_data_t *)bdmDeviceList[i].priv;
            if (!pDeviceData) {
                pOwner->menuItem.visible = 0;
                continue;
            }

            if (gBDMStartMode == START_MODE_DISABLED) {
                pOwner->menuItem.visible = 0;
            } else if (gBDMStartMode == START_MODE_MANUAL) {
                // If BDM has already been started then make the page invisible and reset the bdm tick counter so visibility status is refreshed
                // according to device state.
                if (bdmDeviceModeStarted == 1) {
                    pOwner->menuItem.visible = 0;
                    pDeviceData->bdmDeviceTick = -1;
                } else
                    pOwner->menuItem.visible = (i == 0 ? 1 : 0);
            } else if (gBDMStartMode == START_MODE_AUTO) {
                pOwner->menuItem.visible = 0;
                pDeviceData->bdmDeviceTick = -1;
            }

            LOG("bdmInitDevicesData: setting device %d %s\n", i, (pOwner->menuItem.visible != 0 ? "visible" : "invisible"));
        }
    }
}

void bdmEnumerateDevices()
{
    LOG("bdmEnumerateDevices\n");

    // Initialize the device list data if it hasn't been initialized yet.
    bdmInitDevicesData();

    // Because bdmLoadModules is called before the config file is loaded bdmLoadBlockDeviceModules will not have loaded any
    // optional bdm modules. Now that the config file has been loaded try loading any optional modules that weren't previously loaded.
    ioPutRequest(IO_CUSTOM_SIMPLEACTION, &bdmLoadBlockDeviceModules);

    LOG("bdmEnumerateDevices done\n");
}

void bdmResolveLBA_UDMA(bdm_device_data_t *pDeviceData)
{
    // If atad is loaded then xhdd is also loaded, query the hdd to see if it supports LBA48 or not.
    pDeviceData->bdmHddIsLBA48 = fileXioDevctl("xhdd0:", ATA_DEVCTL_IS_48BIT, NULL, 0, NULL, 0);
    if (pDeviceData->bdmHddIsLBA48 < 0) {
        // Failed to query the LBA limit of the device, fail safe to LBA28.
        LOG("BDM device %d is backed by ATA but failed to get LBA limit %d\n", pDeviceData->massDeviceIndex, pDeviceData->bdmHddIsLBA48);
        pDeviceData->bdmHddIsLBA48 = 0;
    }

    // Query the drive for the highest UDMA mode.
    pDeviceData->ataHighestUDMAMode = fileXioDevctl("xhdd0:", ATA_DEVCTL_GET_HIGHEST_UDMA_MODE, NULL, 0, NULL, 0);
    if (pDeviceData->ataHighestUDMAMode < 0 || pDeviceData->ataHighestUDMAMode > 7) {
        // Failed to query highest UDMA mode supported.
        LOG("BDM device %d is backed by ATA but failed to get highest UDMA mode %d\n", pDeviceData->massDeviceIndex, pDeviceData->ataHighestUDMAMode);
        pDeviceData->ataHighestUDMAMode = 4;
    }

    // Set the UDMA mode to highest available.
    hddSetTransferMode(0x40, pDeviceData->ataHighestUDMAMode);
}

static const char *bdmGetDevicePrefix(int deviceType)
{
    switch (deviceType) {
        case BDM_TYPE_USB:
            return "usb";
        case BDM_TYPE_ILINK:
            return "ilink";
        case BDM_TYPE_SDC:
            return "mx4sio";
        case BDM_TYPE_ATA:
            return "ata";
        default:
            return NULL;
    }
}

static int bdmBuildTruePath(char *path, size_t pathSize, int deviceType, int deviceIndex, int withSlash)
{
    const char *prefix = bdmGetDevicePrefix(deviceType);
    int len;

    if (!path || !pathSize || !prefix || deviceIndex < 0)
        return 0;

    if (withSlash)
        len = snprintf(path, pathSize, "%s%d:/", prefix, deviceIndex);
    else
        len = snprintf(path, pathSize, "%s%d:", prefix, deviceIndex);

    if (len < 0 || (size_t)len >= pathSize) {
        path[0] = '\0';
        return 0;
    }

    return 1;
}

static int bdmOpenTrueDevice(int deviceType, int deviceIndex)
{
    char path[16];

    if (!bdmBuildTruePath(path, sizeof(path), deviceType, deviceIndex, 1))
        return -1;

    return fileXioDopen(path);
}

static int bdmSetDeviceTypeAndTruePrefix(bdm_device_data_t *pDeviceData, item_list_t *itemList, int deviceType, int deviceIndex)
{
    if (!pDeviceData)
        return BDM_TYPE_UNKNOWN;

    pDeviceData->bdmDeviceType = deviceType;

    if (!bdmBuildTruePath(pDeviceData->bdmTruePrefix, sizeof(pDeviceData->bdmTruePrefix), deviceType, deviceIndex, 0)) {
        pDeviceData->bdmDeviceType = BDM_TYPE_UNKNOWN;
        pDeviceData->bdmTruePrefix[0] = '\0';
        return BDM_TYPE_UNKNOWN;
    }

    if (itemList)
        itemList->flags = deviceType == BDM_TYPE_ATA ? MODE_FLAG_COMPAT_DMA : 0;

    return pDeviceData->bdmDeviceType;
}

static int bdmSetupDeviceData(bdm_device_data_t *pDeviceData, item_list_t *itemList, int deviceType, int deviceIndex, int dir)
{
    if (!pDeviceData || dir < 0)
        return 0;

    memset(pDeviceData->bdmDriver, 0, sizeof(pDeviceData->bdmDriver));
    pDeviceData->bdmTruePrefix[0] = '\0';
    pDeviceData->bdmPrefix[0] = '\0';
    pDeviceData->bdmDeviceType = BDM_TYPE_UNKNOWN;
    pDeviceData->massDeviceIndex = deviceIndex;

    fileXioIoctl2(dir, USBMASS_IOCTL_GET_DRIVERNAME, NULL, 0, &pDeviceData->bdmDriver, sizeof(pDeviceData->bdmDriver) - 1);
    fileXioIoctl2(dir, USBMASS_IOCTL_GET_DEVICE_NUMBER, NULL, 0, &pDeviceData->massDeviceIndex, sizeof(pDeviceData->massDeviceIndex));

    if (itemList)
        itemList->flags = 0;

    if (bdmSetDeviceTypeAndTruePrefix(pDeviceData, itemList, deviceType, pDeviceData->massDeviceIndex) == BDM_TYPE_UNKNOWN)
        return 0;

    if (gBDMPrefix[0] != '\0')
        snprintf(pDeviceData->bdmPrefix, sizeof(pDeviceData->bdmPrefix), "%s%s/", pDeviceData->bdmTruePrefix, gBDMPrefix);
    else
        snprintf(pDeviceData->bdmPrefix, sizeof(pDeviceData->bdmPrefix), "%s", pDeviceData->bdmTruePrefix);

    return 1;
}

static int bdmBuildUsbMassCompatCandidate(char *out, size_t out_len, const char *truePrefix, const char *tail)
{
    int len;

    if (!out || !out_len || !truePrefix || !truePrefix[0])
        return 0;

    if (tail && tail[0])
        len = snprintf(out, out_len, "%s%s", truePrefix, tail);
    else
        len = snprintf(out, out_len, "%s/", truePrefix);

    if (len < 0 || (size_t)len >= out_len) {
        out[0] = '\0';
        return 0;
    }

    return 1;
}

static int bdmResolveUsbMassCompatPathPass(char *out, size_t out_len, const char *tail, int massIndex, int strictIndex)
{
    int i;
    struct stat st;

    if (!bdmDeviceListInitialized)
        return 0;

    for (i = 0; i < MAX_BDM_TRUE_DEVICES; i++) {
        bdm_device_data_t *pDeviceData = bdmDeviceList[i].priv;
        char candidate[128];

        if (!pDeviceData || !pDeviceData->bdmTruePrefix[0])
            continue;

        if (pDeviceData->bdmDeviceType != BDM_TYPE_USB)
            continue;

        if (strictIndex && massIndex >= 0 && pDeviceData->massDeviceIndex != massIndex)
            continue;

        if (!bdmBuildUsbMassCompatCandidate(candidate, sizeof(candidate), pDeviceData->bdmTruePrefix, tail))
            continue;

        if (stat(candidate, &st) != 0)
            continue;

        snprintf(out, out_len, "%s", candidate);
        LOG("BDMSUPPORT: resolved USB mass compatibility path '%s'\n", out);

        return 1;
    }

    return 0;
}

int bdmResolveUsbMassCompatPath(char *out, size_t out_len, const char *path)
{
    const char *tail;
    int massIndex;

    if (!out || !out_len || !path)
        return 0;

    if (!pathParseDevicePrefix(path, "mass", &massIndex, &tail, 0))
        return 0;

    // First try a strict match using the reported BDM device number
    if (bdmResolveUsbMassCompatPathPass(out, out_len, tail, massIndex, 1))
        return 1;

    // Fallback wLE massN: does not always match the true BDM index
    if (bdmResolveUsbMassCompatPathPass(out, out_len, tail, massIndex, 0))
        return 1;

    LOG("BDMSUPPORT: could not resolve USB mass compatibility path '%s'\n", path);

    return 0;
}

int bdmUpdateDeviceData(item_list_t *itemList)
{
    int deviceType;
    int deviceIndex;

    // If bdm mode is disabled bail out as we don't want to update the visibility state of the device pages.
    if (gBDMStartMode == START_MODE_DISABLED)
        return 0;

    // LOG("bdmUpdateDeviceData: %d\n", itemList->mode);

    // Get the per-device data and check if the menu item is currently visible.
    bdm_device_data_t *pDeviceData = itemList->priv;
    if (!pDeviceData)
        return 0;

    int visible = itemList->owner != NULL ? ((opl_io_module_t *)itemList->owner)->menuItem.visible : 0;

    int slotIndex = itemList->mode - BDM_MODE;
    int found = 0;
    int dir = -1;

    if (slotIndex < 0 || slotIndex >= MAX_BDM_DEVICES)
        return 0;

    for (int type = BDM_TYPE_USB; type <= BDM_TYPE_ATA && dir < 0; type++) {
        if ((type == BDM_TYPE_USB && !gEnableUSB) || (type == BDM_TYPE_ILINK && !gEnableILK) || (type == BDM_TYPE_SDC && !gEnableMX4SIO) || (type == BDM_TYPE_ATA && !gEnableBdmHDD))
            continue;

        for (int index = 0; index < MAX_BDM_DEVICES; index++) {
            int testDir = bdmOpenTrueDevice(type, index);
            if (testDir < 0)
                continue;

            if (found == slotIndex) {
                deviceType = type;
                deviceIndex = index;
                dir = testDir;
                break;
            }

            fileXioDclose(testDir);
            found++;
        }
    }
    // LOG("opendir %s -> %d\n", path, dir);

    // If we opened the device and the menu isn't visible (OR is visible but hasn't been initialized ex: manual device start) initialize device info.
    if (dir >= 0 && (visible == 0 || pDeviceData->bdmPrefix[0] == '\0')) {
        int hadDevice = pDeviceData->bdmTruePrefix[0] != '\0' || pDeviceData->bdmPrefix[0] != '\0';
        int oldDeviceType = pDeviceData->bdmDeviceType;
        int oldMassDeviceIndex = pDeviceData->massDeviceIndex;
        char oldTruePrefix[sizeof(pDeviceData->bdmTruePrefix)];

        snprintf(oldTruePrefix, sizeof(oldTruePrefix), "%s", pDeviceData->bdmTruePrefix);

        if (!bdmSetupDeviceData(pDeviceData, itemList, deviceType, deviceIndex, dir)) {
            fileXioDclose(dir);
            return 0;
        }

        // If the device is backed by the ATA driver then get the supported LBA size for the drive.
        if (pDeviceData->bdmDeviceType == BDM_TYPE_ATA) {
            bdmResolveLBA_UDMA(pDeviceData);
            LOG("BDM device: %d (%d LBA%d UDMA%d) %s -> %s\n", itemList->mode, pDeviceData->massDeviceIndex, (pDeviceData->bdmHddIsLBA48 == 1 ? 48 : 28), pDeviceData->ataHighestUDMAMode, pDeviceData->bdmPrefix, pDeviceData->bdmDriver);
        } else
            LOG("BDM device: %d (%d) %s -> %s\n", itemList->mode, pDeviceData->massDeviceIndex, pDeviceData->bdmPrefix, pDeviceData->bdmDriver);

        // Make the menu item visible.
        if (itemList->owner != NULL) {
            LOG("bdmUpdateDeviceData: setting device %d visible\n", itemList->mode);
            ((opl_io_module_t *)itemList->owner)->menuItem.visible = 1;
        }

        // Close the device handle.
        fileXioDclose(dir);

        if (!hadDevice)
            return 1;

        if (oldDeviceType != pDeviceData->bdmDeviceType)
            return 1;

        if (oldMassDeviceIndex != pDeviceData->massDeviceIndex)
            return 1;

        if (strcmp(oldTruePrefix, pDeviceData->bdmTruePrefix))
            return 1;

        return 0;
    } else if (dir < 0 && visible == 1) {
        int hadDevice = pDeviceData->bdmTruePrefix[0] != '\0';

        // Device has been removed, make the menu item invisible. We can't really cleanup resources (like the game list) just yet
        // as we don't know if the data is being used asynchronously.
        pDeviceData->bdmTruePrefix[0] = '\0';
        pDeviceData->bdmPrefix[0] = '\0';
        if (itemList->owner != NULL) {
            LOG("bdmUpdateDeviceData: setting device %d invisible\n", itemList->mode);
            ((opl_io_module_t *)itemList->owner)->menuItem.visible = 0;
        }

        if (hadDevice) {
            LOG("BDM device: %d (%d) disconnected\n", itemList->mode, pDeviceData->massDeviceIndex);
            return -1;
        }

        return 0;
    }

    // No change to the device state detected.
    if (dir >= 0)
        fileXioDclose(dir);
    return 0;
}

void autoLaunchBDMGame(char *argv[])
{
    miniInit(BDM_MODE);

    gAutoLaunchBDMGame = malloc(sizeof(base_game_info_t));
    if (!gAutoLaunchBDMGame) {
        miniDeinit();
        return;
    }

    memset(gAutoLaunchBDMGame, 0, sizeof(base_game_info_t));

    int nameLen;
    int format = isValidIsoName(argv[1], &nameLen);
    if (format == GAME_FORMAT_OLD_ISO) {
        strncpy(gAutoLaunchBDMGame->name, &argv[1][GAME_STARTUP_MAX], nameLen);
        gAutoLaunchBDMGame->name[nameLen] = '\0';
        strncpy(gAutoLaunchBDMGame->extension, &argv[1][GAME_STARTUP_MAX + nameLen], sizeof(gAutoLaunchBDMGame->extension));
        gAutoLaunchBDMGame->extension[sizeof(gAutoLaunchBDMGame->extension) - 1] = '\0';
    } else {
        strncpy(gAutoLaunchBDMGame->name, argv[1], nameLen);
        gAutoLaunchBDMGame->name[nameLen] = '\0';
        strncpy(gAutoLaunchBDMGame->extension, &argv[1][nameLen], sizeof(gAutoLaunchBDMGame->extension));
        gAutoLaunchBDMGame->extension[sizeof(gAutoLaunchBDMGame->extension) - 1] = '\0';
    }

    snprintf(gAutoLaunchBDMGame->startup, sizeof(gAutoLaunchBDMGame->startup), argv[2]);

    if (strcasecmp("DVD", argv[3]) == 0)
        gAutoLaunchBDMGame->media = SCECdPS2DVD;
    else if (strcasecmp("CD", argv[3]) == 0)
        gAutoLaunchBDMGame->media = SCECdPS2CD;

    gAutoLaunchBDMGame->format = format;
    gAutoLaunchBDMGame->parts = 1; // ul not supported.

    gAutoLaunchDeviceData = malloc(sizeof(bdm_device_data_t));
    if (!gAutoLaunchDeviceData) {
        miniDeinit();

        free(gAutoLaunchBDMGame);
        gAutoLaunchBDMGame = NULL;

        return;
    }

    memset(gAutoLaunchDeviceData, 0, sizeof(bdm_device_data_t));

    int foundDevice = 0;

    delay(8);

    // Probe real BDM prefixes only.
    for (int typeSlot = 0; typeSlot < 4; typeSlot++) {
        int deviceType;

        switch (typeSlot) {
            case 0:
                deviceType = BDM_TYPE_USB;
                break;
            case 1:
                deviceType = BDM_TYPE_ILINK;
                break;
            case 2:
                deviceType = BDM_TYPE_SDC;
                break;
            case 3:
                deviceType = BDM_TYPE_ATA;
                break;
            default:
                continue;
        }

        for (int i = 0; i < MAX_BDM_DEVICES; i++) {
            bdm_device_data_t candidateData;
            int dir = bdmOpenTrueDevice(deviceType, i);

            if (dir >= 0) {
                memset(&candidateData, 0, sizeof(candidateData));
                bdmSetupDeviceData(&candidateData, NULL, deviceType, i, dir);

                if (candidateData.bdmDeviceType != BDM_TYPE_UNKNOWN && (!foundDevice || candidateData.bdmDeviceType == BDM_TYPE_ATA)) {
                    memcpy(gAutoLaunchDeviceData, &candidateData, sizeof(bdm_device_data_t));
                    foundDevice = 1;
                }

                if (candidateData.bdmDeviceType == BDM_TYPE_ATA) {
                    bdmResolveLBA_UDMA(gAutoLaunchDeviceData);
                    fileXioDclose(dir);
                    break; // Exit the loop if "ata" device is found
                }

                fileXioDclose(dir);
            } else
                break;

            delay(6);
        }
    }

    if (!foundDevice) {
        miniDeinit();

        free(gAutoLaunchBDMGame);
        gAutoLaunchBDMGame = NULL;

        free(gAutoLaunchDeviceData);
        gAutoLaunchDeviceData = NULL;

        return;
    }

    per_game_cfg_t pgcfg;
    sbPopulateConfig(gAutoLaunchBDMGame, gAutoLaunchDeviceData->bdmPrefix, "/", NULL, &pgcfg);

    bdmLaunchGame(NULL, -1, &pgcfg);
}

static int bdmDeviceIsATA(int deviceId)
{
    bdm_device_data_t data;

    int dir = bdmOpenTrueDevice(BDM_TYPE_ATA, deviceId);

    if (dir < 0)
        return 0;

    memset(&data, 0, sizeof(data));
    bdmSetupDeviceData(&data, NULL, BDM_TYPE_ATA, deviceId, dir);

    fileXioDclose(dir);

    return data.bdmDeviceType == BDM_TYPE_ATA;
}

static int bdmGetATADeviceId()
{
    for (int i = 0; i < MAX_BDM_DEVICES; i++) {
        if (bdmDeviceIsATA(i)) {
            return i;
        }
    }
    return -1;
}

int bdmHDDIsPresent(u32 timeoutMs)
{
    const int RETRY_DELAY = 100; // ms
    u32 start;

    if (!hddIsPresent())
        return 0;

    if (bdmGetATADeviceId() >= 0)
        return 1;

    if (timeoutMs == 0)
        return 0;

    start = GetTimerSystemTime();

    while (1) {
        u32 elapsed_ms = (GetTimerSystemTime() - start) / (kBUSCLK / 1000);

        if (elapsed_ms >= timeoutMs)
            break;

        DelayThread(RETRY_DELAY * 1000);

        if (bdmGetATADeviceId() >= 0)
            return 1;
    }

    LOG("bdmHDDIsPresent: waiting for BDM ATA device timed out.\n");

    return 0;
}
