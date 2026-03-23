#include "include/opl.h"
#include "include/textures.h"
#include "include/util.h"
#include "include/ioman.h"
#include "include/art_tar.h"
#include <png.h>
#include <fcntl.h>
#include <malloc.h>
#include <unistd.h>

extern void *loading_0_png;
extern void *loading_1_png;
extern void *loading_2_png;
extern void *loading_3_png;
extern void *loading_4_png;
extern void *loading_5_png;
extern void *loading_6_png;
extern void *loading_7_png;
extern void *loading_8_png;
extern void *category_empty_bdm_png; // Leave BDM Icon as usb.png to maintain theme compat
extern void *category_usb_png;
extern void *category_ilink_png;
extern void *category_mx4sio_png;
extern void *category_hdd_bdm_png;
extern void *category_hdd_apa_png;
extern void *category_net_smb_png;
extern void *category_apps_png;
extern void *category_fav_png;
extern void *mark_star_png;
extern void *category_mmce_png;
extern void *bdm_index_1_png;
extern void *bdm_index_2_png;
extern void *bdm_index_3_png;
extern void *bdm_index_4_png;
extern void *bdm_index_5_png;

extern void *button_stick_r3_png;
extern void *button_dpad_left_png;
extern void *button_dpad_right_png;
extern void *button_dpad_up_png;
extern void *button_dpad_down_png;
extern void *button_symbol_cross_png;
extern void *button_symbol_triangle_png;
extern void *button_symbol_circle_png;
extern void *button_symbol_square_png;
extern void *button_select_png;
extern void *button_start_png;
/* currently unused.
extern void *L1_png;
extern void *L2_png;
extern void *L3_png;
extern void *R1_png;
extern void *R2_png;*/

extern void *bg_main_png;
extern void *bg_info_png;
extern void *cover_png;
extern void *disc_png;
extern void *screenshot_png;

extern void *badge_exec_elf_png;
extern void *badge_disc_hdl_png;
extern void *badge_disc_iso_png;
extern void *badge_disc_zso_png;
extern void *badge_disc_ul_png;
extern void *badge_exec_app_png;
extern void *badge_disc_cd_png;
extern void *badge_disc_dvd_png;
extern void *badge_vmode_43_png;
extern void *badge_vmode_169_png;
extern void *badge_vmode_169_ps2rd_png;
extern void *badge_vmode_169_hexiso_png;
extern void *dev_1_png;
extern void *dev_2_png;
extern void *dev_3_png;
extern void *dev_4_png;
extern void *dev_5_png;
extern void *dev_6_png;
extern void *dev_7_png;
extern void *dev_8_png;
extern void *rating_0_png;
extern void *rating_1_png;
extern void *rating_2_png;
extern void *rating_3_png;
extern void *rating_4_png;
extern void *rating_5_png;
extern void *badge_res_240p_png;
extern void *badge_res_240p_hexiso_png;
extern void *badge_res_480i_png;
extern void *badge_res_480p_png;
extern void *badge_res_480p_xt_png;
extern void *badge_res_480p_xc_png;
extern void *badge_res_480p_gsm_png;
extern void *badge_res_480p_hexiso_png;
extern void *badge_res_480p_ps2rd_png;
extern void *badge_res_576i_png;
extern void *badge_res_576p_gsm_png;
extern void *badge_res_720p_gsm_png;
extern void *badge_res_1080i_png;
extern void *badge_res_1080i_gsm_png;
extern void *badge_res_1080p_gsm_png; // TODO: Add 1080p support or remove this since gsm doesn´t support 1080p
extern void *badge_region_multi_png;
extern void *badge_region_ntsc_png;
extern void *badge_region_pal_png;

extern void *logo_png;
extern void *case_png;
extern void *apps_case_png;
extern void *plank_png;
extern void *discbox_list_games_png;
extern void *discbox_list_apps_png;
extern void *discbox_list_shadow_png;

// Not related to screen size, just to limit at some point
static int maxSize = 720 * 512 * 4;

typedef struct
{
    int id;
    char *name;
    void **texture;
} texture_t;

typedef struct
{
    u8 red;
    u8 green;
    u8 blue;
    u8 alpha;
} png_clut_t;

typedef struct
{
    png_colorp palette;
    int numPalette;
    int numTrans;
    png_bytep trans;
} png_texture_t;

static png_texture_t pngTexture;

