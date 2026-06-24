#include "include/common.h"
#include "include/lang.h"
#include "include/util.h"
#include "include/iosupport.h"
#include "include/system.h"
#include "include/supportbase.h"
#include "include/ioman.h"
#include "modules/iopcore/common/cdvd_config.h"
#ifdef CHEAT
#include "include/cheatman.h"
#endif
#ifdef GSM
#include "include/pggsm.h"
#endif
#include "include/ps2cnf.h"
#include "include/gui.h"
#include "include/guigame.h"
#include "include/bdmsupport.h"
#include "include/hddsupport.h"
#include "include/mmcesupport.h"
#include "include/tar.h"
#include "include/config_wopl.h"

#define NEWLIB_PORT_AWARE
#include <fileXio_rpc.h> // fileXioMount("iso:", ***), fileXioUmount("iso:")
#include <io_common.h>   // FIO_MT_RDONLY
#include <ps2sdkapi.h>   // lseek64
#include <malloc.h>
#include <kernel.h>
#include <libcdvd-common.h>

#include "../modules/isofs/zso.h"

extern int probed_fd;
extern u32 probed_lba;

extern void *icon_sys;
extern int size_icon_sys;
extern void *icon_icn;
extern int size_icon_icn;
extern void *icon_cpy_icn;
extern int size_icon_cpy_icn;
extern void *icon_del_icn;
extern int size_icon_del_icn;

/// internal linked list used to populate the list from directory listing
struct game_list_t
{
    base_game_info_t gameinfo;
    char filename[128];
    struct game_list_t *next;
};

struct game_cache_list
{
    unsigned int count;
    base_game_info_t *games;
};

static int mcID = -1;

#ifdef PADEMU
int gEnablePadEmu;
int gPadEmuSettings;
int gPadMacroSource;
int gPadMacroSettings;
int gPadEmuSource;
#endif

int sbGetmcID(void)
{
    return mcID;
}

int sbGetFileSize(int fd)
{
    int size = lseek(fd, 0, SEEK_END);
    lseek(fd, 0, SEEK_SET);
    return size;
}

static int checkMC()
{
    int mc0_is_ps2card, mc1_is_ps2card;
    int mc0_has_folder, mc1_has_folder;

    if (mcID == -1) {
        mc0_is_ps2card = 0;
        DIR *mc0_root_dir = opendir("mc0:/");
        if (mc0_root_dir != NULL) {
            closedir(mc0_root_dir);
            mc0_is_ps2card = 1;
        }

        mc1_is_ps2card = 0;
        DIR *mc1_root_dir = opendir("mc1:/");
        if (mc1_root_dir != NULL) {
            closedir(mc1_root_dir);
            mc1_is_ps2card = 1;
        }

        char mc0_dir[64];
        snprintf(mc0_dir, sizeof(mc0_dir), "mc0:%s/", WOPL_CONFIG_NAME);
        mc0_has_folder = 0;
        DIR *mc0_opl_dir = opendir(mc0_dir);
        if (mc0_opl_dir != NULL) {
            closedir(mc0_opl_dir);
            mc0_has_folder = 1;
        }

        char mc1_dir[64];
        snprintf(mc1_dir, sizeof(mc1_dir), "mc1:%s/", WOPL_CONFIG_NAME);
        mc1_has_folder = 0;
        DIR *mc1_opl_dir = opendir(mc1_dir);
        if (mc1_opl_dir != NULL) {
            closedir(mc1_opl_dir);
            mc1_has_folder = 1;
        }

        if (mc0_has_folder) {
            mcID = '0';
            return mcID;
        }

        if (mc1_has_folder) {
            mcID = '1';
            return mcID;
        }

        if (mc0_is_ps2card) {
            mcID = '0';
            return mcID;
        }

        if (mc1_is_ps2card) {
            mcID = '1';
            return mcID;
        }
    }
    return mcID;
}

void sbCheckMCFolder(void)
{
    char path[32];
    int fd;

    if (checkMC() < 0) {
        return;
    }

    snprintf(path, sizeof(path), "mc%d:%s/", mcID & 1, WOPL_CONFIG_NAME);
    mkdir(path, 0777);

    snprintf(path, sizeof(path), "mc%d:%s/list.icn", mcID & 1, WOPL_CONFIG_NAME);
    fd = open(path, O_RDONLY);
    if (fd < 0) {
        fd = sbOpenFile(path, O_WRONLY | O_CREAT | O_TRUNC);
        if (fd >= 0) {
            write(fd, &icon_icn, size_icon_icn);
            close(fd);
        }
    } else
        close(fd);

    snprintf(path, sizeof(path), "mc%d:%s/copy.icn", mcID & 1, WOPL_CONFIG_NAME);
    fd = open(path, O_RDONLY);
    if (fd < 0) {
        fd = sbOpenFile(path, O_WRONLY | O_CREAT | O_TRUNC);
        if (fd >= 0) {
            write(fd, &icon_cpy_icn, size_icon_cpy_icn);
            close(fd);
        }
    } else
        close(fd);

    snprintf(path, sizeof(path), "mc%d:%s/del.icn", mcID & 1, WOPL_CONFIG_NAME);
    fd = open(path, O_RDONLY);
    if (fd < 0) {
        fd = sbOpenFile(path, O_WRONLY | O_CREAT | O_TRUNC);
        if (fd >= 0) {
            write(fd, &icon_del_icn, size_icon_del_icn);
            close(fd);
        }
    } else
        close(fd);

    snprintf(path, sizeof(path), "mc%d:%s/icon.sys", mcID & 1, WOPL_CONFIG_NAME);
    fd = open(path, O_RDONLY);
    if (fd < 0) {
        fd = sbOpenFile(path, O_WRONLY | O_CREAT | O_TRUNC);
        if (fd >= 0) {
            write(fd, &icon_sys, size_icon_sys);
            close(fd);
        }
    } else {
        close(fd);
    }
}

static int checkFile(char *path, int mode)
{
    // check if it is mc
    if (strncmp(path, "mc", 2) == 0) {

        // if user didn't explicitly asked for a MC (using '?' char)
        if (path[2] == 0x3F) {

            // Use default detected card
            if (checkMC() >= 0)
                path[2] = mcID;
            else
                return 0;
        }

        // in create mode, we check that the directory exist, or create it
        if (mode & O_CREAT) {
            char dirPath[256];
            char *pos = strrchr(path, '/');
            if (pos) {
                memcpy(dirPath, path, (pos - path));
                dirPath[(pos - path)] = '\0';
                DIR *dir = opendir(dirPath);
                if (dir == NULL) {
                    int res = mkdir(dirPath, 0777);
                    if (res != 0)
                        return 0;
                } else
                    closedir(dir);
            }
        }
    }
    return 1;
}


