#include "include/opl.h"
#include "include/art_tar.h"
#include <ps2sdkapi.h>

static ArtTarEntry *s_tarIndex = NULL;
static u32 s_tarCount = 0;
static int s_tarFd = -1;

static u64 parseOctal(const char *s, int len)
{
    u64 val = 0;
    for (int i = 0; i < len; i++) {
        char c = s[i];
        if (c == '\0' || c == ' ')
            break;
        if (c < '0' || c > '7')
            break;
        val = (val << 3) + (u64)(c - '0');
    }
    return val;
}

static int isZeroBlock(const unsigned char header[TAR_BLOCK_SIZE])
{
    for (int i = 0; i < TAR_BLOCK_SIZE; i++) {
        if (header[i] != 0)
            return 0;
    }
    return 1;
}

ArtTarEntry *findTarEntry(const char *filename)
{
    if (!filename || !s_tarIndex || s_tarCount == 0)
        return NULL;

    for (u32 i = 0; i < s_tarCount; i++) {
        if (strcasecmp(s_tarIndex[i].filename, filename) == 0)
            return &s_tarIndex[i];
    }
    return NULL;
}

int closeTarFile(void)
{
    if (s_tarFd >= 0) {
        close(s_tarFd);
        s_tarFd = -1;
    }
    free(s_tarIndex);
    s_tarIndex = NULL;
    s_tarCount = 0;
    return 0;
}

static int readTarCache(const char *cachePath, const char *tarPath)
{
    int fd = open(cachePath, O_RDONLY);
    if (fd < 0)
        return -1;

    struct stat stCache;
    if (fstat(fd, &stCache) < 0) {
        close(fd);
        return -1;
    }

    int fileSize = stCache.st_size;
    if (fileSize < (int)sizeof(ArtCacheHeader)) {
        close(fd);
        return -1;
    }

    void *buf = memalign(64, fileSize);
    if (!buf) {
        close(fd);
        return -1;
    }

    int bytesRead = read(fd, buf, fileSize);
    close(fd);
    if (bytesRead != fileSize) {
        free(buf);
        return -1;
    }

    unsigned char *p = (unsigned char *)buf;
    ArtCacheHeader *hdr = (ArtCacheHeader *)p;

    if (memcmp(hdr->magic, ARC_MAGIC, 4) != 0 ||
        hdr->version != ARC_VERSION) {
        free(buf);
        return -1;
    }

    struct stat stTar;
    if (stat(tarPath, &stTar) < 0 || (u64)stTar.st_size != hdr->tarSize) {
        free(buf);
        return -1;
    }

    u32 entryCount = hdr->entryCount;
    int expectedSize = sizeof(ArtCacheHeader) + entryCount * sizeof(ArtTarEntry);

    if (fileSize < expectedSize) {
        free(buf);
        return -1;
    }

    ArtTarEntry *entries = malloc(sizeof(ArtTarEntry) * entryCount);
    if (!entries) {
        free(buf);
        return -1;
    }

    ArtTarEntry *src = (ArtTarEntry *)(p + sizeof(ArtCacheHeader));
    memcpy(entries, src, sizeof(ArtTarEntry) * entryCount);

    s_tarIndex = entries;
    s_tarCount = entryCount;

    free(buf);
    return 0;
}

