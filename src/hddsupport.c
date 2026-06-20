#include "include/common.h"
#include "include/lang.h"
#include "include/gui.h"
#include "include/supportbase.h"
#include "include/hddsupport.h"
#include "include/util.h"
#include "include/themes.h"
#include "include/textures.h"
#include "include/ioman.h"
#include "include/system.h"
#include "include/extern_irx.h"
#ifdef CHEAT
#include "include/cheatman.h"
#endif
#include "modules/iopcore/common/cdvd_config.h"
#include "include/mcemu.h"
#include <malloc.h>
#include <dirent.h>
#include <libcdvd.h>
#include <kernel.h>
#include "opl-hdd-ioctl.h"
#include "include/initializer.h"
#include "include/config_wopl.h"
#include <stdlib.h>

#define NEWLIB_PORT_AWARE
#include <fileXio_rpc.h> // fileXioFormat, fileXioMount, fileXioUmount, fileXioDevctl
#include <io_common.h>   // FIO_MT_RDWR

#include <hdd-ioctl.h>
#include <speedregs.h>

#include "../modules/isofs/zso.h"


#define OPL_HDD_MODE_PS2LOGO_OFFSET 0x17F8

#define HDD_MODE_UPDATE_DELAY MENU_UPD_DELAY_NOUPDATE

#define PFS_INODE_MAX_BLOCKS 114

typedef struct
{
    u32 start;  // Sector address
    u32 length; // Sector count
} apa_sub_t;

typedef struct
{
    u8 unused;
    u8 sec;
    u8 min;
    u8 hour;
    u8 day;
    u8 month;
    u16 year;
} ps2time_t;
typedef struct
{
    u32 checksum;
    u32 magic; // APA_MAGIC
    u32 next;
    u32 prev;
    char id[APA_IDMAX];
    char rpwd[APA_PASSMAX];
    char fpwd[APA_PASSMAX];
    u32 start;
    u32 length;
    u16 type;
    u16 flags;
    u32 nsub;
    ps2time_t created;
    u32 main;
    u32 number;
    u32 modver;
    u32 pading1[7];
    char pading2[128];
    struct
    {
        char magic[32];
        u32 version;
        u32 nsector;
        ps2time_t created;
        u32 osdStart;
        u32 osdSize;
        char pading3[200];
    } mbr;
    apa_sub_t subs[APA_MAXSUB];
} apa_header_t;

typedef struct
{
    u32 number;
    u16 subpart;
    u16 count;
} pfs_blockinfo_t;

typedef struct
{
    u32 checksum;
    u32 magic;
    pfs_blockinfo_t inode_block;
    pfs_blockinfo_t next_segment;
    pfs_blockinfo_t last_segment;
    pfs_blockinfo_t unused;
    pfs_blockinfo_t data[PFS_INODE_MAX_BLOCKS];
    u16 mode;
    u16 attr;
    u16 uid;
    u16 gid;
    ps2time_t atime;
    ps2time_t ctime;
    ps2time_t mtime;
    u64 size;
    u32 number_blocks;
    u32 number_data;
    u32 number_segdesg;
    u32 subpart;
    u32 reserved[4];
} pfs_inode_t;

typedef struct // size = 1024
{
    u32 checksum; // HDL uses 0xdeadfeed magic here
    u32 magic;
    char gamename[160];
    u8 hdl_compat_flags;
    u8 ops2l_compat_flags;
    u8 dma_type;
    u8 dma_mode;
    char startup[60];
    u32 layer1_start;
    u32 discType;
    int num_partitions;
    struct
    {
        u32 part_offset; // in MB
        u32 data_start;  // in sectors
        u32 part_size;   // in KB
    } part_specs[65];
} hdl_apa_header;


typedef struct
{
    int active;                 /* Activation flag */
    apa_sub_t parts[5];         /* Vmc file Apa partitions */
    pfs_blockinfo_t blocks[10]; /* Vmc file Pfs inode */
    int flags;                  /* Card flag */
    vmc_spec_t specs;           /* Card specifications */
} hdd_vmc_infos_t;

#define HDL_GAME_DATA_OFFSET 0x100000 // Sector 0x800 in the extended attribute area.
#define HDL_FS_MAGIC         0x1337

#define WOPL_PARTITION "+" WOPL_CONFIG_NAME

extern int probed_fd;
extern u32 probed_lba;
u8 IOBuffer[2048] ALIGNED(64); // one sector

static unsigned char hddForceUpdate = 0;
static unsigned char hddModulesLoaded = 0;
static unsigned char hddHDProKitDetected = 0;
static unsigned char hddModulesLoadCount = 0;
static unsigned char hddSupportModulesLoaded = 0;

static char *hddPrefix = "pfs0:";
static hdl_games_list_t hddGames;

// forward declaration
static item_list_t hddGameList;

int gHDDSpindown;
int gHDDStartMode;
int hddCacheSize;
hdl_game_info_t *gAutoLaunchGame;
int gHDDGameListCache;
char gOPLPart[128];
char *gHDDPrefix;
typedef enum {
    HDD_LOADMODULES_STATUS_ERROR = -2,
    HDD_LOADMODULES_STATUS_UNK = -1,
    HDD_LOADMODULES_STATUS_NOERROR,
    HDD_LOADMODULES_STATUS_ALREADYLOADED,
    HDD_LOADMODULES_STATUS_BUSYLOADING,
    HDD_LOADMODULES_STATUS_COUNT,
} hdd_loadmodules_status;

static int hddLoadGameListCache(hdl_games_list_t *cache);
static int hddUpdateGameListCache(hdl_games_list_t *cache, hdl_games_list_t *game_list);

int hddCheck(void)
{
    int ret;

    ret = fileXioDevctl("hdd0:", HDIOC_STATUS, NULL, 0, NULL, 0);
    LOG("HDD: Status is %d\n", ret);
    // 0 = HDD connected and formatted, 1 = not formatted, 2 = HDD not usable, 3 = HDD not connected.
    if ((ret >= 3) || (ret < 0))
        return -1;

    return ret;
}

int hddSetTransferMode(int type, int mode)
{
    hddAtaSetMode_t *args = (hddAtaSetMode_t *)IOBuffer;

    args->type = type;
    args->mode = mode;

    return fileXioDevctl("xhdd0:", ATA_DEVCTL_SET_TRANSFER_MODE, args, sizeof(hddAtaSetMode_t), NULL, 0);
}