static texture_t internalDefault[TEXTURES_COUNT] = {
    {LOADING_1_ICON, "loading_1", &loading_1_png},
    {LOADING_2_ICON, "loading_2", &loading_2_png},
    {LOADING_3_ICON, "loading_3", &loading_3_png},
    {LOADING_4_ICON, "loading_4", &loading_4_png},
    {LOADING_5_ICON, "loading_5", &loading_5_png},
    {LOADING_6_ICON, "loading_6", &loading_6_png},
    {LOADING_7_ICON, "loading_7", &loading_7_png},
    {LOADING_8_ICON, "loading_8", &loading_8_png},
    {CATEGORY_EMPTY_BDM_ICON, "category_empty_bdm", &category_empty_bdm_png},
    {CATEGORY_USB_ICON, "category_usb", &category_usb_png},
    {CATEGORY_ILINK_ICON, "category_ilink", &category_ilink_png},
    {CATEGORY_MX4SIO_ICON, "category_mx4sio", &category_mx4sio_png},
    {CATEGORY_HDD_BDM_ICON, "category_hdd_bdm", &category_hdd_bdm_png},
    {CATEGORY_HDD_APA_ICON, "category_hdd_apa", &category_hdd_apa_png},
    {CATEGORY_NET_SMB_ICON, "category_net_smb", &category_net_smb_png},
    {CATEGORY_APPS_ICON, "category_apps", &category_apps_png},
    {CATEGORY_FAV_ICON, "category_fav", &category_fav_png},
    {MARK_STAR, "mark_star", &mark_star_png},
    {CATEGORY_MMCE_ICON, "category_mmce", &category_mmce_png},
    {BDM_INDEX_1, "bdm_index_1", &bdm_index_1_png},
    {BDM_INDEX_2, "bdm_index_2", &bdm_index_2_png},
    {BDM_INDEX_3, "bdm_index_3", &bdm_index_3_png},
    {BDM_INDEX_4, "bdm_index_4", &bdm_index_4_png},
    {BDM_INDEX_5, "bdm_index_5", &bdm_index_5_png},
    {BUTTON_STICK_R3_ICON, "button_stick_r3", &button_stick_r3_png},
    {BUTTON_DPAD_LEFT_ICON, "button_dpad_left", &button_dpad_left_png},
    {BUTTON_DPAD_RIGHT_ICON, "button_dpad_right", &button_dpad_right_png},
    {BUTTON_SYMBOL_CROSS_ICON, "button_symbol_cross", &button_symbol_cross_png},
    {BUTTON_SYMBOL_TRIANGLE_ICON, "button_symbol_triangle", &button_symbol_triangle_png},
    {BUTTON_SYMBOL_CIRCLE_ICON, "button_symbol_circle", &button_symbol_circle_png},
    {BUTTON_SYMBOL_SQUARE_ICON, "button_symbol_square", &button_symbol_square_png},
    {BUTTON_SELECT_ICON, "button_select", &button_select_png},
    {BUTTON_START_ICON, "button_start", &button_start_png},
    {BUTTON_DPAD_UP_ICON, "button_dpad_up", &button_dpad_up_png},
    {BUTTON_DPAD_DOWN_ICON, "button_dpad_down", &button_dpad_down_png},
    /* currently unused.
    {L1_ICON, "L1", &L1_png},
    {L2_ICON, "L2", &L2_png},
    {L3_ICON, "L3", &L3_png},
    {R1_ICON, "R1", &R1_png},
    {R2_ICON, "R2", &R2_png}, */
    {BG_MAIN, "bg_main", &bg_main_png},
    {BG_INFO, "bg_info", &bg_info_png},
    {COVER_DEFAULT, "cover", &cover_png},
    {DISC_DEFAULT, "disc", &disc_png},
    {SCREENSHOT_DEFAULT, "screenshot", &screenshot_png},
    {BADGE_EXEC_ELF_FORMAT, "badge_exec_elf", &badge_exec_elf_png},
    {BADGE_DISC_HDL_FORMAT, "badge_disc_hdl", &badge_disc_hdl_png},
    {BADGE_DISC_ISO_FORMAT, "badge_disc_iso", &badge_disc_iso_png},
    {BADGE_DISC_ZSO_FORMAT, "badge_disc_zso", &badge_disc_zso_png},
    {BADGE_DISC_UL_FORMAT, "badge_disc_ul", &badge_disc_ul_png},
    {BADGE_EXEC_APP_MEDIA, "badge_exec_app", &badge_exec_app_png},
    {BADGE_DISC_CD_MEDIA, "badge_disc_cd", &badge_disc_cd_png},
    {BADGE_DISC_DVD_MEDIA, "badge_disc_dvd", &badge_disc_dvd_png},
    {BADGE_VMODE_43, "badge_vmode_43", &badge_vmode_43_png},
    {BADGE_VMODE_169, "badge_vmode_169", &badge_vmode_169_png},
    {BADGE_VMODE_169_PS2RD, "badge_vmode_169_ps2rd", &badge_vmode_169_ps2rd_png},
    {BADGE_VMODE_169_HEXISO, "badge_vmode_169_hexiso", &badge_vmode_169_hexiso_png},
    {DEV_1, "dev_1", &dev_1_png},
    {DEV_2, "dev_2", &dev_2_png},
    {DEV_3, "dev_3", &dev_3_png},
    {DEV_4, "dev_4", &dev_4_png},
    {DEV_5, "dev_5", &dev_5_png},
    {DEV_6, "dev_6", &dev_6_png},
    {DEV_7, "dev_7", &dev_7_png},
    {DEV_8, "dev_8", &dev_8_png},
    {RATING_0, "rating_0", &rating_0_png},
    {RATING_1, "rating_1", &rating_1_png},
    {RATING_2, "rating_2", &rating_2_png},
    {RATING_3, "rating_3", &rating_3_png},
    {RATING_4, "rating_4", &rating_4_png},
    {RATING_5, "rating_5", &rating_5_png},
    {BADGE_RES_240P, "badge_res_240p", &badge_res_240p_png},
    {BADGE_RES_240_HEXISO, "badge_res_240p_hexiso", &badge_res_240p_hexiso_png},
    {BADGE_RES_480I, "badge_res_480i", &badge_res_480i_png},
    {BADGE_RES_480P, "badge_res_480p", &badge_res_480p_png},
    {BADGE_RES_480P_XT, "badge_res_480p_xt", &badge_res_480p_xt_png},
    {BADGE_RES_480P_XC, "badge_res_480p_xc", &badge_res_480p_xc_png},
    {BADGE_RES_480P_GSM, "badge_res_480p_gsm", &badge_res_480p_gsm_png},
    {BADGE_RES_480P_PS2RD, "badge_res_480p_ps2rd", &badge_res_480p_ps2rd_png},
    {BADGE_RES_480P_HEXISO, "badge_res_480p_hexiso", &badge_res_480p_hexiso_png},
    {BADGE_RES_576I, "badge_res_576i", &badge_res_576i_png},
    {BADGE_RES_576P_GSM, "badge_res_576p_gsm", &badge_res_576p_gsm_png},
    {BADGE_RES_720P_GSM, "badge_res_720p_gsm", &badge_res_720p_gsm_png},
    {BADGE_RES_1080I, "badge_res_1080i", &badge_res_1080i_png},
    {BADGE_RES_1080I_GSM, "badge_res_1080i_gsm", &badge_res_1080i_gsm_png},
    {BADGE_RES_1080P_GSM, "badge_res_1080p_gsm", &badge_res_1080p_gsm_png},
    {BADGE_REGION_MULTI, "badge_region_multi", &badge_region_multi_png},
    {BADGE_REGION_NTSC, "badge_region_ntsc", &badge_region_ntsc_png},
    {BADGE_REGION_PAL, "badge_region_pal", &badge_region_pal_png},
    {LOGO_PICTURE, "logo", &logo_png},
    {CASE_OVERLAY, "case", &case_png},
    {APPS_CASE_OVERLAY, "apps_case", &apps_case_png},
    {PLANK, "plank", &plank_png},
    {DISCBOX_LIST_GAMES_OVERLAY, "discbox_list_games", &discbox_list_games_png},
    {DISCBOX_LIST_APPS_OVERLAY, "discbox_list_apps", &discbox_list_apps_png},
    {DISCBOX_LIST_SHADOW_OVERLAY, "discbox_list_shadow", &discbox_list_shadow_png},
};