int sbIsSameSize(const char *prefix, int prevSize)
{
    int size = -1;
    char path[256];
    snprintf(path, sizeof(path), "%sul.cfg", prefix);

    int fd = sbOpenFile(path, O_RDONLY);
    if (fd >= 0) {
        size = sbGetFileSize(fd);
        close(fd);
    }

    return size == prevSize;
}

int sbFileExists(const char *path)
{
    int fd = open(path, O_RDONLY);
    if (fd < 0)
        return 0;

    close(fd);
    return 1;
}

int sbCreateSemaphore(void)
{
    ee_sema_t sema;

    sema.option = sema.attr = 0;
    sema.init_count = 1;
    sema.max_count = 1;
    return CreateSema(&sema);
}

// 0 = Not ISO disc image, GAME_FORMAT_OLD_ISO = legacy ISO disc image (filename follows old naming requirement), GAME_FORMAT_ISO = plain ISO image.
int isValidIsoName(char *name, int *pNameLen)
{
    // Old ISO image naming format: SCUS_XXX.XX.ABCDEFGHIJKLMNOP.iso

    // Minimum is 17 char, GameID (11) + "." (1) + filename (1 min.) + ".iso" (4)
    int size = strlen(name);
    if (strcasecmp(&name[size - 4], ".iso") == 0 || strcasecmp(&name[size - 4], ".zso") == 0) {
        if ((size >= 17) && (name[4] == '_') && (name[8] == '.') && (name[11] == '.')) {
            *pNameLen = size - 16;
            return GAME_FORMAT_OLD_ISO;
        } else if (size >= 5) {
            *pNameLen = size - 4;
            return GAME_FORMAT_ISO;
        }
    }

    return 0;
}

int sbOpenFile(char *path, int mode)
{
    if (checkFile(path, mode))
        return open(path, mode, 0666);
    else
        return -1;
}

void *sbReadFile(char *path, int align, int *size)
{
    void *buffer = NULL;

    int fd = sbOpenFile(path, O_RDONLY);
    if (fd >= 0) {
        unsigned int realSize = sbGetFileSize(fd);

        if ((*size > 0) && (*size != realSize)) {
            LOG("UTIL Invalid filesize, expected: %d, got: %d\n", *size, realSize);
            close(fd);
            return NULL;
        }

        if (align > 0)
            buffer = memalign(64, realSize); // The allocation is aligned to aid the DMA transfers
        else
            buffer = malloc(realSize);

        if (!buffer) {
            LOG("UTIL ReadFile: Failed allocation of %d bytes", realSize);
            *size = 0;
        } else {
            read(fd, buffer, realSize);
            close(fd);
            *size = realSize;
        }
    }
    return buffer;
}

int sbListDir(char *path, const char *separator, int maxElem,
              int (*readEntry)(int index, const char *path, const char *separator, const char *name, unsigned char d_type))
{
    int index = 0;
    char filename[128];

    if (checkFile(path, O_RDONLY)) {
        DIR *dir = opendir(path);
        struct dirent *dirent;
        if (dir != NULL) {
            while (index < maxElem && (dirent = readdir(dir)) != NULL) {
                snprintf(filename, 128, "%s/%s", path, dirent->d_name);
                index = readEntry(index, path, separator, dirent->d_name, dirent->d_type);
            }

            closedir(dir);
        }
    }
    return index;
}

/* size will be the maximum line size possible */
file_buffer_t *sbOpenFileBuffer(char *fpath, int mode, short allocResult, unsigned int size)
{
    file_buffer_t *fileBuffer = NULL;
    unsigned char bom[3];

    int fd = sbOpenFile(fpath, mode);
    if (fd >= 0) {
        fileBuffer = (file_buffer_t *)malloc(sizeof(file_buffer_t));
        fileBuffer->size = size;
        fileBuffer->available = 0;
        fileBuffer->buffer = (char *)malloc(size * sizeof(char));
        if (mode == O_RDONLY) {
            fileBuffer->lastPtr = NULL;

            // Check for and skip the UTF-8 BOM sequence.
            if ((read(fd, bom, sizeof(bom)) != 3) ||
                (bom[0] != 0xEF || bom[1] != 0xBB || bom[2] != 0xBF)) {
                // Not BOM, so rewind.
                lseek(fd, 0, SEEK_SET);
            }
        } else
            fileBuffer->lastPtr = fileBuffer->buffer;
        fileBuffer->allocResult = allocResult;
        fileBuffer->fd = fd;
        fileBuffer->mode = mode;
    }

    return fileBuffer;
}

/* size will be the maximum line size possible */
file_buffer_t *sbOpenFileBufferBuffer(short allocResult, const void *buffer, unsigned int size)
{
    file_buffer_t *fileBuffer = NULL;

    fileBuffer = (file_buffer_t *)malloc(sizeof(file_buffer_t));
    fileBuffer->size = size;
    fileBuffer->available = size;
    fileBuffer->buffer = (char *)malloc((size + 1) * sizeof(char));
    fileBuffer->lastPtr = fileBuffer->buffer; // O_RDONLY, but with the data in the buffer.
    fileBuffer->allocResult = allocResult;
    fileBuffer->fd = -1;
    fileBuffer->mode = O_RDONLY;

    memcpy(fileBuffer->buffer, buffer, size);
    fileBuffer->buffer[size] = '\0';

    return fileBuffer;
}

