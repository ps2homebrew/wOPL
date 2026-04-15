#ifndef __TAR_H
#define __TAR_H

#define TAR_BLOCK_SIZE 512
#define MAX_FILE_SIZE  (4 * 1024 * 1024) // 4 MiB
#define ARC_MAGIC      "ARC\0"
#define ARC_VERSION    1

typedef struct ArtCacheHeader
{
    char magic[4];
    u32 version;
    u64 tarSize;
    u32 entryCount;
} ArtCacheHeader;

typedef struct
{
    u64 offset;        // offset of file data in archive
    u32 rawSize;       // actual file size from header (octal), capped by MAX_FILE_SIZE
    u32 paddedSize;    // file size rounded up to 512-byte TAR blocks
    char filename[21]; // filename up to 21 chars + null
} ArtTarEntry;

int loadTarFile(const char *path);
int closeTarFile(void);
ArtTarEntry *findTarEntry(const char *filename);
int hasTarEntry(const char *filename);
void *getFileFromTar(const char *filename);
u32 readFileFromTar(const ArtTarEntry *entry, void *dst, u32 dstSize);

#endif