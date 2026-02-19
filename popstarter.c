
#include "include/opl.h"
#include "include/dialogs.h"
#include "include/gui.h"
#include "include/system.h"
#include "include/ioman.h"
#include "popstarter.h"
#include <stdio.h>
#include <string.h>

// Global POPSTARTER config
POPS_CONFIG popsConfig;

// POPSTARTER binaries embedded as resources
extern unsigned char pops_usb_bin[];
extern unsigned char pops_hdd_bin[];
extern unsigned char pops_smb_bin[];
extern int size_pops_usb_bin;
extern int size_pops_hdd_bin;
extern int size_pops_smb_bin;

// Initialize POPSTARTER
int popsInit(void)
{
    // Set default configuration
    popsSetDefaultConfig();

    // Load configuration if exists
    popsLoadConfig();

    return 0;
}

// Load POPSTARTER configuration
int popsLoadConfig(void)
{
    char path[256];
    config_set_t *configSet;

    snprintf(path, sizeof(path), "%s/POPSTARTER.cfg", gOPLPath);
    configSet = configAlloc(0, NULL, path);

    if (configRead(configSet)) {
        configGetInt(configSet, "enabled", &popsConfig.enabled);
        configGetInt(configSet, "resolution", (int *)&popsConfig.resolution);
        configGetInt(configSet, "aspectRatio", (int *)&popsConfig.aspectRatio);
        configGetInt(configSet, "quality", (int *)&popsConfig.quality);
        configGetInt(configSet, "enableScanlines", &popsConfig.visualOptions.enableScanlines);
        configGetInt(configSet, "scanlineIntensity", &popsConfig.visualOptions.scanlineIntensity);
        configGetInt(configSet, "bilinearFilter", &popsConfig.visualOptions.bilinearFilter);
        configGetInt(configSet, "enhanceTextures", &popsConfig.visualOptions.enhanceTextures);
        configGetInt(configSet, "smoothPolygons", &popsConfig.visualOptions.smoothPolygons);
        configGetStr(configSet, "configFile", popsConfig.configFile, sizeof(popsConfig.configFile));
    }

    configFree(configSet);
    return 1;
}

// Save POPSTARTER configuration
int popsSaveConfig(void)
{
    char path[256];
    config_set_t *configSet;

    snprintf(path, sizeof(path), "%s/POPSTARTER.cfg", gOPLPath);
    configSet = configAlloc(0, NULL, path);

    configSetInt(configSet, "enabled", popsConfig.enabled);
    configSetInt(configSet, "resolution", (int)popsConfig.resolution);
    configSetInt(configSet, "aspectRatio", (int)popsConfig.aspectRatio);
    configSetInt(configSet, "quality", (int)popsConfig.quality);
    configSetInt(configSet, "enableScanlines", popsConfig.visualOptions.enableScanlines);
    configSetInt(configSet, "scanlineIntensity", popsConfig.visualOptions.scanlineIntensity);
    configSetInt(configSet, "bilinearFilter", popsConfig.visualOptions.bilinearFilter);
    configSetInt(configSet, "enhanceTextures", popsConfig.visualOptions.enhanceTextures);
    configSetInt(configSet, "smoothPolygons", popsConfig.visualOptions.smoothPolygons);
    configSetStr(configSet, "configFile", popsConfig.configFile);

    configWrite(configSet);
    configFree(configSet);
    return 1;
}

// Set default POPSTARTER settings
void popsSetDefaultConfig(void)
{
    popsConfig.enabled = 1;
    popsConfig.resolution = POPS_RES_AUTO;
    popsConfig.aspectRatio = POPS_ASPECT_4_3;
    popsConfig.quality = POPS_QUALITY_BALANCED;
    popsConfig.visualOptions.enableScanlines = 0;
    popsConfig.visualOptions.scanlineIntensity = 50;
    popsConfig.visualOptions.bilinearFilter = 1;
    popsConfig.visualOptions.enhanceTextures = 0;
    popsConfig.visualOptions.smoothPolygons = 1;
    strcpy(popsConfig.configFile, "");
}