int sbReadFileBuffer(file_buffer_t *fileBuffer, char **outBuf)
{
    int lineSize = 0, readSize, length;
    char *posLF = NULL;

    while (1) {
        // if lastPtr is set, then we continue the read from this point as reference
        if (fileBuffer->lastPtr) {
            // Calculate the remaining chars to the right of lastPtr
            lineSize = fileBuffer->available - (fileBuffer->lastPtr - fileBuffer->buffer);
            /* LOG("##### Continue read, position: %X (total: %d) line size (\\0 not inc.): %d end: %x\n",
                fileBuffer->lastPtr - fileBuffer->buffer, fileBuffer->available, lineSize, fileBuffer->lastPtr[lineSize]); */
            posLF = strchr(fileBuffer->lastPtr, '\n');
        }

        if (!posLF) { // We can come here either when the buffer is empty, or if the remaining chars don't have a LF

            // if available, we shift the remaining chars to the left ...
            if (lineSize) {
                // LOG("##### LF not found, Shift %d characters from end to beginning\n", lineSize);
                memmove(fileBuffer->buffer, fileBuffer->lastPtr, lineSize);
            }

            // ... and complete the buffer if we're not at EOF
            if (fileBuffer->fd >= 0) {

                // Load as many characters necessary to fill the buffer
                length = fileBuffer->size - lineSize - 1;
                // LOG("##### Asking for %d characters to complete buffer\n", length);
                readSize = read(fileBuffer->fd, fileBuffer->buffer + lineSize, length);
                fileBuffer->buffer[lineSize + readSize] = '\0';

                // Search again (from the lastly added chars only), the result will be "analyzed" in next if
                posLF = strchr(fileBuffer->buffer + lineSize, '\n');

                // Now update read context info
                lineSize = lineSize + readSize;
                // LOG("##### %d characters really read, line size now (\\0 not inc.): %d\n", read, lineSize);

                // If buffer not full it means we are at EOF
                if (fileBuffer->size != lineSize + 1) {
                    // LOG("##### Reached EOF\n");
                    close(fileBuffer->fd);
                    fileBuffer->fd = -1;
                }
            }

            fileBuffer->lastPtr = fileBuffer->buffer;
            fileBuffer->available = lineSize;
        }

        if (posLF)
            lineSize = posLF - fileBuffer->lastPtr;

        // Check the previous char (on Windows there are CR/LF instead of single linux LF)
        if (lineSize)
            if (*(fileBuffer->lastPtr + lineSize - 1) == '\r')
                lineSize--;

        fileBuffer->lastPtr[lineSize] = '\0';
        *outBuf = fileBuffer->lastPtr;

        // LOG("##### Result line is \"%s\" size: %d avail: %d pos: %d\n", fileBuffer->lastPtr, lineSize, fileBuffer->available, fileBuffer->lastPtr - fileBuffer->buffer);

        // If we are at EOF and no more chars available to scan, then we are finished
        if (!lineSize && !fileBuffer->available && fileBuffer->fd == -1)
            return 0;

        if (fileBuffer->lastPtr[0] == '#') { // '#' for comment lines
            if (posLF)
                fileBuffer->lastPtr = posLF + 1;
            else
                fileBuffer->lastPtr = NULL;
            continue;
        }

        if (lineSize && fileBuffer->allocResult) {
            *outBuf = (char *)malloc((lineSize + 1) * sizeof(char));
            memcpy(*outBuf, fileBuffer->lastPtr, lineSize + 1);
        }

        // Either move the pointer to next chars, or set it to null to force a whole buffer read (if possible)
        if (posLF)
            fileBuffer->lastPtr = posLF + 1;
        else {
            fileBuffer->lastPtr = NULL;
        }

        return 1;
    }
}

void sbWriteFileBuffer(file_buffer_t *fileBuffer, char *inBuf, int size)
{
    // LOG("writeFileBuffer avail: %d size: %d\n", fileBuffer->available, size);
    if (fileBuffer->available && fileBuffer->available + size > fileBuffer->size) {
        // LOG("writeFileBuffer flushing: %d\n", fileBuffer->available);
        write(fileBuffer->fd, fileBuffer->buffer, fileBuffer->available);
        fileBuffer->lastPtr = fileBuffer->buffer;
        fileBuffer->available = 0;
    }

    if (size > fileBuffer->size) {
        // LOG("writeFileBuffer direct write: %d\n", size);
        write(fileBuffer->fd, inBuf, size);
    } else {
        memcpy(fileBuffer->lastPtr, inBuf, size);
        fileBuffer->lastPtr += size;
        fileBuffer->available += size;

        // LOG("writeFileBuffer lastPrt: %d\n", (fileBuffer->lastPtr - fileBuffer->buffer));
    }
}

void sbCloseFileBuffer(file_buffer_t *fileBuffer)
{
    if (fileBuffer->fd >= 0) {
        if (fileBuffer->mode != O_RDONLY && fileBuffer->available) {
            // LOG("writeFileBuffer final write: %d\n", fileBuffer->available);
            write(fileBuffer->fd, fileBuffer->buffer, fileBuffer->available);
        }
        close(fileBuffer->fd);
    }
    free(fileBuffer->buffer);
    free(fileBuffer);
}

void sbMMCESendGameId(const char *gameId)
{
    mmceSendGameId(gameId);
}

static int GetStartupExecName(const char *path, char *filename, int maxlength)
{
    char ps2disc_boot[CNF_PATH_LEN_MAX] = "";
    const char *key;
    int ret;

    if ((ret = ps2cnfGetBootFile(path, ps2disc_boot)) == 0) {
        int length = 0;
        const char *start;

        /* Skip the device name part of the path ("cdrom0:\"). */
        key = ps2disc_boot;

        for (; *key != ':'; key++) {
            if (*key == '\0') {
                LOG("GetStartupExecName: missing ':' (%s).\n", ps2disc_boot);
                return -1;
            }
        }

        ++key;
        while (*key == '\\') {
            key++;
        }

        start = key;

        while ((*key != ';') && (*key != '\0')) {
            length++;
            key++;
        }

        if (length > maxlength) {
            length = maxlength;
        }

        if (length == 0) {
            LOG("GetStartupExecName: serial len 0 ':' (%s).\n", ps2disc_boot);
            return -1;
        }

        strncpy(filename, start, length);
        filename[length] = '\0';
        LOG("GetStartupExecName: serial len %d %s \n", length, filename);

        return 0;
    } else {
        LOG("GetStartupExecName: Could not get BOOT2 parameter.\n");
        return ret;
    }
}

static void freeISOGameListCache(struct game_cache_list *cache);

static int loadISOGameListCache(const char *path, struct game_cache_list *cache)
{
    char filename[256];
    FILE *file;
    base_game_info_t *games;
    int result, size, count;

    freeISOGameListCache(cache);

    sprintf(filename, "%s/games.bin", path);
    file = fopen(filename, "rb");
    if (file != NULL) {
        fseek(file, 0, SEEK_END);
        size = ftell(file);
        rewind(file);

        count = size / sizeof(base_game_info_t);
        if (count > 0) {
            games = memalign(64, count * sizeof(base_game_info_t));
            if (games != NULL) {
                if (fread(games, sizeof(base_game_info_t), count, file) == count) {
                    LOG("loadISOGameListCache: %d games loaded.\n", count);
                    cache->count = count;
                    cache->games = games;
                    result = 0;
                } else {
                    LOG("loadISOGameListCache: I/O error.\n");
                    free(games);
                    result = EIO;
                }
            } else {
                LOG("loadISOGameListCache: failed to allocate memory.\n");
                result = ENOMEM;
            }
        } else {
            result = -1; // Empty file (should not happen)
        }

        fclose(file);
    } else {
        result = ENOENT;
    }

    return result;
}

static void freeISOGameListCache(struct game_cache_list *cache)
{
    if (cache->games != NULL) {
        free(cache->games);
        cache->games = NULL;
        cache->count = 0;
    }
}

