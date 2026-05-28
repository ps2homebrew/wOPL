#ifndef __SUPPORT_BASE_H
#define __SUPPORT_BASE_H

enum GAME_FORMAT {
    GAME_FORMAT_USBLD = 0,
    GAME_FORMAT_OLD_ISO,
    GAME_FORMAT_ISO,
};

typedef struct
{
    char name[ISO_GAME_NAME_MAX + 1]; // MUST be the higher value from UL / ISO
    char startup[GAME_STARTUP_MAX + 1];
    char extension[ISO_GAME_EXTENSION_MAX + 1];
    u8 parts;
    u8 media;
    u8 format;
    u32 sizeMB;
} base_game_info_t;

typedef struct
{
    char name[UL_GAME_NAME_MAX];    // it is not a string but character array, terminating NULL is not necessary
    char magic[3];                  // magic string "ul."
    char startup[GAME_STARTUP_MAX]; // it is not a string but character array, terminating NULL is not necessary
    u8 parts;                       // slice count
    u8 media;                       // Disc type
    u8 unknown[4];                  // Always zero
    u8 Byte08;                      // Always 0x08
    u8 unknown2[10];                // Always zero
} USBExtreme_game_entry_t;


typedef struct
{
    int fd;
    int mode;
    char *buffer;
    unsigned int size;
    unsigned int available;
    char *lastPtr;
    short allocResult;
} file_buffer_t;

#ifdef PADEMU
extern int gEnablePadEmu;
extern int gPadEmuSettings;
extern int gPadMacroSource;
extern int gPadMacroSettings;
extern int gPadEmuSource;
#endif

int isValidIsoName(char *name, int *pNameLen);
int sbGetmcID(void);
int sbGetFileSize(int fd);
void sbCheckMCFolder(void);
int sbOpenFile(char *path, int mode);
void *sbReadFile(char *path, int align, int *size);
int sbIsSameSize(const char *prefix, int prevSize);
int sbFileExists(const char *path);
int sbCreateSemaphore(void);
int sbListDir(char *path, const char *separator, int maxElem,
              int (*readEntry)(int index, const char *path, const char *separator, const char *name, unsigned char d_type));
int sbReadList(base_game_info_t **list, const char *prefix, int *fsize, int *gamecount);
int sbPrepare(base_game_info_t *game, const per_game_cfg_t *pgcfg, int size_cdvdman, void **cdvdman_irx, int *patchindex);
void sbUnprepare(void *pCommon);
void sbRebuildULCfg(base_game_info_t **list, const char *prefix, int gamecount, int excludeID);
void sbCreatePath(const base_game_info_t *game, char *path, const char *prefix, const char *sep, int part);
void sbDelete(base_game_info_t **list, const char *prefix, const char *sep, int gamecount, int id);
void sbRename(base_game_info_t **list, const char *prefix, const char *sep, int gamecount, int id, char *newname);
void sbPopulateConfig(base_game_info_t *game, const char *prefix, const char *sep, game_info_t *gi, per_game_cfg_t *pgcfg);
void sbCreateFolders(const char *path, int createDiscImgFolders);
file_buffer_t *sbOpenFileBufferBuffer(short allocResult, const void *buffer, unsigned int size);
file_buffer_t *sbOpenFileBuffer(char *fpath, int mode, short allocResult, unsigned int size);
int sbReadFileBuffer(file_buffer_t *readContext, char **outBuf);
void sbWriteFileBuffer(file_buffer_t *fileBuffer, char *inBuf, int size);
void sbCloseFileBuffer(file_buffer_t *fileBuffer);
void sbMMCESendGameId(const char *gameId);

int sbSaveConfig(base_game_info_t *game, const char *prefix, const char *sep, const per_game_cfg_t *cfg);

// ISO9660 filesystem management functions.
u32 sbGetISO9660MaxLBA(const char *path);
int sbProbeISO9660(const char *path, base_game_info_t *game, u32 layer1_offset);
int sbProbeISO9660_64(const char *path, base_game_info_t *game, u32 layer1_offset);

#ifdef CHEAT
int sbLoadCheats(const char *path, const char *file);
int sbLoadImage(const char *path, const char *file);
#endif

#endif