// Apply POPSTARTER resolution settings
int popsApplyResolutionSettings(void)
{
    char popsParms[256] = "";

    // Build parameters based on resolution settings
    switch (popsConfig.resolution) {
        case POPS_RES_NATIVE:
            strcat(popsParms, "--native");
            break;
        case POPS_RES_480P:
            strcat(popsParms, "--480p");
            break;
        case POPS_RES_720P:
            strcat(popsParms, "--720p");
            break;
        case POPS_RES_1080I:
            strcat(popsParms, "--1080i");
            break;
        case POPS_RES_AUTO:
            strcat(popsParms, "--auto");
            break;
    }

    // Add aspect ratio parameter
    switch (popsConfig.aspectRatio) {
        case POPS_ASPECT_4_3:
            strcat(popsParms, " --4:3");
            break;
        case POPS_ASPECT_16_9:
            strcat(popsParms, " --16:9");
            break;
        case POPS_ASPECT_AUTO:
            strcat(popsParms, " --aspect=auto");
            break;
    }

    // Add quality settings
    switch (popsConfig.quality) {
        case POPS_QUALITY_FAST:
            strcat(popsParms, " --quality=fast");
            break;
        case POPS_QUALITY_BALANCED:
            strcat(popsParms, " --quality=balanced");
            break;
        case POPS_QUALITY_SMOOTH:
            strcat(popsParms, " --quality=smooth");
            break;
    }

    // Add visual options
    if (popsConfig.visualOptions.enableScanlines) {
        char temp[32];
        sprintf(temp, " --scanlines=%d", popsConfig.visualOptions.scanlineIntensity);
        strcat(popsParms, temp);
    }

    if (popsConfig.visualOptions.bilinearFilter) {
        strcat(popsParms, " --filter");
    }

    if (popsConfig.visualOptions.enhanceTextures) {
        strcat(popsParms, " --enhance");
    }

    if (popsConfig.visualOptions.smoothPolygons) {
        strcat(popsParms, " --smooth");
    }

    // Save parameters to config file
    char path[256];
    snprintf(path, sizeof(path), "%s/POPSCONFIG.CFG", gOPLPath);

    FILE *file = fopen(path, "w");
    if (file) {
        fprintf(file, "%s\n", popsParms);
        fclose(file);
        return 1;
    }

    return 0;
}

// Launch game with POPSTARTER
int popsLaunchGame(const char *vcdPath, config_set_t *configSet)
{
    char popsBinary[256];
    char mountPoint[32];
    char bootParams[512];
    char *popsData = NULL;
    int popsSize = 0;

    // Apply resolution settings before launch
    popsApplyResolutionSettings();

    // Determine storage device and select appropriate binary
    if (strncmp(vcdPath, "mass:", 5) == 0) {
        // USB mode
        popsData = (char *)pops_usb_bin;
        popsSize = size_pops_usb_bin;
        strcpy(mountPoint, "mass0:/");
    } else if (strncmp(vcdPath, "hdd:", 4) == 0) {
        // HDD mode
        popsData = (char *)pops_hdd_bin;
        popsSize = size_pops_hdd_bin;
        strcpy(mountPoint, "hdd0:");
    } else if (strncmp(vcdPath, "smb:", 4) == 0) {
        // SMB mode
        popsData = (char *)pops_smb_bin;
        popsSize = size_pops_smb_bin;
        strcpy(mountPoint, "smb0:");
    } else {
        // Unsupported device
        return -1;
    }

    if (!popsData || popsSize <= 0) {
        // Binary not found
        return -2;
    }

    // Create temporary file for POPSTARTER binary
    snprintf(popsBinary, sizeof(popsBinary), "%s/POPSTARTER.ELF", gOPLPath);
    FILE *file = fopen(popsBinary, "wb");
    if (!file) {
        return -3;
    }

    // Write binary data
    fwrite(popsData, 1, popsSize, file);
    fclose(file);

    // Build boot parameters
    snprintf(bootParams, sizeof(bootParams),
             "%s %s --hdtvfix --resolution=%d --aspect=%d %s",
             popsBinary, vcdPath,
             popsConfig.resolution,
             popsConfig.aspectRatio,
             popsConfig.configFile);

    // Launch POPSTARTER
    int ret = oplExecELF(bootParams);

    // Clean up
    unlink(popsBinary);

    return ret;
}

// Check if a file is a valid PS1 VCD
int popsIsValidVCD(const char *path)
{
    // Simple check for file extension
    const char *ext = strrchr(path, '.');
    if (ext && (strcasecmp(ext, ".VCD") == 0 || strcasecmp(ext, ".BIN") == 0)) {
        return 1;
    }

    return 0;
}

// Update PS1 game compatibility database
int popsUpdateCompatibility(void)
{
    // This would connect to a compatibility database
    // and download the latest compatibility information
    // For now, just return success
    return 1;
}