static int hddIs48bit(void)
{
    return fileXioDevctl("xhdd0:", ATA_DEVCTL_IS_48BIT, NULL, 0, NULL, 0);
}

void hddSetIdleTimeout(int timeout)
{
    // From hdparm man:
    // A value of zero means "timeouts  are  disabled":  the
    // device will not automatically enter standby mode.  Values from 1
    // to 240 specify multiples of 5 seconds, yielding timeouts from  5
    // seconds to 20 minutes.  Values from 241 to 251 specify from 1 to
    // 11 units of 30 minutes, yielding timeouts from 30 minutes to 5.5
    // hours.   A  value  of  252  signifies a timeout of 21 minutes. A
    // value of 253 sets a vendor-defined timeout period between 8  and
    // 12  hours, and the value 254 is reserved.  255 is interpreted as
    // 21 minutes plus 15 seconds.  Note that  some  older  drives  may
    // have very different interpretations of these values.

    u8 standbytimer = (u8)timeout;

    fileXioDevctl("hdd0:", HDIOC_IDLE, &standbytimer, 1, NULL, 0);
    fileXioDevctl("hdd1:", HDIOC_IDLE, &standbytimer, 1, NULL, 0);
}

static void hddSetIdleImmediate(void)
{
    fileXioDevctl("hdd0:", HDIOC_IDLEIMM, NULL, 0, NULL, 0);
    fileXioDevctl("hdd1:", HDIOC_IDLEIMM, NULL, 0, NULL, 0);
}

int hddReadSectors(u32 lba, u32 nsectors, void *buf)
{
    hddAtaTransfer_t *args = (hddAtaTransfer_t *)IOBuffer;

    args->lba = lba;
    args->size = nsectors;

    if (fileXioDevctl("hdd0:", HDIOC_READSECTOR, args, sizeof(hddAtaTransfer_t), buf, nsectors * 512) != 0)
        return -1;

    return 0;
}

static int hddWriteSectors(u32 lba, u32 nsectors, const void *buf)
{
    static u8 WriteBuffer[2 * 512 + sizeof(hddAtaTransfer_t)] ALIGNED(64); // Has to be a different buffer from IOBuffer (input can be in IOBuffer).
    int argsz;
    hddAtaTransfer_t *args = (hddAtaTransfer_t *)WriteBuffer;

    if (nsectors > 2) // Sanity check
        return -ENOMEM;

    args->lba = lba;
    args->size = nsectors;
    memcpy(args->data, buf, nsectors * 512);

    argsz = sizeof(hddAtaTransfer_t) + (nsectors * 512);

    if (fileXioDevctl("hdd0:", HDIOC_WRITESECTOR, args, argsz, NULL, 0) != 0)
        return -1;

    return 0;
}

struct GameDataEntry
{
    u32 lba, size;
    struct GameDataEntry *next;
    char id[APA_IDMAX + 1];
} GameDataEntry;

static void hddFreeHDLGamelist(hdl_games_list_t *game_list)
{
    if (game_list->games != NULL) {
        free(game_list->games);
        game_list->games = NULL;
        game_list->count = 0;
    }
}

static int hddGetHDLGameInfo(struct GameDataEntry *game, hdl_game_info_t *ginfo)
{
    int ret;

    ret = hddReadSectors(game->lba, 2, IOBuffer);
    if (ret == 0) {

        hdl_apa_header *hdl_header = (hdl_apa_header *)IOBuffer;

        strncpy(ginfo->partition_name, game->id, APA_IDMAX);
        ginfo->partition_name[APA_IDMAX] = '\0';
        strncpy(ginfo->name, hdl_header->gamename, HDL_GAME_NAME_MAX);
        ginfo->name[HDL_GAME_NAME_MAX] = '\0';
        strncpy(ginfo->startup, hdl_header->startup, sizeof(ginfo->startup) - 1);
        ginfo->startup[sizeof(ginfo->startup) - 1] = '\0';
        ginfo->hdl_compat_flags = hdl_header->hdl_compat_flags;
        ginfo->ops2l_compat_flags = hdl_header->ops2l_compat_flags;
        ginfo->dma_type = hdl_header->dma_type;
        ginfo->dma_mode = hdl_header->dma_mode;
        ginfo->layer_break = hdl_header->layer1_start;
        ginfo->disctype = (u8)hdl_header->discType;
        ginfo->start_sector = game->lba;
        ginfo->total_size_in_kb = game->size * 2; // size * 2048 / 1024 = 2x
    } else
        ret = -1;

    return ret;
}

//-------------------------------------------------------------------------
static struct GameDataEntry *GetGameListRecord(struct GameDataEntry *head, const char *partition)
{
    struct GameDataEntry *current;

    for (current = head; current != NULL; current = current->next) {
        if (!strncmp(current->id, partition, APA_IDMAX)) {
            return current;
        }
    }

    return NULL;
}

static int hddGetHDLGamelist(hdl_games_list_t *game_list)
{
    struct GameDataEntry *head, *current, *next, *pGameEntry;
    unsigned int count, i;
    iox_dirent_t dirent;
    int fd, ret;

    hddFreeHDLGamelist(game_list);

    ret = 0;
    if ((fd = fileXioDopen("hdd0:")) >= 0) {
        head = current = NULL;
        count = 0;
        while (fileXioDread(fd, &dirent) > 0) {
            if (dirent.stat.mode == HDL_FS_MAGIC) {
                if ((pGameEntry = GetGameListRecord(head, dirent.name)) == NULL) {
                    if (head == NULL) {
                        current = head = malloc(sizeof(struct GameDataEntry));
                    } else {
                        current = current->next = malloc(sizeof(struct GameDataEntry));
                    }

                    if (current == NULL)
                        break;

                    strncpy(current->id, dirent.name, APA_IDMAX);
                    current->id[APA_IDMAX] = '\0';
                    count++;
                    current->next = NULL;
                    current->size = 0;
                    current->lba = 0;
                    pGameEntry = current;
                }

                if (!(dirent.stat.attr & APA_FLAG_SUB)) {
                    // Note: The APA specification states that there is a 4KB area used for storing the partition's information, before the extended attribute area.
                    pGameEntry->lba = dirent.stat.private_5 + (HDL_GAME_DATA_OFFSET + 4096) / 512;
                }

                pGameEntry->size += (dirent.stat.size / 4); // size in HDD sectors * (512 / 2048) = 0.25x
            }
        }

        fileXioDclose(fd);

        if (head != NULL) {
            if ((game_list->games = malloc(sizeof(hdl_game_info_t) * count)) != NULL) {
                memset(game_list->games, 0, sizeof(hdl_game_info_t) * count);

                for (i = 0, current = head; i < count; i++, current = current->next) {
                    if ((ret = hddGetHDLGameInfo(current, &game_list->games[i])) != 0)
                        break;
                }

                if (ret) {
                    free(game_list->games);
                    game_list->games = NULL;
                } else {
                    game_list->count = count;
                }
            } else {
                ret = ENOMEM;
            }

            for (current = head; current != NULL; current = next) {
                next = current->next;
                free(current);
            }
        }
    } else {
        ret = fd;
    }

    return ret;
}