int texLookupInternalTexId(const char *name)
{
    int i;
    int result = -1;

    for (i = 0; i < TEXTURES_COUNT; i++) {
        if (!strcmp(name, internalDefault[i].name)) {
            result = internalDefault[i].id;
            break;
        }
    }

    return result;
}

static int texSizeValidate(int width, int height, u8 psm)
{
    if (width > 1024 || height > 1024)
        return -1;

    if (gsKit_texture_size(width, height, (int)psm) > maxSize)
        return -1;

    return 0;
}

static void texPrepare(GSTEXTURE *texture)
{
    texture->Width = 0;                              // Must be set by loader
    texture->Height = 0;                             // Must be set by loader
    texture->PSM = GS_PSM_CT24;                      // Must be set by loader
    texture->ClutPSM = 0;                            // Default, can be set by loader
    texture->TBW = 0;                                // gsKit internal value
    texture->Mem = NULL;                             // Must be allocated by loader
    texture->Clut = NULL;                            // Default, can be set by loader
    texture->Vram = 0;                               // VRAM allocation handled by texture manager
    texture->VramClut = 0;                           // VRAM allocation handled by texture manager
    texture->Filter = GS_FILTER_LINEAR;              // Default
    texture->ClutStorageMode = GS_CLUT_STORAGE_CSM1; // Default

    // Do not load the texture to VRAM directly, only load it to EE RAM
    texture->Delayed = 1;
}