static int updateISOGameList(const char *path, const struct game_cache_list *cache, const struct game_list_t *head, int count)
{
    char filename[256];
    FILE *file;
    const struct game_list_t *game;
    int result, i, j, modified;
    base_game_info_t *list;

    modified = 0;
    if (cache != NULL) {
        if ((head != NULL) && (count > 0)) {
            game = head;

            for (i = 0; i < count; i++) {
                for (j = 0; j < cache->count; j++) {
                    if (strncmp(cache->games[i].name, game->gameinfo.name, ISO_GAME_NAME_MAX + 1) == 0 && strncmp(cache->games[i].extension, game->gameinfo.extension, ISO_GAME_EXTENSION_MAX + 1) == 0)
                        break;
                }

                if (j == cache->count) {
                    LOG("updateISOGameList: game added.\n");
                    modified = 1;
                    break;
                }

                game = game->next;
            }

            if ((!modified) && (count != cache->count)) {
                LOG("updateISOGameList: game removed.\n");
                modified = 1;
            }
        } else {
            modified = 0;
        }
    } else {
        modified = ((head != NULL) && (count > 0)) ? 1 : 0;
    }

    if (!modified)
        return 0;
    LOG("updateISOGameList: caching new game list.\n");

    result = 0;
    sprintf(filename, "%s/games.bin", path);
    if ((head != NULL) && (count > 0)) {
        list = (base_game_info_t *)memalign(64, sizeof(base_game_info_t) * count);

        if (list != NULL) {
            // Convert the linked list into a flat array, for writing performance.
            game = head;
            for (i = 0; (i < count) && (game != NULL); i++, game = game->next) {
                // copy one game, advance
                memcpy(&list[i], &game->gameinfo, sizeof(base_game_info_t));
            }

            file = fopen(filename, "wb");
            if (file != NULL) {
                result = fwrite(list, sizeof(base_game_info_t), count, file) == count ? 0 : EIO;

                fclose(file);

                if (result != 0)
                    remove(filename);
            } else
                result = EIO;

            free(list);
        } else
            result = ENOMEM;
    } else {
        // Last game deleted.
        remove(filename);
    }

    return result;
}

// Queries for the game entry, based on filename. Only the new filename format is supported (filename.ext).
static int queryISOGameListCache(const struct game_cache_list *cache, base_game_info_t *ginfo, const char *filename)
{
    char isoname[ISO_GAME_FNAME_MAX + 1];
    int i;

    for (i = 0; i < cache->count; i++) {
        snprintf(isoname, sizeof(isoname), "%s%s", cache->games[i].name, cache->games[i].extension);

        if (strcmp(filename, isoname) == 0) {
            memcpy(ginfo, &cache->games[i], sizeof(base_game_info_t));
            return 0;
        }
    }

    return ENOENT;
}

static void applyISOSizes(char *path, struct game_list_t *glist)
{
    iox_dirent_t dirent;

    int fd = fileXioDopen(path);
    if (fd < 0)
        return;

    while (fileXioDread(fd, &dirent) > 0) {
        if (dirent.name[0] == '\0')
            continue;

        u32 sizeMB = (((u64)dirent.stat.hisize << 32) | dirent.stat.size) >> 20;
        struct game_list_t *g = glist;
        while (g) {
            if (g->gameinfo.format == GAME_FORMAT_USBLD) {
                const char *fname = dirent.name + 12;

                if (!strncmp(fname, g->gameinfo.startup, strlen(g->gameinfo.startup))) {
                    g->gameinfo.sizeMB += sizeMB;
                    break;
                }
            } else {
                if (strcmp(g->filename, dirent.name) == 0) {
                    g->gameinfo.sizeMB = sizeMB;
                    break;
                }
            }
            g = g->next;
        }
    }

    fileXioDclose(fd);
}

static int scanForISO(char *path, char type, struct game_list_t **glist)
{
    int count = 0;
    struct game_cache_list cache = {0, NULL};
    base_game_info_t cachedGInfo;
    char fullpath[256];
    struct dirent *dirent;
    DIR *dir;

    int cacheLoaded = loadISOGameListCache(path, &cache) == 0;

    if ((dir = opendir(path)) != NULL) {
        size_t base_path_len = strlen(path);
        strcpy(fullpath, path);
        fullpath[base_path_len] = '/';

        while ((dirent = readdir(dir)) != NULL) {
            int NameLen;
            int format = isValidIsoName(dirent->d_name, &NameLen);

            if (format <= 0 || NameLen > ISO_GAME_NAME_MAX)
                continue; // Skip files that cannot be supported properly.

            strcpy(fullpath + base_path_len + 1, dirent->d_name);

            struct game_list_t *next = malloc(sizeof(struct game_list_t));
            if (!next)
                break; // Out of memory

            next->next = *glist;
            *glist = next;
            base_game_info_t *game = &next->gameinfo;
            strncpy(next->filename, dirent->d_name, sizeof(next->filename) - 1);
            next->filename[sizeof(next->filename) - 1] = '\0';
            memset(game, 0, sizeof(base_game_info_t));

            if (format == GAME_FORMAT_OLD_ISO) {
                // old iso format can't be cached
                strncpy(game->name, &dirent->d_name[GAME_STARTUP_MAX], NameLen);
                game->name[NameLen] = '\0';
                strncpy(game->startup, dirent->d_name, GAME_STARTUP_MAX - 1);
                game->startup[GAME_STARTUP_MAX - 1] = '\0';
                strncpy(game->extension, &dirent->d_name[GAME_STARTUP_MAX + NameLen], sizeof(game->extension) - 1);
                game->extension[sizeof(game->extension) - 1] = '\0';
            } else if (cacheLoaded && queryISOGameListCache(&cache, &cachedGInfo, dirent->d_name) == 0) {
                // use cached entry
                memcpy(game, &cachedGInfo, sizeof(base_game_info_t));
            } else {
                // need to mount and read SYSTEM.CNF
                char startup[GAME_STARTUP_MAX];
                int MountFD = fileXioMount("iso:", fullpath, FIO_MT_RDONLY);

                if (MountFD < 0 || GetStartupExecName("iso:/SYSTEM.CNF;1", startup, GAME_STARTUP_MAX - 1) != 0) {
                    fileXioUmount("iso:");
                    *glist = next->next;
                    free(next);
                    continue;
                }

                strcpy(game->startup, startup);
                strncpy(game->name, dirent->d_name, NameLen);
                game->name[NameLen] = '\0';
                strncpy(game->extension, &dirent->d_name[NameLen], sizeof(game->extension) - 1);
                game->extension[sizeof(game->extension) - 1] = '\0';

                fileXioUmount("iso:");
            }

            game->parts = 1;
            game->media = type;
            game->format = format;
            game->sizeMB = 0;

            count++;
        }
        closedir(dir);
    }

    if (cacheLoaded) {
        updateISOGameList(path, &cache, *glist, count);
        freeISOGameListCache(&cache);
    } else {
        updateISOGameList(path, NULL, *glist, count);
    }

    applyISOSizes(path, *glist);

    return count;
}