static int hddSetHDLGameInfo(hdl_game_info_t *ginfo)
{
    if (hddReadSectors(ginfo->start_sector, 2, IOBuffer) != 0)
        return -EIO;

    hdl_apa_header *hdl_header = (hdl_apa_header *)IOBuffer;

    // just change game name and compat flags !!!
    strncpy(hdl_header->gamename, ginfo->name, sizeof(hdl_header->gamename));
    hdl_header->gamename[sizeof(hdl_header->gamename) - 1] = '\0';
    // hdl_header->hdl_compat_flags = ginfo->hdl_compat_flags;
    hdl_header->ops2l_compat_flags = ginfo->ops2l_compat_flags;
    hdl_header->dma_type = ginfo->dma_type;
    hdl_header->dma_mode = ginfo->dma_mode;

    if (hddWriteSectors(ginfo->start_sector, 2, IOBuffer) != 0)
        return -EIO;

    return 0;
}

static int hddDeleteHDLGame(hdl_game_info_t *ginfo)
{
    char path[38];

    LOG("HDD Delete game: '%s'\n", ginfo->name);

    sprintf(path, "hdd0:%s", ginfo->partition_name);

    return unlink(path);
}

static int hddGetPartitionInfo(const char *name, apa_sub_t *parts)
{
    u32 lba;
    iox_stat_t stat;
    apa_header_t *header;
    int result, i;

    if ((result = fileXioGetStat(name, &stat)) >= 0) {
        lba = stat.private_5;
        header = (apa_header_t *)IOBuffer;

        if (hddReadSectors(lba, sizeof(apa_header_t) / 512, header) == 0) {
            parts[0].start = header->start;
            parts[0].length = header->length;

            for (i = 0; i < header->nsub; i++)
                parts[1 + i] = header->subs[i];

            result = header->nsub + 1;
        } else
            result = -EIO;
    }

    return result;
}

static int hddGetFileBlockInfo(const char *name, const apa_sub_t *subs, pfs_blockinfo_t *blocks, int max)
{
    u32 lba;
    iox_stat_t stat;
    pfs_inode_t *inode;
    int result;

    if ((result = fileXioGetStat(name, &stat)) >= 0) {
        lba = subs[stat.private_4].start + stat.private_5;
        inode = (pfs_inode_t *)IOBuffer;

        if (hddReadSectors(lba, sizeof(pfs_inode_t) / 512, inode) == 0) {
            if (inode->number_data < max) {
                memcpy(blocks, inode->data, max * sizeof(pfs_blockinfo_t));
                result = inode->number_data;
            } else
                result = -ENOMEM;
        } else
            result = -EIO;
    }

    return result;
}


static void hddInitModules(void)
{
    hddLoadModules();
    hddLoadSupportModules();

    // update Themes
    char path[256];
    sprintf(path, "%sTHM", gHDDPrefix);
    thmAddElements(path, "/", 1);

    sprintf(path, "%sLNG", gHDDPrefix);
    lngAddLanguages(path, "/", hddGameList.mode);

    sbCreateFolders(gHDDPrefix, 0);
}

// HD Pro Kit is mapping the 1st word in ROM0 seg as a main ATA controller,
// The pseudo ATA controller registers are accessed (input/ouput) by writing
// an id to the main ATA controller
#define HDPROreg_IO8   (*(volatile unsigned char *)0xBFC00000)
#define CDVDreg_STATUS (*(volatile unsigned char *)0xBF40200A)

static int hddCheckHDProKit(void)
{
    int ret = 0;

    DIntr();
    ee_kmode_enter();
    // HD Pro IO start commands sequence
    HDPROreg_IO8 = 0x72;
    CDVDreg_STATUS = 0;
    HDPROreg_IO8 = 0x34;
    CDVDreg_STATUS = 0;
    HDPROreg_IO8 = 0x61;
    CDVDreg_STATUS = 0;
    u32 res = HDPROreg_IO8;
    CDVDreg_STATUS = 0;

    // check result
    if ((res & 0xff) == 0xe7) {
        // HD Pro IO finish commands sequence
        HDPROreg_IO8 = 0xf3;
        CDVDreg_STATUS = 0;
        ret = 1;
    }
    ee_kmode_exit();
    EIntr();

    if (ret)
        LOG("HDDSUPPORT HD Pro Kit detected!\n");

    return ret;
}

/**
 * Some compatible adaptors may malfunction if transfers are not done according
 * to the old ps2atad design. Official adaptors appear to have a 0x0001 set for
 * this register, but not compatibles. While official I/O to this register are
 * 8-bit, some compatibles have a 0x01 for the lower 8-bits, but the upper
 * 8-bits contain some random value. Hence perform a 16-bit read instead.
 */
static int hddCheckGameStar(void)
{
    int ret = 0;
    USE_SPD_REGS;

    DIntr();
    ee_kmode_enter();

    ret = (SPD_REG16(0x20) != 1);

    ee_kmode_exit();
    EIntr();

    if (ret)
        LOG("HDDSUPPORT GameStar detected!\n");

    return ret;
}

// Taken from libhdd:
#define PFS_ZONE_SIZE 8192
#define PFS_FRAGMENT  0x00000000

static void hddCheckOPLFolder(const char *mountPoint)
{
    DIR *dir;
    char path[32];

    sprintf(path, "%s%s", mountPoint, WOPL_CONFIG_NAME);

    dir = opendir(path);
    if (dir == NULL)
        mkdir(path, 0777);
    else
        closedir(dir);
}