void texFree(GSTEXTURE *texture)
{
    if (texture->Mem) {
        free(texture->Mem);
        texture->Mem = NULL;
    }
    if (texture->Clut) {
        free(texture->Clut);
        texture->Clut = NULL;
    }
}

static int texEnd(png_structp pngPtr, png_infop infoPtr, void *pFileBuffer, int status)
{
    if (pFileBuffer != NULL)
        free(pFileBuffer);

    if (infoPtr != NULL)
        png_destroy_read_struct(&pngPtr, &infoPtr, (png_infopp)NULL);

    return status;
}

static void texReadMemFunction(png_structp pngPtr, png_bytep data, png_size_t length)
{
    void **PngBufferPtr = png_get_io_ptr(pngPtr);

    memcpy(data, *PngBufferPtr, length);
    *PngBufferPtr = (u8 *)(*PngBufferPtr) + length;
}

static void texReadPixels4(GSTEXTURE *texture, png_bytep *rowPointers, size_t size)
{
    unsigned char *pixel = (unsigned char *)texture->Mem;
    png_clut_t *clut = (png_clut_t *)texture->Clut;
    int i;

    memset(&clut[pngTexture.numPalette], 0, (16 - pngTexture.numPalette) * sizeof(clut[0]));

    for (i = 0; i < pngTexture.numPalette; i++) {
        clut[i].red = pngTexture.palette[i].red;
        clut[i].green = pngTexture.palette[i].green;
        clut[i].blue = pngTexture.palette[i].blue;
        clut[i].alpha = (i < pngTexture.numTrans) ? (pngTexture.trans[i] >> 1) : 0x80;
    }

    for (i = 0; i < texture->Height; i++)
        memcpy(&pixel[i * (texture->Width / 2)], rowPointers[i], texture->Width / 2);

    for (i = 0; i < size; i++)
        pixel[i] = (pixel[i] << 4) | (pixel[i] >> 4);
}

static void texReadPixels8(GSTEXTURE *texture, png_bytep *rowPointers, size_t size)
{
    unsigned char *pixel = (unsigned char *)texture->Mem;
    png_clut_t *clut = (png_clut_t *)texture->Clut;
    int i;

    memset(&clut[pngTexture.numPalette], 0, (256 - pngTexture.numPalette) * sizeof(clut[0]));

    for (i = 0; i < pngTexture.numPalette; i++) {
        clut[i].red = pngTexture.palette[i].red;
        clut[i].green = pngTexture.palette[i].green;
        clut[i].blue = pngTexture.palette[i].blue;
        clut[i].alpha = (i < pngTexture.numTrans) ? (pngTexture.trans[i] >> 1) : 0x80;
    }

    for (i = 0; i < pngTexture.numPalette; i++) {
        if ((i & 0x18) == 8) {
            png_clut_t tmp = clut[i];
            clut[i] = clut[i + 8];
            clut[i + 8] = tmp;
        }
    }

    for (i = 0; i < texture->Height; i++)
        memcpy(&pixel[i * texture->Width], rowPointers[i], texture->Width);
}

static void texReadPixels24(GSTEXTURE *texture, png_bytep *rowPointers, size_t size)
{
    struct pixel3
    {
        unsigned char r, g, b;
    };
    struct pixel3 *Pixels = (struct pixel3 *)texture->Mem;

    int i, j, k = 0;
    for (i = 0; i < texture->Height; i++) {
        for (j = 0; j < texture->Width; j++) {
            memcpy(&Pixels[k++], &rowPointers[i][4 * j], 3);
        }
    }
}