int sbReadList(base_game_info_t **list, const char *prefix, int *fsize, int *gamecount)
{
    int fd, size, id = 0, result;
    int count;
    char path[256];

    free(*list);
    *list = NULL;
    *fsize = -1;
    *gamecount = 0;

    // temporary storage for the game names
    struct game_list_t *dlist_head = NULL;

    // count iso games in "cd" directory
    guiSetBootStatusIfActive("Scanning CD images...");
    snprintf(path, sizeof(path), "%sCD", prefix);
    count = scanForISO(path, SCECdPS2CD, &dlist_head);

    // count iso games in "dvd" directory
    guiSetBootStatusIfActive("Scanning DVD images...");
    snprintf(path, sizeof(path), "%sDVD", prefix);
    if ((result = scanForISO(path, SCECdPS2DVD, &dlist_head)) >= 0) {
        count = count < 0 ? result : count + result;
    }

    // count and process games in ul.cfg
    guiSetBootStatusIfActive("Checking USBExtreme games...");
    snprintf(path, sizeof(path), "%sul.cfg", prefix);
    fd = sbOpenFile(path, O_RDONLY);
    if (fd >= 0) {
        USBExtreme_game_entry_t GameEntry;

        if (count < 0)
            count = 0;
        size = sbGetFileSize(fd);
        *fsize = size;
        count += size / sizeof(USBExtreme_game_entry_t);

        if (count > 0) {
            if ((*list = (base_game_info_t *)malloc(sizeof(base_game_info_t) * count)) != NULL) {
                memset(*list, 0, sizeof(base_game_info_t) * count);

                while (size > 0) {
                    base_game_info_t *g = &(*list)[id++];

                    // populate game entry in list even if entry corrupted
                    read(fd, &GameEntry, sizeof(USBExtreme_game_entry_t));
                    size -= sizeof(USBExtreme_game_entry_t);

                    // to ensure no leaks happen, we copy manually and pad the strings
                    memcpy(g->name, GameEntry.name, UL_GAME_NAME_MAX);
                    g->name[UL_GAME_NAME_MAX] = '\0';
                    memcpy(g->startup, GameEntry.startup, GAME_STARTUP_MAX);
                    g->startup[GAME_STARTUP_MAX] = '\0';
                    g->extension[0] = '\0';
                    g->parts = GameEntry.parts;
                    g->media = GameEntry.media;
                    g->format = GAME_FORMAT_USBLD;
                    g->sizeMB = 0;
                }
            }
        }
        close(fd);
    } else if (count > 0) {
        *list = (base_game_info_t *)malloc(sizeof(base_game_info_t) * count);
    }

    if (*list != NULL) {
        // copy the dlist into the list
        while ((id < count) && dlist_head) {
            // copy one game, advance
            struct game_list_t *cur = dlist_head;
            dlist_head = dlist_head->next;

            memcpy(&(*list)[id++], &cur->gameinfo, sizeof(base_game_info_t));
            free(cur);
        }
    } else
        count = 0;

    if (count > 0)
        *gamecount = count;

    return count;
}

extern int probed_fd;
extern u32 probed_lba;
extern u8 IOBuffer[2048] ALIGNED(64); // one sector

static int ProbeZISO(int fd)
{
    struct
    {
        ZISO_header header;
        u32 first_block;
    } ziso_data;
    lseek(fd, 0, SEEK_SET);
    if (read(fd, &ziso_data, sizeof(ziso_data)) == sizeof(ziso_data) && ziso_data.header.magic == ZSO_MAGIC) {
        // initialize ZSO
        ziso_init(&ziso_data.header, ziso_data.first_block);
        // set ISO file descriptor for ZSO reader
        probed_fd = fd;
        probed_lba = 0;
        return 1;
    } else {
        return 0;
    }
}

u32 sbGetISO9660MaxLBA(const char *path)
{
    u32 maxLBA;
    int file;

    if ((file = open(path, O_RDONLY, 0666)) >= 0) {
        if (ProbeZISO(file)) {
            if (ziso_read_sector(IOBuffer, 16, 1) == 1) {
                maxLBA = *(u32 *)(IOBuffer + 80);
            } else {
                maxLBA = 0;
            }
        } else {
            lseek(file, 16 * 2048 + 80, SEEK_SET);
            if (read(file, &maxLBA, sizeof(maxLBA)) != sizeof(maxLBA))
                maxLBA = 0;
        }
        close(file);
    } else {
        maxLBA = 0;
    }

    return maxLBA;
}

int sbProbeISO9660(const char *path, base_game_info_t *game, u32 layer1_offset)
{
    int result = -1, fd;
    char buffer[6];

    result = -1;
    if (game->media == SCECdPS2DVD) { // Only DVDs can have multiple layers.
        if ((fd = open(path, O_RDONLY, 0666)) >= 0) {
            if (ProbeZISO(fd)) {
                if (ziso_read_sector(IOBuffer, layer1_offset, 1) == 1 &&
                    ((IOBuffer[0x00] == 1) && (!strncmp((char *)(&IOBuffer[0x01]), "CD001", 5)))) {
                    result = 0;
                }
            } else {
                if (lseek64(fd, (u64)layer1_offset * 2048, SEEK_SET) == (u64)layer1_offset * 2048) {
                    if ((read(fd, buffer, sizeof(buffer)) == sizeof(buffer)) &&
                        ((buffer[0x00] == 1) && (!strncmp(&buffer[0x01], "CD001", 5)))) {
                        result = 0;
                    }
                }
            }
            close(fd);
        } else
            result = fd;
    }

    return result;
}

static const struct cdvdman_settings_common cdvdman_settings_common_sample = CDVDMAN_SETTINGS_DEFAULT_COMMON;