static void hddFindOPLPartition(void)
{
    int fd, ret = 0;

    fileXioUmount(hddPrefix);
    ret = fileXioMount("pfs0:", "hdd0:__common", FIO_MT_RDWR);
    if (ret == 0) {
        const char *paths[] = {
            "pfs0:" WOPL_CONFIG_NAME "/conf_hdd.cfg",
            "pfs0:OPL/conf_hdd.cfg", // for OPL Launcher backwards compat
            NULL};

        for (int i = 0; paths[i]; i++) {
            fd = open(paths[i], O_RDONLY);
            if (fd >= 0) {
                char line[128];
                int n = read(fd, line, sizeof(line) - 1);
                close(fd);
                if (n > 0) {
                    line[n] = '\0';
                    char *val = strchr(line, '=');
                    if (val) {
                        val++;
                        char *cr = strchr(val, '\r');
                        if (cr)
                            *cr = '\0';
                        char *nl = strchr(val, '\n');
                        if (nl)
                            *nl = '\0';
                        snprintf(gOPLPart, sizeof(gOPLPart), "hdd0:%s", val);

                        return;
                    }
                }
            }
        }

        // not found anywhere.. create in wOPL location with default
        hddCheckOPLFolder(hddPrefix);
        char path[256];
        snprintf(path, sizeof(path), "pfs0:%s/conf_hdd.cfg", WOPL_CONFIG_NAME);
        fd = open(path, O_CREAT | O_TRUNC | O_WRONLY);
        if (fd >= 0) {
            char buf[64];
            snprintf(buf, sizeof(buf), "hdd_partition=%s\n", WOPL_PARTITION);
            write(fd, buf, strlen(buf));
            close(fd);
        }
    }

    snprintf(gOPLPart, sizeof(gOPLPart), "hdd0:%s", WOPL_PARTITION);
}

static int hddCreateOPLPartition(const char *name)
{
    int formatArg[3] = {PFS_ZONE_SIZE, 0x2d66, PFS_FRAGMENT};
    int fd, result;
    char cmd[140];

    sprintf(cmd, "%s,,,128M,PFS", name);
    if ((fd = open(cmd, O_CREAT | O_TRUNC | O_WRONLY)) >= 0) {
        close(fd);
        result = fileXioFormat(hddPrefix, name, (const char *)&formatArg, sizeof(formatArg));
    } else {
        result = fd;
    }

    return result;
}

int hddLoadModules(void)
{
    int retLoadModule;
    int retStatus = HDD_LOADMODULES_STATUS_UNK;

    LOG("HDDSUPPORT LoadModules %d\n", hddModulesLoadCount);

    if (hddModulesLoaded)
        retStatus = HDD_LOADMODULES_STATUS_ALREADYLOADED;

    if (hddModulesLoadCount == 0) {
        // Increment the load count as soon as possible to prevent thread scheduling from allowing another thread to
        // call into here and try to double load modules.
        hddModulesLoadCount = 1;

        // DEV9 must be loaded, as HDD.IRX depends on it. Even if not required by the I/F (i.e. HDPro)
        sysInitDev9();

        // try to detect HD Pro Kit (not the connected HDD),
        // if detected it loads the specific ATAD module
        hddHDProKitDetected = hddCheckHDProKit();
        if (hddHDProKitDetected) {
            LOG("[ATAD_HDPRO]:\n");
            retLoadModule = sysLoadModuleBuffer(&hdpro_atad_irx, size_hdpro_atad_irx, 0, NULL);
            LOG("[XHDD]:\n");
            sysLoadModuleBuffer(&xhdd_irx, size_xhdd_irx, 6, "-hdpro");
        } else {
            LOG("[ATAD]:\n");
            retLoadModule = sysLoadModuleBuffer(&ps2atad_irx, size_ps2atad_irx, 0, NULL);
            LOG("[XHDD]:\n");
            sysLoadModuleBuffer(&xhdd_irx, size_xhdd_irx, 0, NULL);
        }

        if (retLoadModule < 0) {
            LOG("HDD: No HardDisk Drive detected.\n");
            guiSetErrorMessageWithCode(_STR_HDD_NOT_CONNECTED_ERROR, ERROR_HDD_IF_NOT_DETECTED);
            retStatus = HDD_LOADMODULES_STATUS_ERROR;
        } else {
            retStatus = HDD_LOADMODULES_STATUS_NOERROR;
            hddModulesLoaded = 1;
        }
    } else {
        hddModulesLoadCount++;
        if (!hddModulesLoaded)
            retStatus = HDD_LOADMODULES_STATUS_BUSYLOADING;
    }

    LOG("HDDSUPPORT LoadModules done\n");
    return retStatus;
}

// Returns 1 for MBR/GPT, 0 for APA, and -1 if an error occured
int hddDetectNonSonyFileSystem()
{
    int result = -1;
    // Allocate memory for storing data for the first two sectors.
    u8 *pSectorData = (u8 *)malloc(512 * 2);
    if (pSectorData == NULL) {
        LOG("hddDetectNonSonyFileSystem: failed to allocate scratch memory\n");
        return -1;
    }

    // Trying to load the APA/PFS irx modules when a non-sony formatted HDD is connected (ie: MBR/GPT  w/ exFAT) runs
    // the risk of corrupting the HDD. To avoid that get the first two sectors and perform some sanity checks. If
    // we reasonably suspect the disk is not APA formatted bail out from loading the sony fs irx modules.
    result = fileXioDevctl("xhdd0:", ATA_DEVCTL_READ_PARTITION_SECTOR, NULL, 0, pSectorData, 512 * 2);
    if (result < 0) {
        LOG("hddDetectNonSonyFileSystem: failed to read data from hdd %d\n", result);
        free(pSectorData);
        return -1;
    }

    // Check for MBR signature.
    if (pSectorData[0x1FE] == 0x55 && pSectorData[0x1FF] == 0xAA) {
        // Found MBR partition type.
        LOG("hddDetectNonSonyFileSystem: found MBR partition data\n");
        result = 1;
    } else if (strncmp((const char *)&pSectorData[0x200], "EFI PART", 8) == 0) {
        // Found GPT partition type.
        LOG("hddDetectNonSonyFileSystem: found GPT partition data\n");
        result = 1;
    } else if (strncmp((const char *)&pSectorData[4], "APA", 3) == 0) {
        // Found APA partition type.
        LOG("hddDetectNonSonyFileSystem: found APA partition data\n");
        result = 0;
    } else {
        // Even though we didn't find evidence of non-APA partition data, if we load the APA irx module
        // it will write to the drive and potentially corrupt any data that might be there.
        LOG("hddDetectNonSonyFileSystem: partition data not recognized\n");
        result = -1;
    }

    // Cleanup and return.
    free(pSectorData);
    return result;
}