static void texReadPixels32(GSTEXTURE *texture, png_bytep *rowPointers, size_t size)
{
    struct pixel
    {
        unsigned char r, g, b, a;
    };
    struct pixel *Pixels = (struct pixel *)texture->Mem;

    int i, j, k = 0;
    for (i = 0; i < texture->Height; i++) {
        for (j = 0; j < texture->Width; j++) {
            memcpy(&Pixels[k], &rowPointers[i][4 * j], 3);
            Pixels[k++].a = rowPointers[i][4 * j + 3] >> 1;
        }
    }
}

static void texReadData(GSTEXTURE *texture, png_structp pngPtr, png_infop infoPtr,
                        void (*texPngReadPixels)(GSTEXTURE *texture, png_bytep *rowPointers, size_t size))
{
    int rowBytes = png_get_rowbytes(pngPtr, infoPtr);
    size_t size = gsKit_texture_size_ee(texture->Width, texture->Height, texture->PSM);
    texture->Mem = memalign(128, size);

    // failed allocation
    if (!texture->Mem) {
        LOG("TEXTURES PngReadData: Failed to allocate %d bytes\n", size);
        return;
    }

    png_bytep *rowPointers = calloc(texture->Height, sizeof(png_bytep));

    png_bytep allRows = malloc(rowBytes * texture->Height);
    if (!allRows) {
        free(rowPointers);
        LOG("TEXTURES PngReadData: Failed to allocate memory for PNG rows\n");
        return;
    }

    for (int row = 0; row < texture->Height; row++)
        rowPointers[row] = &allRows[row * rowBytes];

    png_read_image(pngPtr, rowPointers);

    texPngReadPixels(texture, rowPointers, size);

    free(allRows);
    free(rowPointers);

    png_read_end(pngPtr, NULL);
}