int sbPrepare(base_game_info_t *game, const per_game_cfg_t *pgcfg, int size_cdvdman, void **cdvdman_irx, int *patchindex)
{
    int i;
    struct cdvdman_settings_common *settings;

    int compatmask = pgcfg ? pgcfg->compat : 0;

    char gameid[5];
    dnas_to_binary(pgcfg ? pgcfg->dnas : NULL, gameid, sizeof(gameid));

    for (i = 0, settings = NULL; i < size_cdvdman; i += 4) {
        if (!memcmp((void *)((u8 *)cdvdman_irx + i), &cdvdman_settings_common_sample, sizeof(cdvdman_settings_common_sample))) {
            settings = (struct cdvdman_settings_common *)((u8 *)cdvdman_irx + i);
            break;
        }
    }
    if (settings == NULL) {
        LOG("sbPrepare: unable to locate patch zone.\n");
        return -1;
    }

    if (game != NULL) {
        settings->NumParts = game->parts;
        settings->media = game->media;
    }
    settings->flags = 0;

    if (compatmask & COMPAT_MODE_1) {
        settings->flags |= IOPCORE_COMPAT_ACCU_READS;
    }

    if (compatmask & COMPAT_MODE_2) {
        settings->flags |= IOPCORE_COMPAT_ALT_READ;
    }

    if (compatmask & COMPAT_MODE_4) {
        settings->flags |= IOPCORE_COMPAT_0_SKIP_VIDEOS;
    }

    if (compatmask & COMPAT_MODE_5) {
        settings->flags |= IOPCORE_COMPAT_EMU_DVDDL;
    }

    if (compatmask & COMPAT_MODE_6) {
        settings->flags |= IOPCORE_ENABLE_POFF;
    }

    settings->fakemodule_flags = 0;
    settings->fakemodule_flags |= FAKE_MODULE_FLAG_CDVDFSV;
    settings->fakemodule_flags |= FAKE_MODULE_FLAG_CDVDSTM;

#ifdef GSM
    InitGSMConfig(pgcfg);
#endif

#ifdef CHEAT
    int cheat_enable = gGlobalGameCfg.cheat_enable;
    int cheat_mode = gGlobalGameCfg.cheat_mode;
    int cheat_image = gGlobalGameCfg.cheat_enable_image;

    if (pgcfg && pgcfg->cheat_source == SETTINGS_PERGAME) {
        cheat_enable = pgcfg->cheat_enable;
        cheat_mode = pgcfg->cheat_mode;
        cheat_image = pgcfg->cheat_enable_image;
    }

    InitCheatsConfig(cheat_enable, cheat_mode, cheat_image);
#endif


#ifdef PADEMU
    gEnablePadEmu = gGlobalGameCfg.pademu_enable;
    gPadEmuSettings = gGlobalGameCfg.pademu_settings;
    gPadMacroSettings = gGlobalGameCfg.padmacro_settings;

    if (pgcfg && pgcfg->pademu_source == SETTINGS_PERGAME) {
        gEnablePadEmu = pgcfg->pademu_enable;
        gPadEmuSettings = pgcfg->pademu_settings;
    }

    if (pgcfg && pgcfg->padmacro_source == SETTINGS_PERGAME)
        gPadMacroSettings = pgcfg->padmacro_settings;

    if (gEnablePadEmu)
        settings->fakemodule_flags |= FAKE_MODULE_FLAG_USBD;
#endif

    gOSDLanguageSource = 0;
    gOSDLanguageEnable = gGlobalGameCfg.osd_enable;
    gOSDLanguageValue = gGlobalGameCfg.osd_langid;
    gOSDTVAspectRatio = gGlobalGameCfg.osd_tv_aspect;
    gOSDVideOutput = gGlobalGameCfg.osd_vmode;

    if (pgcfg && pgcfg->osd_source == SETTINGS_PERGAME) {
        gOSDLanguageSource = SETTINGS_PERGAME;
        gOSDLanguageEnable = pgcfg->osd_enable;
        gOSDLanguageValue = pgcfg->osd_langid;
        gOSDTVAspectRatio = pgcfg->osd_tv_aspect;
        gOSDVideOutput = pgcfg->osd_vmode;
    }

    *patchindex = i;

    // game id
    memcpy(settings->DiscID, gameid, sizeof(settings->DiscID));

    return compatmask;
}

void sbUnprepare(void *pCommon)
{
    memcpy(pCommon, &cdvdman_settings_common_sample, sizeof(struct cdvdman_settings_common));
}

void sbRebuildULCfg(base_game_info_t **list, const char *prefix, int gamecount, int excludeID)
{
    char path[256];
    USBExtreme_game_entry_t GameEntry;
    snprintf(path, sizeof(path), "%sul.cfg", prefix);

    FILE *file = fopen(path, "wb");
    if (file != NULL) {
        int i;
        base_game_info_t *game;

        memset(&GameEntry, 0, sizeof(GameEntry));
        GameEntry.Byte08 = 0x08; // just to be compatible with original ul.cfg
        memcpy(GameEntry.magic, "ul.", 3);

        for (i = 0; i < gamecount; i++) {
            game = &(*list)[i];

            if (game->format == GAME_FORMAT_USBLD && (i != excludeID)) {
                memcpy(GameEntry.startup, game->startup, GAME_STARTUP_MAX);
                memcpy(GameEntry.name, game->name, UL_GAME_NAME_MAX);
                // don't fill last symbol with zero, cause trailing symbol can be useful character
                GameEntry.parts = game->parts;
                GameEntry.media = game->media;

                fwrite(&GameEntry, sizeof(GameEntry), 1, file);
            }
        }

        fclose(file);
    }
}

static void sbCreatePath_name(const base_game_info_t *game, char *path, const char *prefix, const char *sep, int part, const char *game_name)
{
    switch (game->format) {
        case GAME_FORMAT_USBLD:
            snprintf(path, 256, "%sul.%08X.%s.%02x", prefix, USBA_crc32(game_name), game->startup, part);
            break;
        case GAME_FORMAT_ISO:
            snprintf(path, 256, "%s%s%s%s%s", prefix, (game->media == SCECdPS2CD) ? "CD" : "DVD", sep, game_name, game->extension);
            break;
        case GAME_FORMAT_OLD_ISO:
            snprintf(path, 256, "%s%s%s%s.%s%s", prefix, (game->media == SCECdPS2CD) ? "CD" : "DVD", sep, game->startup, game_name, game->extension);
            break;
    }
}

void sbCreatePath(const base_game_info_t *game, char *path, const char *prefix, const char *sep, int part)
{
    sbCreatePath_name(game, path, prefix, sep, part, game->name);
}

void sbDelete(base_game_info_t **list, const char *prefix, const char *sep, int gamecount, int id)
{
    int part;
    char path[256];
    base_game_info_t *game = &(*list)[id];

    for (part = 0; part < game->parts; part++) {
        sbCreatePath(game, path, prefix, sep, part);
        unlink(path);
    }

    if (game->format == GAME_FORMAT_USBLD) {
        sbRebuildULCfg(list, prefix, gamecount, id);
    }
}

void sbRename(base_game_info_t **list, const char *prefix, const char *sep, int gamecount, int id, char *newname)
{
    int part;
    char oldpath[256], newpath[256];
    base_game_info_t *game = &(*list)[id];

    for (part = 0; part < game->parts; part++) {
        sbCreatePath_name(game, oldpath, prefix, sep, part, game->name);
        sbCreatePath_name(game, newpath, prefix, sep, part, newname);
        rename(oldpath, newpath);
    }

    if (game->format == GAME_FORMAT_USBLD) {
        memset(game->name, 0, UL_GAME_NAME_MAX + 1);
        memcpy(game->name, newname, UL_GAME_NAME_MAX);
        sbRebuildULCfg(list, prefix, gamecount, -1);
    }
}