static int writeTarCache(const char *cachePath, const char *tarPath)
{
    if (!s_tarIndex || s_tarCount == 0)
        return -1;

    struct stat stTar;
    if (stat(tarPath, &stTar) < 0)
        return -1;

    ArtCacheHeader hdr;
    memcpy(hdr.magic, ARC_MAGIC, 4);
    hdr.version = ARC_VERSION;
    hdr.tarSize = (u64)stTar.st_size;
    hdr.entryCount = s_tarCount;

    int fd = open(cachePath, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (fd < 0)
        return -1;

    if (write(fd, &hdr, sizeof(hdr)) != sizeof(hdr)) {
        close(fd);
        return -1;
    }

    int entriesSize = sizeof(ArtTarEntry) * s_tarCount;
    if (write(fd, s_tarIndex, entriesSize) != entriesSize) {
        close(fd);
        return -1;
    }

    close(fd);
    return 0;
}

int loadTarFile(const char *path)
{
    char dirPath[256];
    strncpy(dirPath, path, sizeof(dirPath));
    dirPath[sizeof(dirPath) - 1] = '\0';

    int len = strlen(dirPath);
    for (int i = len - 1; i >= 0; i--) {
        if (dirPath[i] == '/') {
            dirPath[i + 1] = '\0';
            break;
        }
    }

    const char *cachePath = "art_cache.bin";

    char fullCachePath[256];
    snprintf(fullCachePath, sizeof(fullCachePath), "%s%s", dirPath, cachePath);

    if (s_tarFd >= 0 || s_tarIndex)
        closeTarFile();

    if (readTarCache(fullCachePath, path) == 0) {
        s_tarFd = open(path, O_RDONLY);
        return (s_tarFd >= 0) ? 0 : -1;
    }

    s_tarFd = open(path, O_RDONLY);
    if (s_tarFd < 0)
        return -1;

    s_tarIndex = NULL;
    s_tarCount = 0;

    while (1) {
        unsigned char header[TAR_BLOCK_SIZE];
        int bytesRead = read(s_tarFd, header, TAR_BLOCK_SIZE);
        if (bytesRead == 0)
            break;
        if (bytesRead != TAR_BLOCK_SIZE)
            goto fail;

        if (isZeroBlock(header))
            break;

        char name[21];
        memcpy(name, header, 20);
        name[20] = '\0';

        u64 rawSize64 = parseOctal((const char *)header + 124, 12);
        u64 paddedSize64 = ((rawSize64 + (TAR_BLOCK_SIZE - 1)) / TAR_BLOCK_SIZE) * TAR_BLOCK_SIZE;

        if (rawSize64 > MAX_FILE_SIZE || paddedSize64 > MAX_FILE_SIZE)
            goto fail;

        u64 dataOffset = lseek64(s_tarFd, 0, SEEK_CUR);
        if (dataOffset == (u64)-1)
            goto fail;

        ArtTarEntry *newIndex = realloc(s_tarIndex, sizeof(ArtTarEntry) * (s_tarCount + 1));
        if (!newIndex)
            goto fail;

        s_tarIndex = newIndex;

        ArtTarEntry *entry = &s_tarIndex[s_tarCount];
        strncpy(entry->filename, name, 20);
        entry->filename[20] = '\0';
        entry->offset = dataOffset;
        entry->rawSize = (u32)rawSize64;
        entry->paddedSize = (u32)paddedSize64;
        s_tarCount++;

        if (lseek64(s_tarFd, paddedSize64, SEEK_CUR) == (u64)-1)
            goto fail;
    }

    writeTarCache(fullCachePath, path);
    return 0;

fail:
    closeTarFile();
    return -1;
}

int hasTarEntry(const char *filename)
{
    return findTarEntry(filename) ? 1 : 0;
}

u32 readFileFromTar(const ArtTarEntry *entry, void *dst, u32 dstSize)
{
    if (!entry || s_tarFd < 0 || !dst)
        return 0;
    if (dstSize < entry->rawSize)
        return 0;

    if (lseek64(s_tarFd, entry->offset, SEEK_SET) != entry->offset)
        return 0;

    u32 total = 0;
    while (total < entry->rawSize) {
        int bytesRead = read(s_tarFd, (unsigned char *)dst + total, entry->rawSize - total);
        if (bytesRead <= 0)
            return 0;

        total += (u32)bytesRead;
    }
    return total;
}

void *getFileFromTar(const char *filename)
{
    ArtTarEntry *entry = findTarEntry(filename);
    if (!entry)
        return NULL;

    void *buffer = malloc(entry->rawSize);
    if (!buffer)
        return NULL;

    u32 bytesRead = readFileFromTar(entry, buffer, entry->rawSize);
    if (bytesRead != entry->rawSize) {
        free(buffer);
        return NULL;
    }
    return buffer;
}