static int texLoadAll(GSTEXTURE *texture, const char *filePath, int texId, int archived)
{
    texPrepare(texture);
    png_structp pngPtr = NULL;
    png_infop infoPtr = NULL;
    png_voidp readData = NULL;
    png_rw_ptr readFunction = NULL;
    void *PngFileBufferPtr;
    void *pFileBuffer = NULL;
    if (archived) {
        pFileBuffer = getFileFromTar(filePath);
        if (!pFileBuffer) {
            return ERR_BAD_FILE;
        }
        PngFileBufferPtr = pFileBuffer;
        readData = &PngFileBufferPtr;
        readFunction = &texReadMemFunction;
    } else if (filePath) {
        int fd = open(filePath, O_RDONLY, 0);
        if (fd < 0)
            return ERR_BAD_FILE;

        int fileSize = lseek(fd, 0, SEEK_END);
        lseek(fd, 0, SEEK_SET);

        pFileBuffer = malloc(fileSize);
        if (pFileBuffer == NULL) {
            close(fd);
            return ERR_BAD_FILE; // There's no out of memory error...
        }

        if (read(fd, pFileBuffer, fileSize) != fileSize) {
            LOG("texLoadAll: failed to read file %s\n", filePath);
            free(pFileBuffer);
            close(fd);
            return ERR_BAD_FILE;
        }

        close(fd);

        PngFileBufferPtr = pFileBuffer;
        readData = &PngFileBufferPtr;
        readFunction = &texReadMemFunction;
    } else {
        if (texId == -1 || !internalDefault[texId].texture)
            return ERR_BAD_FILE;

        PngFileBufferPtr = internalDefault[texId].texture;
        readData = &PngFileBufferPtr;
        readFunction = &texReadMemFunction;
    }

    pngPtr = png_create_read_struct(PNG_LIBPNG_VER_STRING, (png_voidp)NULL, NULL, NULL);
    if (!pngPtr)
        return texEnd(pngPtr, infoPtr, pFileBuffer, ERR_READ_STRUCT);

    infoPtr = png_create_info_struct(pngPtr);
    if (!infoPtr)
        return texEnd(pngPtr, infoPtr, pFileBuffer, ERR_INFO_STRUCT);

    if (setjmp(png_jmpbuf(pngPtr)))
        return texEnd(pngPtr, infoPtr, pFileBuffer, ERR_SET_JMP);

    png_set_read_fn(pngPtr, readData, readFunction);

    unsigned int sigRead = 0;
    png_set_sig_bytes(pngPtr, sigRead);
    png_read_info(pngPtr, infoPtr);

    png_uint_32 pngWidth, pngHeight;
    int bitDepth, colorType, interlaceType;
    png_get_IHDR(pngPtr, infoPtr, &pngWidth, &pngHeight, &bitDepth, &colorType, &interlaceType, NULL, NULL);
    texture->Width = pngWidth;
    texture->Height = pngHeight;

    if (bitDepth == 16)
        png_set_strip_16(pngPtr);

    if (colorType == PNG_COLOR_TYPE_GRAY || bitDepth < 4) {
        png_set_expand(pngPtr);
        if (png_get_valid(pngPtr, infoPtr, PNG_INFO_tRNS))
            png_set_tRNS_to_alpha(pngPtr);
    }

    png_set_filler(pngPtr, 0xff, PNG_FILLER_AFTER);
    png_read_update_info(pngPtr, infoPtr);

    // clang-format off
    void (*texPngReadPixels)(GSTEXTURE * texture, png_bytep * rowPointers, size_t size);
    // clang-format on
    switch (png_get_color_type(pngPtr, infoPtr)) {
        case PNG_COLOR_TYPE_RGB_ALPHA:
            texture->PSM = GS_PSM_CT32;
            texPngReadPixels = &texReadPixels32;
            break;
        case PNG_COLOR_TYPE_RGB:
            texture->PSM = GS_PSM_CT24;
            texPngReadPixels = &texReadPixels24;
            break;
        case PNG_COLOR_TYPE_PALETTE:
            pngTexture.palette = NULL;
            pngTexture.numPalette = 0;
            pngTexture.trans = NULL;
            pngTexture.numTrans = 0;

            png_get_PLTE(pngPtr, infoPtr, &pngTexture.palette, &pngTexture.numPalette);
            png_get_tRNS(pngPtr, infoPtr, &pngTexture.trans, &pngTexture.numTrans, NULL);
            texture->ClutPSM = GS_PSM_CT32;

            if (bitDepth == 4) {
                texture->PSM = GS_PSM_T4;
                texture->Clut = memalign(128, gsKit_texture_size_ee(8, 2, GS_PSM_CT32));
                memset(texture->Clut, 0, gsKit_texture_size_ee(8, 2, GS_PSM_CT32));

                texPngReadPixels = &texReadPixels4;
            } else if (bitDepth == 8) {
                texture->PSM = GS_PSM_T8;
                texture->Clut = memalign(128, gsKit_texture_size_ee(16, 16, GS_PSM_CT32));
                memset(texture->Clut, 0, gsKit_texture_size_ee(16, 16, GS_PSM_CT32));

                texPngReadPixels = &texReadPixels8;
            } else
                return texEnd(pngPtr, infoPtr, pFileBuffer, ERR_BAD_DEPTH);
            break;
        default:
            return texEnd(pngPtr, infoPtr, pFileBuffer, ERR_BAD_DEPTH);
    }

    if (texSizeValidate(texture->Width, texture->Height, texture->PSM) < 0) {
        texFree(texture);

        return texEnd(pngPtr, infoPtr, pFileBuffer, ERR_BAD_DIMENSION);
    }

    texReadData(texture, pngPtr, infoPtr, texPngReadPixels);

    return texEnd(pngPtr, infoPtr, pFileBuffer, 0);
}

static int texLoad(GSTEXTURE *texture, const char *filePath, int archived)
{
    return texLoadAll(texture, filePath, -1, archived);
}

int texLoadInternal(GSTEXTURE *texture, int texId)
{
    return texLoadAll(texture, NULL, texId, 0);
}

int texDiscoverLoad(GSTEXTURE *texture, const char *path, int texId, int archived)
{
    char filePath[256];

    LOG("texDiscoverLoad(%s)\n", path);

    if (texId != -1)
        snprintf(filePath, sizeof(filePath), "%s%s.%s", path, internalDefault[texId].name, "png");
    else
        snprintf(filePath, sizeof(filePath), "%s.%s", path, "png");

    if (archived) {
        if (findTarEntry(filePath) != NULL) {
            return (texLoad(texture, filePath, archived) >= 0) ? 0 : ERR_BAD_FILE;
        }
    } else {
        int fd = open(filePath, O_RDONLY);
        if (fd > 0) {
            // File found, load it
            close(fd);
            return (texLoad(texture, filePath, archived) >= 0) ? 0 : ERR_BAD_FILE;
        }
    }

    return ERR_BAD_FILE;
}