void sbPopulateConfig(base_game_info_t *game, const char *prefix, const char *sep, game_info_t *gi, per_game_cfg_t *pgcfg)
{
    char info_path[256], cfg_path[256];
    struct stat st;

    snprintf(info_path, sizeof(info_path), "%sCFG%s%s.info", prefix, sep, game->startup);
    snprintf(cfg_path, sizeof(cfg_path), "%sCFG%s%s.cfg", prefix, sep, game->startup);

    if (gi) {
        wOPLGameInfoLoad(info_path, gi);

        // fallback if not set.. fill for display.. don't save
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

    if (pgcfg) {
        wOPLPerGameLoad(cfg_path, pgcfg);

        // auto determine format/media/size if not set
        if (!pgcfg->format[0]) {
            if (game->format == GAME_FORMAT_USBLD)
                strcpy(pgcfg->format, "UL");
            else if (!strcasecmp(game->extension, ".zso"))
                strcpy(pgcfg->format, "ZSO");
            else
                strcpy(pgcfg->format, "ISO");
        }

        if (!pgcfg->media[0])
            strcpy(pgcfg->media, game->media == SCECdPS2CD ? "CD" : "DVD");

        if (!pgcfg->size_mb) {
            if (game->sizeMB > 0) {
                pgcfg->size_mb = game->sizeMB;
            } else {
                char gamepath[256];
                if (game->format == GAME_FORMAT_ISO) {
                    snprintf(gamepath, sizeof(gamepath), "%s%s%s%s%s%s", prefix, sep, game->media == SCECdPS2CD ? "CD" : "DVD", sep, game->name, game->extension);
                    if (stat(gamepath, &st) == 0)
                        pgcfg->size_mb = st.st_size >> 20;
                } else if (game->format == GAME_FORMAT_OLD_ISO) {
                    snprintf(gamepath, sizeof(gamepath), "%s%s%s%s%s.%s%s", prefix, sep, game->media == SCECdPS2CD ? "CD" : "DVD", sep, game->startup, game->name, game->extension);
                    if (stat(gamepath, &st) == 0)
                        pgcfg->size_mb = st.st_size >> 20;
                }
            }
        }
    }
}

int sbSaveConfig(base_game_info_t *game, const char *prefix, const char *sep, const per_game_cfg_t *cfg)
{
    char path[256];

    snprintf(path, sizeof(path), "%sCFG%s%s.cfg", prefix, sep, game->startup);

    return wOPLPerGameSave(path, cfg);
}

static void sbCreateFoldersFromList(const char *path, const char **folders)
{
    int i;
    char fullpath[256];

    for (i = 0; folders[i] != NULL; i++) {
        sprintf(fullpath, "%s%s", path, folders[i]);
        mkdir(fullpath, 0777);
    }
}

void sbCreateFolders(const char *path, int createDiscImgFolders)
{
    const char *basicFolders[] = {"CFG", "THM", "LNG", "ART", "VMC", "CHT", "APPS", "IMG", NULL};
    const char *discImgFolders[] = {"CD", "DVD", NULL};

    sbCreateFoldersFromList(path, basicFolders);

    if (createDiscImgFolders)
        sbCreateFoldersFromList(path, discImgFolders);
}

#ifdef CHEAT
int sbLoadCheats(const char *path, const char *file)
{
    char cheatfile[64];
    int cheatMode = 0;

    if (GetCheatsEnabled()) {
        char filename[32];
        snprintf(filename, sizeof(filename), "%s.cht", file);

        TarEntryBase *entry = tarFind(TAR_KIND_CHT, filename);
        if (entry) {
            LOG("Loading Cheat File from TAR: %s\n", filename);

            void *buf = tarGet(TAR_KIND_CHT, filename);
            if (buf) {
                cheatMode = load_cheats_buf((char *)buf, entry->rawSize);
                free(buf);

                if (cheatMode >= 0) {
                    LOG("Cheats found in TAR\n");
                    if ((gAutoLaunchGame == NULL) && (gAutoLaunchBDMGame == NULL) && (cheatMode == 1))
                        guiManageCheats();
                    return cheatMode;
                }

                LOG("Error: failed to parse cheats from TAR\n");
            }
        }

        snprintf(cheatfile, sizeof(cheatfile), "%sCHT/%s.cht", path, file);
        LOG("Loading Cheat File %s\n", cheatfile);

        if ((cheatMode = load_cheats(cheatfile)) < 0)
            LOG("Error: failed to load cheats\n");
        else {
            LOG("Cheats found\n");
            if ((gAutoLaunchGame == NULL) && (gAutoLaunchBDMGame == NULL) && (cheatMode == 1))
                guiManageCheats();
        }
    }

    return cheatMode;
}

int sbLoadImage(const char *path, const char *file)
{
    char imgfile[64];
    int result = 0;

    if (GetImageEnabled()) {
        snprintf(imgfile, sizeof(imgfile), "%sIMG/%s.img", path, file);
        LOG("Load image file %s\n", imgfile);
        if (!LoadImage(imgfile)) {
            LOG("Image load success\n");
        } else {
            result = -1;
        }
    }
    return result;
}
#endif

static int sbTryNeutrinoPath(neutrino_path_t *path, const char *cwd)
{
    int i;
    int length;
    const char *elfNames[] = {
        "neutrino.elf",
        "neutrino.ELF",
        "NEUTRINO.elf",
        "NEUTRINO.ELF",
    };

    if (!path || !cwd || !cwd[0])
        return 0;

    snprintf(path->cwd, sizeof(path->cwd), "%s", cwd);

    length = strlen(path->cwd);
    if (length <= 0 || length >= sizeof(path->cwd) - 1)
        return 0;

    if (path->cwd[length - 1] != '/') {
        path->cwd[length++] = '/';
        path->cwd[length] = '\0';
    }

    for (i = 0; i < (int)(sizeof(elfNames) / sizeof(elfNames[0])); i++) {
        snprintf(path->elf, sizeof(path->elf), "%s%s", path->cwd, elfNames[i]);
        LOG("SUPPORTBASE: Checking Neutrino ELF '%s'\n", path->elf);

        if (sbFileExists(path->elf)) {
            LOG("SUPPORTBASE: Neutrino ELF found at '%s'\n", path->elf);
            return 1;
        }
    }

    path->elf[0] = '\0';
    path->cwd[0] = '\0';

    return 0;
}

/*
 * HDD path must not use pfs0: because hddLaunchGame() deinitializes/unmounts it before launching Neutrino
 * For +wOPL: hdd0:+wOPL/neutrino/neutrino.elf
 * For __common: hdd0:__common/wOPL/neutrino/neutrino.elf
 */
int sbFindNeutrino(neutrino_path_t *path, const char *preferredPrefix)
{
    int i;
    char cwd[256];
    const char *mcPaths[] = {
        "mc0:NEUTRINO",
        "mc1:NEUTRINO",
        "mc0:/NEUTRINO",
        "mc1:/NEUTRINO",
        "mc0:neutrino",
        "mc1:neutrino",
        "mc0:/neutrino",
        "mc1:/neutrino",
        "mc0:/APPS/neutrino",
        "mc1:/APPS/neutrino",
        "mc0:/APPS/NEUTRINO",
        "mc1:/APPS/NEUTRINO",
    };

    if (!path)
        return 0;

    path->elf[0] = '\0';
    path->cwd[0] = '\0';

    if (preferredPrefix && preferredPrefix[0]) {
        if (!strncmp(preferredPrefix, "hdd0:", 5)) {
            if (preferredPrefix[5] != '+') {
                snprintf(cwd, sizeof(cwd), "%s/%s/neutrino", preferredPrefix, WOPL_CONFIG_NAME);
                if (sbTryNeutrinoPath(path, cwd))
                    return 1;

                snprintf(cwd, sizeof(cwd), "%s/%s/NEUTRINO", preferredPrefix, WOPL_CONFIG_NAME);
                if (sbTryNeutrinoPath(path, cwd))
                    return 1;
            }

            snprintf(cwd, sizeof(cwd), "%s/neutrino", preferredPrefix);
            if (sbTryNeutrinoPath(path, cwd))
                return 1;

            snprintf(cwd, sizeof(cwd), "%s/NEUTRINO", preferredPrefix);
            if (sbTryNeutrinoPath(path, cwd))
                return 1;
        } else {
            snprintf(cwd, sizeof(cwd), "%sneutrino", preferredPrefix);
            if (sbTryNeutrinoPath(path, cwd))
                return 1;

            snprintf(cwd, sizeof(cwd), "%sNEUTRINO", preferredPrefix);
            if (sbTryNeutrinoPath(path, cwd))
                return 1;
        }
    }

    for (i = 0; i < MAX_BDM_DEVICES; i++) {
        snprintf(cwd, sizeof(cwd), "mass%d:/neutrino", i);
        if (sbTryNeutrinoPath(path, cwd))
            return 1;

        snprintf(cwd, sizeof(cwd), "mass%d:neutrino", i);
        if (sbTryNeutrinoPath(path, cwd))
            return 1;

        snprintf(cwd, sizeof(cwd), "mass%d:/NEUTRINO", i);
        if (sbTryNeutrinoPath(path, cwd))
            return 1;

        snprintf(cwd, sizeof(cwd), "mass%d:NEUTRINO", i);
        if (sbTryNeutrinoPath(path, cwd))
            return 1;
    }

    for (i = 0; i < 2; i++) {
        snprintf(cwd, sizeof(cwd), "mmce%d:/neutrino", i);
        if (sbTryNeutrinoPath(path, cwd))
            return 1;

        snprintf(cwd, sizeof(cwd), "mmce%d:neutrino", i);
        if (sbTryNeutrinoPath(path, cwd))
            return 1;

        snprintf(cwd, sizeof(cwd), "mmce%d:/NEUTRINO", i);
        if (sbTryNeutrinoPath(path, cwd))
            return 1;

        snprintf(cwd, sizeof(cwd), "mmce%d:NEUTRINO", i);
        if (sbTryNeutrinoPath(path, cwd))
            return 1;
    }

    for (i = 0; i < (int)(sizeof(mcPaths) / sizeof(mcPaths[0])); i++) {
        if (sbTryNeutrinoPath(path, mcPaths[i]))
            return 1;
    }

    return 0;
}

void sbCreateNeutrinoVMCPath(char *path, int length, const char *prefix, const char *vmc)
{
    if (!path || length <= 0)
        return;

    path[0] = '\0';

    if (!prefix || !prefix[0] || !vmc || !vmc[0])
        return;

    if (!strncmp(prefix, "hdd0:", 5)) {
        if (prefix[5] != '+')
            snprintf(path, length, "%s/%s/VMC/%s.bin", prefix, WOPL_CONFIG_NAME, vmc);
        else
            snprintf(path, length, "%s/VMC/%s.bin", prefix, vmc);
    } else
        snprintf(path, length, "%sVMC/%s.bin", prefix, vmc);
}

static int sbParsePathDeviceIndex(const char *path, const char *prefix, int *device)
{
    const char *p;
    int dev = 0;
    int haveDigit = 0;
    int prefixLen = strlen(prefix);

    if (!path || strncmp(path, prefix, prefixLen))
        return 0;

    p = path + prefixLen;

    while (*p >= '0' && *p <= '9') {
        haveDigit = 1;
        dev = dev * 10 + (*p - '0');
        p++;
    }

    if (*p != ':')
        return 0;

    if (!haveDigit)
        dev = 0;

    if (device)
        *device = dev;

    return 1;
}

int sbGetPathModeAndDevice(const char *path, int *device)
{
    const char *blkdevnameend;
    const char *prefixend;
    int i, blkdevnamelen, prefixlen;
    int dev;
    item_list_t *listSupport;

    if (device)
        *device = -1;

    if (!path || !path[0])
        return -1;

    if (!strncmp(path, "hdd0:", 5) || !strncmp(path, "pfs0:", 5))
        return HDD_MODE;

    if (sbParsePathDeviceIndex(path, "mass", &dev)) {
        if (dev < 0 || dev >= MAX_BDM_DEVICES)
            return -1;

        if (device)
            *device = dev;

        return BDM_MODE + dev;
    }

    if (sbParsePathDeviceIndex(path, "mmce", &dev)) {
        if (device)
            *device = dev;

        return MMCE_MODE;
    }

    blkdevnameend = strchr(path, ':');
    if (blkdevnameend == NULL)
        return -1;

    blkdevnamelen = (int)(blkdevnameend - path);

    for (i = 0; i < MODE_COUNT; i++) {
        listSupport = list_support[i].support;
        if ((listSupport != NULL) && (listSupport->itemGetPrefix != NULL)) {
            char *prefix = listSupport->itemGetPrefix(listSupport);
            if (prefix != NULL) {
                prefixend = strchr(prefix, ':');
                if (prefixend != NULL) {
                    prefixlen = (int)(prefixend - prefix);

                    if (blkdevnamelen == prefixlen && strncmp(path, prefix, blkdevnamelen) == 0)
                        return listSupport->mode;
                }
            }
        }
    }

    return -1;
}

int sbGetPathMode(const char *path)
{
    return sbGetPathModeAndDevice(path, NULL);
}

int sbPathIsMC(const char *path)
{
    return path && (!strncmp(path, "mc0:", 4) || !strncmp(path, "mc1:", 4));
}