void hddLoadSupportModules(void)
{
    static char hddarg[] = "-o"
                           "\0"
                           "4"
                           "\0"
                           "-n"
                           "\0"
                           "20";
    static char pfsarg[] = "\0"
                           "-o" // max open
                           "\0"
                           "10" // Default value: 2
                           "\0"
                           "-n" // Number of buffers
                           "\0"
                           "40"; // Default value: 8 | Max value: 127

    LOG("HDDSUPPORT LoadSupportModules\n");

    // Check if the drive contains MBR/GPT partition data before we load the APA/PFS modules. If the drive is not
    // APA then loading the APA irx modules can corrupt the drive as it will try to write APA partition data.
    if (hddDetectNonSonyFileSystem() != 0) {
        // Drive is MBR/GPT style, or unknown, bail out or risk corrupting the drive.
        LOG("HDDSUPPORT LoadSupportModules bailing out early...\n");
        return;
    }

    if (!hddSupportModulesLoaded) {
        LOG("[HDD]:\n");
        int ret = sysLoadModuleBuffer(&ps2hdd_irx, size_ps2hdd_irx, sizeof(hddarg), hddarg);
        if (ret < 0) {
            LOG("HDD: No HardDisk Drive detected.\n");
            guiSetErrorMessageWithCode(_STR_HDD_NOT_CONNECTED_ERROR, ERROR_HDD_MODULE_HDD_FAILURE);
            return;
        }

        // Check if a HDD unit is connected
        if (hddCheck() < 0) {
            LOG("HDD: No HardDisk Drive detected.\n");
            guiSetErrorMessageWithCode(_STR_HDD_NOT_CONNECTED_ERROR, ERROR_HDD_NOT_DETECTED);
            return;
        }

        LOG("[PS2FS]:\n");
        ret = sysLoadModuleBuffer(&ps2fs_irx, size_ps2fs_irx, sizeof(pfsarg), pfsarg);
        if (ret < 0) {
            LOG("HDD: HardDisk Drive not formatted (PFS).\n");
            guiSetErrorMessageWithCode(_STR_HDD_NOT_FORMATTED_ERROR, ERROR_HDD_MODULE_PFS_FAILURE);
            return;
        }

        hddSupportModulesLoaded = 1;
        LOG("HDDSUPPORT modules loaded\n");

        if (gOPLPart[0] == '\0')
            hddFindOPLPartition();

        fileXioUmount(hddPrefix);

        ret = fileXioMount(hddPrefix, gOPLPart, FIO_MT_RDWR);
        if (ret == -ENOENT) {
            // Attempt to create the partition.
            if ((hddCreateOPLPartition(gOPLPart)) >= 0)
                fileXioMount(hddPrefix, gOPLPart, FIO_MT_RDWR);
        }

        if (gOPLPart[5] != '+') {
            hddCheckOPLFolder(hddPrefix);
            gHDDPrefix = "pfs0:wOPL/";
        }
    }
}

static void hddInit(item_list_t *itemList)
{
    LOG("HDDSUPPORT Init\n");
    hddForceUpdate = 0; // Use cache at initial startup.
    itemList->delay = gHDDFramesDelay;
    ioPutRequest(IO_CUSTOM_SIMPLEACTION, &hddInitModules);
    hddGameList.enabled = 1;
    itemList->enabled = 1;
}

item_list_t *hddGetObject(int initOnly)
{
    if (initOnly && !hddGameList.enabled)
        return NULL;
    return &hddGameList;
}

static int hddNeedsUpdate(item_list_t *itemList)
{ /* Auto refresh is disabled by setting HDD_MODE_UPDATE_DELAY to MENU_UPD_DELAY_NOUPDATE, within hddsupport.h.
       Hence any update request would be issued by the user, which should be taken as an explicit request to re-scan the HDD. */
    return 1;
}

static int hddUpdateGameList(item_list_t *itemList)
{
    hdl_games_list_t hddGamesNew;
    int ret;

    if (((ret = hddLoadGameListCache(&hddGames)) != 0) || (hddForceUpdate)) {
        hddGamesNew.count = 0;
        hddGamesNew.games = NULL;
        ret = hddGetHDLGamelist(&hddGamesNew);
        if (ret == 0) {
            hddUpdateGameListCache(&hddGames, &hddGamesNew);
            hddFreeHDLGamelist(&hddGames);
            hddGames = hddGamesNew;
        }
    }

    hddForceUpdate = 1; // Subsequent refresh operations will cause the HDD to be scanned.

    return (ret == 0 ? hddGames.count : 0);
}

static int hddGetGameCount(item_list_t *itemList)
{
    return hddGames.count;
}

static void *hddGetGame(item_list_t *itemList, int id)
{
    return (void *)&hddGames.games[id];
}

static char *hddGetGameName(item_list_t *itemList, int id)
{
    return hddGames.games[id].name;
}

static int hddGetGameNameLength(item_list_t *itemList, int id)
{
    return HDL_GAME_NAME_MAX + 1;
}

static char *hddGetGameStartup(item_list_t *itemList, int id)
{
    return hddGames.games[id].startup;
}

static void hddDeleteGame(item_list_t *itemList, int id)
{
    hddDeleteHDLGame(&hddGames.games[id]);
    hddForceUpdate = 1;
}

static void hddRenameGame(item_list_t *itemList, int id, char *newName)
{
    hdl_game_info_t *game = &hddGames.games[id];
    strcpy(game->name, newName);
    hddSetHDLGameInfo(&hddGames.games[id]);
    hddForceUpdate = 1;
}

void hddLaunchGame(item_list_t *itemList, int id, per_game_cfg_t *pgcfg)
{
    int i, size_irx = 0;
    int EnablePS2Logo = 0;
#ifdef CHEAT
    int result;
#endif
    void *irx = NULL;
    char filename[32];
    hdl_game_info_t *game;
    struct cdvdman_settings_hdd *settings;

    if (gAutoLaunchGame == NULL)
        game = &hddGames.games[id];
    else
        game = gAutoLaunchGame;

    apa_sub_t parts[APA_MAXSUB + 1];
    char vmc_name[2][32];
    int part_valid = 0, size_mcemu_irx = 0, nparts;
    hdd_vmc_infos_t hdd_vmc_infos;
    memset(&hdd_vmc_infos, 0, sizeof(hdd_vmc_infos_t));

    strncpy(vmc_name[0], pgcfg->vmc1, sizeof(vmc_name[0]) - 1);
    vmc_name[0][sizeof(vmc_name[0]) - 1] = '\0';

    strncpy(vmc_name[1], pgcfg->vmc2, sizeof(vmc_name[1]) - 1);
    vmc_name[1][sizeof(vmc_name[1]) - 1] = '\0';

    if (vmc_name[0][0] || vmc_name[1][0]) {
        nparts = hddGetPartitionInfo(gOPLPart, parts);
        if (nparts > 0 && nparts <= 5) {
            for (i = 0; i < nparts; i++) {
                hdd_vmc_infos.parts[i].start = parts[i].start;
                hdd_vmc_infos.parts[i].length = parts[i].length;
                LOG("HDDSUPPORT hdd_vmc_infos.parts[%d].start : 0x%X\n", i, hdd_vmc_infos.parts[i].start);
                LOG("HDDSUPPORT hdd_vmc_infos.parts[%d].length : 0x%X\n", i, hdd_vmc_infos.parts[i].length);
            }
            part_valid = 1;
        }
    }

    if (part_valid) {
        char vmc_path[256];
        int vmc_id, have_error = 0;
        vmc_superblock_t vmc_superblock;
        pfs_blockinfo_t blocks[11];

        for (vmc_id = 0; vmc_id < 2; vmc_id++) {
            if (vmc_name[vmc_id][0]) {
                have_error = 1;
                hdd_vmc_infos.active = 0;
                if (sysCheckVMC(gHDDPrefix, "/", vmc_name[vmc_id], 0, &vmc_superblock) > 0) {
                    hdd_vmc_infos.flags = vmc_superblock.mc_flag & 0xFF;
                    hdd_vmc_infos.flags |= 0x100;
                    hdd_vmc_infos.specs.page_size = vmc_superblock.page_size;
                    hdd_vmc_infos.specs.block_size = vmc_superblock.pages_per_block;
                    hdd_vmc_infos.specs.card_size = vmc_superblock.pages_per_cluster * vmc_superblock.clusters_per_card;

                    // Check vmc inode block chain (write operation can cause damage)
                    snprintf(vmc_path, sizeof(vmc_path), "%sVMC/%s.bin", gHDDPrefix, vmc_name[vmc_id]);
                    if ((nparts = hddGetFileBlockInfo(vmc_path, parts, blocks, 11)) > 0) {
                        have_error = 0;
                        hdd_vmc_infos.active = 1;
                        for (i = 0; i < nparts - 1; i++) {
                            hdd_vmc_infos.blocks[i].number = blocks[i + 1].number;
                            hdd_vmc_infos.blocks[i].subpart = blocks[i + 1].subpart;
                            hdd_vmc_infos.blocks[i].count = blocks[i + 1].count;
                            LOG("HDDSUPPORT hdd_vmc_infos.blocks[%d].number     : 0x%X\n", i, hdd_vmc_infos.blocks[i].number);
                            LOG("HDDSUPPORT hdd_vmc_infos.blocks[%d].subpart    : 0x%X\n", i, hdd_vmc_infos.blocks[i].subpart);
                            LOG("HDDSUPPORT hdd_vmc_infos.blocks[%d].count      : 0x%X\n", i, hdd_vmc_infos.blocks[i].count);
                        }
                    } else { // else VMC file is too fragmented
                        LOG("HDDSUPPORT Block Chain NG\n");
                        have_error = 2;
                    }
                }

                if (have_error) {
                    if (gAutoLaunchGame == NULL) {
                        char error[256];
                        if (have_error == 2) // VMC file is fragmented
                            snprintf(error, sizeof(error), _l(_STR_ERR_VMC_FRAGMENTED_CONTINUE), vmc_name[vmc_id], (vmc_id + 1));
                        else
                            snprintf(error, sizeof(error), _l(_STR_ERR_VMC_CONTINUE), vmc_name[vmc_id], (vmc_id + 1));
                        if (!guiMsgBox(error, 1, NULL))
                            return;
                    } else
                        LOG("VMC error\n");
                }

                for (i = 0; i < size_hdd_mcemu_irx; i++) {
                    if (((u32 *)&hdd_mcemu_irx)[i] == (0xC0DEFAC0 + vmc_id)) {
                        if (hdd_vmc_infos.active)
                            size_mcemu_irx = size_hdd_mcemu_irx;
                        memcpy(&((u32 *)&hdd_mcemu_irx)[i], &hdd_vmc_infos, sizeof(hdd_vmc_infos_t));
                        break;
                    }
                }
            }
        }
    }

    if (gRememberLastPlayed)
        wOPLLastSave(game->startup);

    int dmaType = 0, dmaMode = 7, compatMode = 0;
    compatMode = pgcfg->compat;
    dmaMode = (pgcfg->dma != 7) ? pgcfg->dma : 7;
    if (dmaMode < 3)
        dmaType = 0x20;
    else {
        dmaType = 0x40;
        dmaMode -= 3;
    }
    hddSetTransferMode(dmaType, dmaMode);
    // gHDDSpindown [0..20] -> spindown [0..240] -> seconds [0..1200]
    hddSetIdleTimeout(gHDDSpindown * 12);

    if (hddHDProKitDetected) {
        size_irx = size_hdd_hdpro_cdvdman_irx;
        irx = &hdd_hdpro_cdvdman_irx;
    } else if (hddCheckGameStar()) {
        size_irx = size_hdd_gamestar_cdvdman_irx;
        irx = &hdd_gamestar_cdvdman_irx;
    } else {
        size_irx = size_hdd_cdvdman_irx;
        irx = &hdd_cdvdman_irx;
    }

    sbPrepare(NULL, pgcfg, size_irx, irx, &i);
#ifdef CHEAT
    if ((result = sbLoadCheats(gHDDPrefix, game->startup)) < 0) {
        if (gAutoLaunchGame == NULL) {
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

    if ((result = sbLoadImage(gHDDPrefix, game->startup)) < 0) {
        if (gAutoLaunchGame == NULL) {
            guiWarning(_l(_STR_ERR_IMAGE_LOAD_FAILED), 10);
        } else {
            LOG("Image error\n");
        }
    }
#endif

    settings = (struct cdvdman_settings_hdd *)((u8 *)irx + i);

    // patch 48bit flag
    settings->common.media = hddIs48bit() & 0xff;

    // patch start_sector
    settings->lba_start = game->start_sector;

    if (pgcfg->alt_startup[0]) {
        strncpy(filename, pgcfg->alt_startup, sizeof(filename) - 1);
        filename[sizeof(filename) - 1] = '\0';
    } else {
        strncpy(filename, game->startup, sizeof(filename) - 1);
        filename[sizeof(filename) - 1] = '\0';
    }

    if (gPS2Logo)
        EnablePS2Logo = CheckPS2Logo(0, game->start_sector + OPL_HDD_MODE_PS2LOGO_OFFSET);

    int coreLoader = 0;
    int isZSO = 0;
    coreLoader = pgcfg->core_loader;

    // Check for ZSO to correctly adjust layer1 start
    settings->common.layer1_start = 0; // cdvdman will read it from APA header
    hddReadSectors(game->start_sector + OPL_HDD_MODE_PS2LOGO_OFFSET, 1, IOBuffer);
    if (*(u32 *)IOBuffer == ZSO_MAGIC) {
        probed_fd = 0;
        probed_lba = game->start_sector + OPL_HDD_MODE_PS2LOGO_OFFSET;
        ziso_init((ZISO_header *)IOBuffer, *(u32 *)((u8 *)IOBuffer + sizeof(ZISO_header)));
        ziso_read_sector(IOBuffer, 16, 1);
        u32 maxLBA = *(u32 *)(IOBuffer + 80);
        if (maxLBA > 0 && maxLBA < ziso_total_block) {   // dual layer check
            settings->common.layer1_start = maxLBA - 16; // adjust second layer start
        }
        isZSO = 1;
    }

    const char *neutrinoPath = NULL;
    if (coreLoader) {
        neutrinoPath = sbFileExists(NEUTRINO_PATH) ? NEUTRINO_PATH : (sbFileExists(NEUTRINO_ALT_PATH) ? NEUTRINO_ALT_PATH : NULL);

        if (isZSO) {
            guiWarning("Neutrino does not support this file format, launching with <OPL> core", 6);
            coreLoader = 0;
        } else if (neutrinoPath == NULL) {
            guiWarning("Neutrino ELF not found, launching with <OPL> core", 6);
            coreLoader = 0;
        }
    }

    sbMMCESendGameId(game->startup);

    if (gAutoLaunchGame == NULL)
        deinit(NO_EXCEPTION, HDD_MODE); // CAREFUL: deinit will call hddCleanUp, so hddGames/game will be freed
    else {
        miniDeinit();

        free(gAutoLaunchGame);
        gAutoLaunchGame = NULL;

        fileXioUmount("pfs0:");
        fileXioDevctl("pfs:", PDIOC_CLOSEALL, NULL, 0, NULL, 0);
    }

    if (coreLoader) {
        LOG("partition_name=[%s] name=[%s] startup=[%s]\n", game->partition_name, game->name, game->startup);
        sysLaunchNeutrino("apa", game->partition_name, compatMode, EnablePS2Logo, neutrinoPath);
        return;
    }

    settings->common.fakemodule_flags |= FAKE_MODULE_FLAG_DEV9;
    settings->common.fakemodule_flags |= FAKE_MODULE_FLAG_ATAD;

    // adjust ZSO cache
    settings->common.zso_cache = hddCacheSize;

    sysLaunchLoaderElf(filename, "HDD_MODE", size_irx, irx, size_mcemu_irx, hdd_mcemu_irx, EnablePS2Logo, compatMode);
}

static void hddGetInfo(item_list_t *itemList, int id, game_info_t *gi)
{
    hdl_game_info_t *game = &hddGames.games[id];
    char info_path[256];

    snprintf(info_path, sizeof(info_path), "%sCFG/%s.info", gHDDPrefix, game->startup);

    wOPLGameInfoLoad(info_path, gi);

    //fallback..
    if (!gi->title[0]) {
        strncpy(gi->title, game->name, sizeof(gi->title) - 1);
        gi->title[sizeof(gi->title) - 1] = '\0';
    }

    if (!gi->serial[0] && game->startup[0]) {
        char *dst = gi->serial;
        for (const char *s = game->startup; *s && (dst - gi->serial) < (int)sizeof(gi->serial) - 1; s++) {
            if (*s == '_')
                *dst++ = '-';
            else if (*s != '.')
                *dst++ = *s;
        }
        *dst = '\0';
    }
}

static void hddGetPgCfg(item_list_t *itemList, int id, per_game_cfg_t *cfg)
{
    hdl_game_info_t *game = &hddGames.games[id];
    char path[256];

    snprintf(path, sizeof(path), "%sCFG/%s.cfg", gHDDPrefix, game->startup);

    wOPLPerGameLoad(path, cfg);

    if (!cfg->format[0])
        strcpy(cfg->format, "HDL");

    if (!cfg->media[0])
        strcpy(cfg->media, game->disctype == SCECdPS2CD ? "CD" : "DVD");

    if (!cfg->size_mb)
        cfg->size_mb = game->total_size_in_kb >> 10;
}

static int hddSavePgCfg(item_list_t *itemList, int id, const per_game_cfg_t *cfg)
{
    char path[256];
    hdl_game_info_t *game = &hddGames.games[id];
    snprintf(path, sizeof(path), "%sCFG/%s.cfg", gHDDPrefix, game->startup);

    return wOPLPerGameSave(path, cfg);
}

static int hddGetImage(item_list_t *itemList, char *folder, int isRelative, char *value, char *suffix, GSTEXTURE *resultTex, short psm)
{
    char path[256];
    if (isRelative)
        snprintf(path, sizeof(path), "%s%s/%s_%s", gHDDPrefix, folder, value, suffix);
    else
        snprintf(path, sizeof(path), "%s%s_%s", folder, value, suffix);

    return texDiscoverLoad(resultTex, path, -1, 0);
}

static int hddGetArchivedImage(item_list_t *itemList, char *folder, char *value, char *suffix, GSTEXTURE *resultTex, short psm)
{
    char path[256];

    snprintf(path, sizeof(path), "%s_%s", value, suffix);

    return texDiscoverLoad(resultTex, path, -1, 1);
}

static int hddGetTextId(item_list_t *itemList)
{
    return _STR_HDD_GAMES;
}

static int hddGetIconId(item_list_t *itemList)
{
    return HDD_ICON;
}

// This may be called, even if hddInit() was not.
static void hddCleanUp(item_list_t *itemList, int exception)
{
    LOG("HDDSUPPORT CleanUp\n");

    if (hddGameList.enabled) {
        hddFreeHDLGamelist(&hddGames);

        if ((exception & UNMOUNT_EXCEPTION) == 0)
            fileXioUmount(hddPrefix);
    }

    // UI may have loaded modules outside of HDD mode, so deinitialize regardless of the enabled status.
    if (hddSupportModulesLoaded) {
        fileXioDevctl("pfs:", PDIOC_CLOSEALL, NULL, 0, NULL, 0);

        hddSupportModulesLoaded = 0;
    }
}

static int hddCheckVMC(item_list_t *itemList, char *name, int createSize)
{
    return sysCheckVMC(gHDDPrefix, "/", name, createSize, NULL);
}

// This may be called, even if hddInit() was not.
static void hddShutdown(item_list_t *itemList)
{
    LOG("HDDSUPPORT Shutdown\n");

    if (hddGameList.enabled) {
        hddFreeHDLGamelist(&hddGames);
        fileXioUmount(hddPrefix);
    }

    // UI may have loaded modules outside of HDD mode, so deinitialize regardless of the enabled status.
    if (hddSupportModulesLoaded) {
        /* Close all files */
        fileXioDevctl("pfs:", PDIOC_CLOSEALL, NULL, 0, NULL, 0);

        hddSupportModulesLoaded = 0;
    }

    if (hddModulesLoadCount > 0) {
        hddModulesLoadCount -= 1;
        if (hddModulesLoadCount == 0) {
            // DEV9 will remain active if ETH is in use, so put the HDD in IDLE state.
            // The HDD should still enter standby state after 21 minutes & 15 seconds, as per the ATAD defaults.
            hddSetIdleImmediate();
        }

        // Only shut down dev9 from here, if it was initialized from here before.
        sysShutdownDev9();
    }
}

static int hddLoadGameListCache(hdl_games_list_t *cache)
{
    char filename[256];
    FILE *file;
    hdl_game_info_t *games;
    int result, size, count;

    if (!gHDDGameListCache)
        return 1;

    hddFreeHDLGamelist(cache);

    sprintf(filename, "%sgames.bin", gHDDPrefix);
    file = fopen(filename, "rb");
    if (file != NULL) {
        fseek(file, 0, SEEK_END);
        size = ftell(file);
        rewind(file);

        count = size / sizeof(hdl_game_info_t);
        if (count > 0) {
            games = memalign(64, count * sizeof(hdl_game_info_t));
            if (games != NULL) {
                if (fread(games, sizeof(hdl_game_info_t), count, file) == count) {
                    cache->count = count;
                    cache->games = games;
                    LOG("hddLoadGameListCache: %d games loaded.\n", count);
                    result = 0;
                } else {
                    LOG("hddLoadGameListCache: I/O error.\n");
                    free(games);
                    result = EIO;
                }
            } else {
                LOG("hddLoadGameListCache: failed to allocate memory.\n");
                result = ENOMEM;
            }
        } else {
            result = -1; // Empty file
        }

        fclose(file);
    } else {
        result = ENOENT;
    }

    return result;
}

static int hddUpdateGameListCache(hdl_games_list_t *cache, hdl_games_list_t *game_list)
{
    char filename[256];
    FILE *file;
    int result, i, j, modified;

    if (!gHDDGameListCache)
        return 1;

    if (cache->count > 0) {
        modified = 0;
        for (i = 0; i < cache->count; i++) {
            for (j = 0; j < game_list->count; j++) {
                if (strncmp(cache->games[i].partition_name, game_list->games[j].partition_name, APA_IDMAX + 1) == 0)
                    break;
            }

            if (j == game_list->count) {
                LOG("hddUpdateGameListCache: game added.\n");
                modified = 1;
                break;
            }
        }

        if ((!modified) && (game_list->count != cache->count)) {
            LOG("hddUpdateGameListCache: game removed.\n");
            modified = 1;
        }
    } else {
        modified = (game_list->count > 0) ? 1 : 0;
    }

    if (!modified)
        return 0;
    LOG("hddUpdateGameListCache: caching new game list.\n");

    sprintf(filename, "%sgames.bin", gHDDPrefix);
    if (game_list->count > 0) {
        file = fopen(filename, "wb");
        if (file != NULL) {
            result = (fwrite(game_list->games, sizeof(hdl_game_info_t), game_list->count, file) == game_list->count) ? 0 : EIO;
            fclose(file);
        } else {
            result = EIO;
        }
    } else {
        // Last game deleted.
        remove(filename);
        result = 0;
    }

    return result;
}

static char *hddGetPrefix(item_list_t *itemList)
{
    return gHDDPrefix;
}

static item_list_t hddGameList = {
    HDD_MODE, 0, 0, MODE_FLAG_COMPAT_DMA, MENU_MIN_INACTIVE_FRAMES, HDD_MODE_UPDATE_DELAY, NULL, NULL, &hddGetTextId, &hddGetPrefix, &hddInit, &hddNeedsUpdate, &hddUpdateGameList,
    &hddGetGameCount, &hddGetGame, &hddGetGameName, &hddGetGameNameLength, &hddGetGameStartup, &hddDeleteGame, &hddRenameGame,
    &hddLaunchGame, &hddGetInfo, &hddGetPgCfg, &hddSavePgCfg, &hddGetImage, &hddGetArchivedImage, &hddCleanUp, &hddShutdown, &hddCheckVMC, &hddGetIconId};

int hddIsPresent()
{
    // the only thing that currently uses ata_device_identify is ATA_DEVCTL_GET_HIGHEST_UDMA_MODE, so this is the best method to check for presence via xhdd (for now anyways)
    // ideally, we'd only have ata_device_identify
    return fileXioDevctl("xhdd0:", ATA_DEVCTL_GET_HIGHEST_UDMA_MODE, NULL, 0, NULL, 0) >= 0;
}

void autoLaunchHDDGame(char *argv[])
{
    char path[256];

    miniInit(HDD_MODE);

    gAutoLaunchGame = malloc(sizeof(hdl_game_info_t));
    memset(gAutoLaunchGame, 0, sizeof(hdl_game_info_t));

    snprintf(gAutoLaunchGame->startup, sizeof(gAutoLaunchGame->startup), argv[1]);
    gAutoLaunchGame->start_sector = strtoul(argv[2], NULL, 0);
    snprintf(gOPLPart, sizeof(gOPLPart), "hdd0:%s", argv[3]);

    per_game_cfg_t pgcfg;
    snprintf(path, sizeof(path), "%sCFG/%s.cfg", gHDDPrefix, gAutoLaunchGame->startup);
    wOPLPerGameLoad(path, &pgcfg);

    hddLaunchGame(NULL, -1, &pgcfg);
}