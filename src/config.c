/*
  Copyright 2009, Ifcaro & volca
  Licenced under Academic Free License version 3.0
  Review OpenUsbLd README & LICENSE files for further details.
*/

#include "include/common.h"
#include "include/config.h"
#include "include/util.h"
#include "include/ioman.h"
#include "include/sound.h"
#include "include/gui.h"
#include "include/menusys.h"
#include "include/lang.h"
#include "include/system.h"
#include "include/pad.h"
#include "include/renderman.h"
#include "include/themes.h"

#ifdef __DEBUG
#include "include/debug.h"
#endif

#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <fcntl.h>
#include <dirent.h>
#include <errno.h>

static u32 currentUID = 0;
static config_set_t configFiles[CONFIG_INDEX_COUNT];
static char legacyNetConfigPath[256] = "mc?:SYS-CONF/IPCONFIG.DAT";
static const char *configFilenames[CONFIG_INDEX_COUNT] = {
    "conf_wopl.cfg",
    "conf_last.cfg",
    "conf_apps.cfg",
    "conf_network.cfg",
    "conf_game.cfg",
};

char *gBaseMCDir;

static int strToColor(const char *string, unsigned char *color)
{
    int cnt = 0, n = 0;
    color[0] = 0;
    color[1] = 0;
    color[2] = 0;

    if (!string || !*string)
        return 0;
    if (string[0] != '#')
        return 0;

    string++;

    while (*string) {
        int fh = fromHex(*string);
        if (fh >= 0) {
            color[n] = color[n] * 16 + fh;
        } else {
            break;
        }

        // Two characters per color
        if (cnt == 1) {
            cnt = 0;
            n++;
        } else {
            cnt++;
        }

        string++;
    }

    return 1;
}

/// true if given a whitespace character
int isWS(char c)
{
    return c == ' ' || c == '\t';
}

static int splitAssignment(char *line, char *key, size_t keymax, char *val, size_t valmax)
{
    // skip whitespace
    for (; isWS(*line); ++line)
        ;

    // find "=".
    // If found, the text before is key, after is val.
    // Otherwise malformed string is encountered

    char *eqpos = strchr(line, '=');

    if (eqpos) {
        // copy the name and the value
        size_t keylen = MIN(keymax, eqpos - line);

        strncpy(key, line, keylen);

        eqpos++;

        size_t vallen = MIN(valmax, strlen(line) - (eqpos - line));
        strncpy(val, eqpos, vallen);
    }

    return (int)eqpos;
}

static int parsePrefix(char *line, char *prefix)
{
    // find "=".
    // If found, the text before is key, after is val.
    // Otherwise malformed string is encountered
    char *colpos = strchr(line, ':');

    if (colpos && colpos != line) {
        // copy the name and the value
        strncpy(prefix, line, colpos - line);
        prefix[colpos - line] = 0;

        return 1;
    } else {
        return 0;
    }
}

static int configKeyValidate(const char *key)
{
    if (strlen(key) == 0)
        return 0;

    return !strchr(key, '=');
}

static struct config_value_t *allocConfigItem(const char *key, const char *val)
{
    struct config_value_t *it = (struct config_value_t *)malloc(sizeof(struct config_value_t));
    strncpy(it->key, key, sizeof(it->key));
    it->key[sizeof(it->key) - 1] = '\0';
    strncpy(it->val, val, sizeof(it->val));
    it->val[sizeof(it->val) - 1] = '\0';
    it->next = NULL;

    return it;
}

/// Low level key addition. Does not check for uniqueness.
static void addConfigValue(config_set_t *configSet, const char *key, const char *val)
{
    if (!configSet->tail) {
        configSet->head = allocConfigItem(key, val);
        configSet->tail = configSet->head;
    } else {
        configSet->tail->next = allocConfigItem(key, val);
        configSet->tail = configSet->tail->next;
    }
}

static struct config_value_t *getConfigItemForName(config_set_t *configSet, const char *name)
{
    struct config_value_t *val = configSet->head;

    while (val) {
        if (strncmp(val->key, name, sizeof(val->key)) == 0)
            break;

        val = val->next;
    }

    return val;
}

static char cfgDevice[128];

static int lscstatus = CONFIG_ALL;
static int lscret = 0;

int configCheckLoadConfigBDM(int types)
{
    char path[64];
    int value;

    // check USB
    if (bdmFindPartition(path, "conf_wopl.cfg", 0)) {
        configEnd();
        configInit(path);
        value = configReadMulti(types);
        config_set_t *configOPL = configGetByType(CONFIG_OPL);
        configSetInt(configOPL, CONFIG_OPL_BDM_MODE, START_MODE_AUTO);
        return value;
    }

    return 0;
}

int configCheckLoadConfigHDD(int types)
{
    int value;
    char path[64];

    hddLoadModules();
    hddLoadSupportModules();

    snprintf(path, sizeof(path), "%sconf_wopl.cfg", gHDDPrefix);
    value = open(path, O_RDONLY);
    if (value >= 0) {
        close(value);
        configEnd();
        configInit(gHDDPrefix);
        value = configReadMulti(types);
        config_set_t *configOPL = configGetByType(CONFIG_OPL);
        configSetInt(configOPL, CONFIG_OPL_HDD_MODE, START_MODE_AUTO);
        return value;
    }

    return 0;
}

// When this function is called, the current device for loading/saving config is the memory card.
static int tryAlternateDevice(int types)
{
    char pwd[8];
    int value;
    DIR *dir;

    getcwd(pwd, sizeof(pwd));

    // First, try the device that OPL booted from.
    if (!strncmp(pwd, "mass", 4) && (pwd[4] == ':' || pwd[5] == ':')) {
        if ((value = configCheckLoadConfigBDM(types)) != 0)
            return value;
    } else if (!strncmp(pwd, "hdd", 3) && (pwd[3] == ':' || pwd[4] == ':')) {
        if ((value = configCheckLoadConfigHDD(types)) != 0)
            return value;
    }

    // Config was not found on the boot device. Check all supported devices.
    //  Check USB device
    if ((value = configCheckLoadConfigBDM(types)) != 0)
        return value;
    // Check HDD
    if ((value = configCheckLoadConfigHDD(types)) != 0)
        return value;

    // At this point, the user has no loadable config files on any supported device, so try to find a device to save on.
    // We don't want to get users into alternate mode for their very first launch of OPL (i.e no config file at all, but still want to save on MC)
    // Check for a memory card inserted.
    if (sysCheckMC() >= 0) {
        configPrepareNotifications(gBaseMCDir);
        showCfgPopup = 0;
        return 0;
    }
    // No memory cards? Try a USB device...
    dir = opendir("mass0:");
    if (dir != NULL) {
        closedir(dir);
        configEnd();
        configInit("mass0:");
    } else {
        // No? Check if the save location on the HDD is available.
        dir = opendir(gHDDPrefix);
        if (dir != NULL) {
            closedir(dir);
            configEnd();
            configInit(gHDDPrefix);
        }
    }
    showCfgPopup = 0;

    return 0;
}

void loadConfig()
{
    int value, themeID = -1, langID = -1;
    const char *temp;
    int result = configReadMulti(lscstatus);

    if (lscstatus & CONFIG_OPL) {
        if (!(result & CONFIG_OPL)) {
            result = tryAlternateDevice(lscstatus);
        }

        if (result & CONFIG_OPL) {
            config_set_t *configOPL = configGetByType(CONFIG_OPL);

            configGetInt(configOPL, CONFIG_OPL_SCROLLING, &gScrollSpeed);
            configGetColor(configOPL, CONFIG_OPL_BGCOLOR, gDefaultBgColor);
            configGetColor(configOPL, CONFIG_OPL_TEXTCOLOR, gDefaultTextColor);
            configGetColor(configOPL, CONFIG_OPL_UI_TEXTCOLOR, gDefaultUITextColor);
            configGetColor(configOPL, CONFIG_OPL_SEL_TEXTCOLOR, gDefaultSelTextColor);
            configGetColor(configOPL, CONFIG_OPL_PLAS_BLEND_COLOR, gDefaultPlasmaBlendColor);
            configGetInt(configOPL, CONFIG_OPL_ENABLE_NOTIFICATIONS, &gEnableNotifications);
            configGetInt(configOPL, CONFIG_OPL_ENABLE_COVERART, &gEnableArt);
            configGetInt(configOPL, CONFIG_OPL_ENABLE_ARCHIVEDART, &gEnableArchivedArt);
            configGetInt(configOPL, CONFIG_OPL_WIDESCREEN, &gWideScreen);

            if (!(getKeyPressed(KEY_TRIANGLE) && getKeyPressed(KEY_CROSS))) {
                configGetInt(configOPL, CONFIG_OPL_VMODE, &gVMode);
            } else {
                LOG("--- Select held at boot - setting Video Mode to Auto ---\n");
                gVMode = 0;
                configSetInt(configOPL, CONFIG_OPL_VMODE, gVMode);
            }

            configGetInt(configOPL, CONFIG_OPL_XOFF, &gXOff);
            configGetInt(configOPL, CONFIG_OPL_YOFF, &gYOff);
            configGetInt(configOPL, CONFIG_OPL_OVERSCAN, &gOverscan);

            configGetInt(configOPL, CONFIG_OPL_BDM_CACHE, &bdmCacheSize);
            configGetInt(configOPL, CONFIG_OPL_HDD_CACHE, &hddCacheSize);
            configGetInt(configOPL, CONFIG_OPL_SMB_CACHE, &smbCacheSize);

            if (configGetStr(configOPL, CONFIG_OPL_THEME, &temp))
                themeID = thmFindGuiID(temp);

            if (configGetStr(configOPL, CONFIG_OPL_LANGUAGE, &temp))
                langID = lngFindGuiID(temp);

            if (configGetInt(configOPL, CONFIG_OPL_SWAP_SEL_BUTTON, &value))
                gSelectButton = value == 0 ? KEY_CIRCLE : KEY_CROSS;

            configGetInt(configOPL, CONFIG_OPL_XSENSITIVITY, &gXSensitivity);
            configGetInt(configOPL, CONFIG_OPL_YSENSITIVITY, &gYSensitivity);
            configGetInt(configOPL, CONFIG_OPL_DISABLE_DEBUG, &gEnableDebug);
            configGetInt(configOPL, CONFIG_OPL_BDM_DEBUG, &gBDMDebug);
            configGetInt(configOPL, CONFIG_OPL_PS2LOGO, &gPS2Logo);
            configGetInt(configOPL, CONFIG_OPL_HDD_GAME_LIST_CACHE, &gHDDGameListCache);
            configGetStrCopy(configOPL, CONFIG_OPL_EXIT_PATH, gExitPath, sizeof(gExitPath));
            configGetInt(configOPL, CONFIG_OPL_AUTO_SORT, &gAutosort);
            configGetInt(configOPL, CONFIG_OPL_AUTO_REFRESH, &gAutoRefresh);
            configGetInt(configOPL, CONFIG_OPL_DEFAULT_DEVICE, &gDefaultDevice);
            configGetInt(configOPL, CONFIG_OPL_ENABLE_WRITE, &gEnableWrite);
            configGetInt(configOPL, CONFIG_OPL_HDD_SPINDOWN, &gHDDSpindown);
            configGetStrCopy(configOPL, CONFIG_OPL_MMCE_PREFIX, gMMCEPrefix, sizeof(gMMCEPrefix));
            configGetStrCopy(configOPL, CONFIG_OPL_BDM_PREFIX, gBDMPrefix, sizeof(gBDMPrefix));
            configGetStrCopy(configOPL, CONFIG_OPL_ETH_PREFIX, gETHPrefix, sizeof(gETHPrefix));
            configGetInt(configOPL, CONFIG_OPL_REMEMBER_LAST, &gRememberLastPlayed);
            configGetInt(configOPL, CONFIG_OPL_AUTOSTART_LAST, &gAutoStartLastPlayed);
            configGetInt(configOPL, CONFIG_OPL_BDM_MODE, &gBDMStartMode);
            configGetInt(configOPL, CONFIG_OPL_HDD_MODE, &gHDDStartMode);
            configGetInt(configOPL, CONFIG_OPL_ETH_MODE, &gETHStartMode);
            configGetInt(configOPL, CONFIG_OPL_APP_MODE, &gAPPStartMode);
            configGetInt(configOPL, CONFIG_OPL_FAV_MODE, &gFAVStartMode);
            configGetInt(configOPL, CONFIG_OPL_MMCE_MODE, &gMMCEStartMode);
            configGetInt(configOPL, CONFIG_OPL_MMCE_SLOT, &gMMCESlot);
            configGetInt(configOPL, CONFIG_OPL_MMCEIGR_SLOT, &gMMCEIGRSlot);
#ifdef __DEBUG
            configGetInt(configOPL, CONFIG_OPL_MMCE_GAMEID, &gMMCEEnableGameID);
#endif
            configGetInt(configOPL, CONFIG_OPL_MMCE_WAIT_CYCLES, &gMMCEAckWaitCycles);
            configGetInt(configOPL, CONFIG_OPL_MMCE_USE_ALARMS, &gMMCEUseAlarms);
            configGetInt(configOPL, CONFIG_OPL_ENABLE_ILINK, &gEnableILK);
            configGetInt(configOPL, CONFIG_OPL_ENABLE_MX4SIO, &gEnableMX4SIO);
            configGetInt(configOPL, CONFIG_OPL_ENABLE_BDMHDD, &gEnableBdmHDD);
            configGetInt(configOPL, CONFIG_OPL_SFX, &gEnableSFX);
            configGetInt(configOPL, CONFIG_OPL_BOOT_SND, &gEnableBootSND);
            configGetInt(configOPL, CONFIG_OPL_BGM, &gEnableBGM);
            configGetInt(configOPL, CONFIG_OPL_SFX_VOLUME, &gSFXVolume);
            configGetInt(configOPL, CONFIG_OPL_BOOT_SND_VOLUME, &gBootSndVolume);
            configGetInt(configOPL, CONFIG_OPL_BGM_VOLUME, &gBGMVolume);
            configGetStrCopy(configOPL, CONFIG_OPL_DEFAULT_BGM_PATH, gDefaultBGMPath, sizeof(gDefaultBGMPath));
        }
    }

    if (lscstatus & CONFIG_NETWORK) {
        if (!(result & CONFIG_NETWORK)) {
            result = tryAlternateDevice(lscstatus);
        }

        if (result & CONFIG_NETWORK) {
            config_set_t *configNet = configGetByType(CONFIG_NETWORK);

            configGetInt(configNet, CONFIG_NET_ETH_LINKM, &gETHOpMode);

            configGetInt(configNet, CONFIG_NET_PS2_DHCP, &ps2_ip_use_dhcp);
            configGetInt(configNet, CONFIG_NET_SMB_NBNS, &gPCShareAddressIsNetBIOS);
            configGetStrCopy(configNet, CONFIG_NET_SMB_NB_ADDR, gPCShareNBAddress, sizeof(gPCShareNBAddress));

            if (configGetStr(configNet, CONFIG_NET_SMB_IP_ADDR, &temp))
                sscanf(temp, "%d.%d.%d.%d", &pc_ip[0], &pc_ip[1], &pc_ip[2], &pc_ip[3]);

            configGetInt(configNet, CONFIG_NET_SMB_PORT, &gPCPort);

            configGetStrCopy(configNet, CONFIG_NET_SMB_SHARE, gPCShareName, sizeof(gPCShareName));
            configGetStrCopy(configNet, CONFIG_NET_SMB_USER, gPCUserName, sizeof(gPCUserName));
            configGetStrCopy(configNet, CONFIG_NET_SMB_PASSW, gPCPassword, sizeof(gPCPassword));

            if (configGetStr(configNet, CONFIG_NET_PS2_IP, &temp))
                sscanf(temp, "%d.%d.%d.%d", &ps2_ip[0], &ps2_ip[1], &ps2_ip[2], &ps2_ip[3]);
            if (configGetStr(configNet, CONFIG_NET_PS2_NETM, &temp))
                sscanf(temp, "%d.%d.%d.%d", &ps2_netmask[0], &ps2_netmask[1], &ps2_netmask[2], &ps2_netmask[3]);
            if (configGetStr(configNet, CONFIG_NET_PS2_GATEW, &temp))
                sscanf(temp, "%d.%d.%d.%d", &ps2_gateway[0], &ps2_gateway[1], &ps2_gateway[2], &ps2_gateway[3]);
            if (configGetStr(configNet, CONFIG_NET_PS2_DNS, &temp))
                sscanf(temp, "%d.%d.%d.%d", &ps2_dns[0], &ps2_dns[1], &ps2_dns[2], &ps2_dns[3]);

            configGetStrCopy(configNet, CONFIG_NET_NBD_DEFAULT_EXPORT, gExportName, sizeof(gExportName));
        }
    }

    configApply(themeID, langID, 0);

    lscret = result;
    lscstatus = 0;
    showCfgPopup = 1;
}

static int trySaveConfigBDM(int types)
{
    char path[64];

    // check USB
    if (bdmFindPartition(path, "conf_wopl.cfg", 1)) {
        configSetMove(path);
        return configWriteMulti(types);
    }

    return -ENOENT;
}

static int trySaveConfigHDD(int types)
{
    hddLoadModules();
    // Check that the formatted & usable HDD is connected.
    if (hddCheck() == 0) {
        configSetMove(gHDDPrefix);
        return configWriteMulti(types);
    }

    return -ENOENT;
}

static int trySaveConfigMC(int types)
{
    configSetMove(NULL);
    return configWriteMulti(types);
}

static int trySaveAlternateDevice(int types)
{
    char pwd[8];
    int value;

    getcwd(pwd, sizeof(pwd));

    // First, try the device that OPL booted from.
    if (!strncmp(pwd, "mass", 4) && (pwd[4] == ':' || pwd[5] == ':')) {
        if ((value = trySaveConfigBDM(types)) > 0)
            return value;
    } else if (!strncmp(pwd, "hdd", 3) && (pwd[3] == ':' || pwd[4] == ':')) {
        if ((value = trySaveConfigHDD(types)) > 0)
            return value;
    }

    // Config was not saved to the boot device. Try all supported devices.
    // Try memory cards
    if (sysCheckMC() >= 0) {
        if ((value = trySaveConfigMC(types)) > 0)
            return value;
    }
    // Try a USB device
    if ((value = trySaveConfigBDM(types)) > 0)
        return value;
    // Try the HDD
    if ((value = trySaveConfigHDD(types)) > 0)
        return value;

    // We tried everything, but...
    return 0;
}

static void saveConfig()
{
    char temp[256];

    if (lscstatus & CONFIG_OPL) {
        config_set_t *configOPL = configGetByType(CONFIG_OPL);
        configSetInt(configOPL, CONFIG_OPL_SCROLLING, gScrollSpeed);
        configSetStr(configOPL, CONFIG_OPL_THEME, thmGetValue());
        configSetStr(configOPL, CONFIG_OPL_LANGUAGE, lngGetValue());
        configSetColor(configOPL, CONFIG_OPL_BGCOLOR, gDefaultBgColor);
        configSetColor(configOPL, CONFIG_OPL_TEXTCOLOR, gDefaultTextColor);
        configSetColor(configOPL, CONFIG_OPL_UI_TEXTCOLOR, gDefaultUITextColor);
        configSetColor(configOPL, CONFIG_OPL_SEL_TEXTCOLOR, gDefaultSelTextColor);
        configSetColor(configOPL, CONFIG_OPL_PLAS_BLEND_COLOR, gDefaultPlasmaBlendColor);
        configSetInt(configOPL, CONFIG_OPL_ENABLE_NOTIFICATIONS, gEnableNotifications);
        configSetInt(configOPL, CONFIG_OPL_ENABLE_COVERART, gEnableArt);
        configSetInt(configOPL, CONFIG_OPL_ENABLE_ARCHIVEDART, gEnableArchivedArt);
        configSetInt(configOPL, CONFIG_OPL_WIDESCREEN, gWideScreen);
        configSetInt(configOPL, CONFIG_OPL_VMODE, gVMode);
        configSetInt(configOPL, CONFIG_OPL_XOFF, gXOff);
        configSetInt(configOPL, CONFIG_OPL_YOFF, gYOff);
        configSetInt(configOPL, CONFIG_OPL_OVERSCAN, gOverscan);
        configSetInt(configOPL, CONFIG_OPL_DISABLE_DEBUG, gEnableDebug);
        configSetInt(configOPL, CONFIG_OPL_BDM_DEBUG, gBDMDebug);
        configSetInt(configOPL, CONFIG_OPL_PS2LOGO, gPS2Logo);
        configSetInt(configOPL, CONFIG_OPL_HDD_GAME_LIST_CACHE, gHDDGameListCache);
        configSetStr(configOPL, CONFIG_OPL_EXIT_PATH, gExitPath);
        configSetInt(configOPL, CONFIG_OPL_AUTO_SORT, gAutosort);
        configSetInt(configOPL, CONFIG_OPL_AUTO_REFRESH, gAutoRefresh);
        configSetInt(configOPL, CONFIG_OPL_DEFAULT_DEVICE, gDefaultDevice);
        configSetInt(configOPL, CONFIG_OPL_ENABLE_WRITE, gEnableWrite);
        configSetInt(configOPL, CONFIG_OPL_HDD_SPINDOWN, gHDDSpindown);
        configSetStr(configOPL, CONFIG_OPL_MMCE_PREFIX, gMMCEPrefix);
        configSetStr(configOPL, CONFIG_OPL_BDM_PREFIX, gBDMPrefix);
        configSetStr(configOPL, CONFIG_OPL_ETH_PREFIX, gETHPrefix);
        configSetInt(configOPL, CONFIG_OPL_REMEMBER_LAST, gRememberLastPlayed);
        configSetInt(configOPL, CONFIG_OPL_AUTOSTART_LAST, gAutoStartLastPlayed);
        configSetInt(configOPL, CONFIG_OPL_BDM_MODE, gBDMStartMode);
        configSetInt(configOPL, CONFIG_OPL_HDD_MODE, gHDDStartMode);
        configSetInt(configOPL, CONFIG_OPL_ETH_MODE, gETHStartMode);
        configSetInt(configOPL, CONFIG_OPL_APP_MODE, gAPPStartMode);
        configSetInt(configOPL, CONFIG_OPL_FAV_MODE, gFAVStartMode);
        configSetInt(configOPL, CONFIG_OPL_MMCE_MODE, gMMCEStartMode);
        configSetInt(configOPL, CONFIG_OPL_MMCE_SLOT, gMMCESlot);
        configSetInt(configOPL, CONFIG_OPL_MMCEIGR_SLOT, gMMCEIGRSlot);
#ifdef __DEBUG
        configSetInt(configOPL, CONFIG_OPL_MMCE_GAMEID, gMMCEEnableGameID);
#endif
        configSetInt(configOPL, CONFIG_OPL_MMCE_WAIT_CYCLES, gMMCEAckWaitCycles);
        configSetInt(configOPL, CONFIG_OPL_MMCE_USE_ALARMS, gMMCEUseAlarms);
        configSetInt(configOPL, CONFIG_OPL_BDM_CACHE, bdmCacheSize);
        configSetInt(configOPL, CONFIG_OPL_HDD_CACHE, hddCacheSize);
        configSetInt(configOPL, CONFIG_OPL_SMB_CACHE, smbCacheSize);
        configSetInt(configOPL, CONFIG_OPL_ENABLE_ILINK, gEnableILK);
        configSetInt(configOPL, CONFIG_OPL_ENABLE_MX4SIO, gEnableMX4SIO);
        configSetInt(configOPL, CONFIG_OPL_ENABLE_BDMHDD, gEnableBdmHDD);
        configSetInt(configOPL, CONFIG_OPL_SFX, gEnableSFX);
        configSetInt(configOPL, CONFIG_OPL_BOOT_SND, gEnableBootSND);
        configSetInt(configOPL, CONFIG_OPL_BGM, gEnableBGM);
        configSetInt(configOPL, CONFIG_OPL_SFX_VOLUME, gSFXVolume);
        configSetInt(configOPL, CONFIG_OPL_BOOT_SND_VOLUME, gBootSndVolume);
        configSetInt(configOPL, CONFIG_OPL_BGM_VOLUME, gBGMVolume);
        configSetStr(configOPL, CONFIG_OPL_DEFAULT_BGM_PATH, gDefaultBGMPath);
        configSetInt(configOPL, CONFIG_OPL_XSENSITIVITY, gXSensitivity);
        configSetInt(configOPL, CONFIG_OPL_YSENSITIVITY, gYSensitivity);

        configSetInt(configOPL, CONFIG_OPL_SWAP_SEL_BUTTON, gSelectButton == KEY_CIRCLE ? 0 : 1);
    }

    if (lscstatus & CONFIG_NETWORK) {
        config_set_t *configNet = configGetByType(CONFIG_NETWORK);

        snprintf(temp, sizeof(temp), "%d.%d.%d.%d", ps2_ip[0], ps2_ip[1], ps2_ip[2], ps2_ip[3]);
        configSetStr(configNet, CONFIG_NET_PS2_IP, temp);
        snprintf(temp, sizeof(temp), "%d.%d.%d.%d", ps2_netmask[0], ps2_netmask[1], ps2_netmask[2], ps2_netmask[3]);
        configSetStr(configNet, CONFIG_NET_PS2_NETM, temp);
        snprintf(temp, sizeof(temp), "%d.%d.%d.%d", ps2_gateway[0], ps2_gateway[1], ps2_gateway[2], ps2_gateway[3]);
        configSetStr(configNet, CONFIG_NET_PS2_GATEW, temp);
        snprintf(temp, sizeof(temp), "%d.%d.%d.%d", ps2_dns[0], ps2_dns[1], ps2_dns[2], ps2_dns[3]);
        configSetStr(configNet, CONFIG_NET_PS2_DNS, temp);

        configSetInt(configNet, CONFIG_NET_ETH_LINKM, gETHOpMode);
        configSetInt(configNet, CONFIG_NET_PS2_DHCP, ps2_ip_use_dhcp);
        configSetInt(configNet, CONFIG_NET_SMB_NBNS, gPCShareAddressIsNetBIOS);
        configSetStr(configNet, CONFIG_NET_SMB_NB_ADDR, gPCShareNBAddress);
        snprintf(temp, sizeof(temp), "%d.%d.%d.%d", pc_ip[0], pc_ip[1], pc_ip[2], pc_ip[3]);
        configSetStr(configNet, CONFIG_NET_SMB_IP_ADDR, temp);
        configSetInt(configNet, CONFIG_NET_SMB_PORT, gPCPort);
        configSetStr(configNet, CONFIG_NET_SMB_SHARE, gPCShareName);
        configSetStr(configNet, CONFIG_NET_SMB_USER, gPCUserName);
        configSetStr(configNet, CONFIG_NET_SMB_PASSW, gPCPassword);
    }

    char *path = configGetDir();
    if (!strncmp(path, "mc", 2)) {
        sbCheckMCFolder();
        configPrepareNotifications(gBaseMCDir);
    }

    lscret = configWriteMulti(lscstatus);
    if (lscret == 0)
        lscret = trySaveAlternateDevice(lscstatus);
    lscstatus = 0;
}


char *configGetDir(void)
{
    static char path[256];

    if (!strncmp(cfgDevice, "mc", 2))
        snprintf(path, sizeof(path), "mc%d:wOPL/", sbGetmcID() & 1);
    else
        snprintf(path, sizeof(path), "%s", cfgDevice);

    return path;
}

void configPrepareNotifications(char *prefix)
{
    snprintf(cfgDevice, sizeof(cfgDevice), prefix);
}

void configInit(char *prefix)
{
    char path[256];
    int i;

    if (prefix)
        snprintf(legacyNetConfigPath, sizeof(legacyNetConfigPath), "%s/IPCONFIG.DAT", prefix);
    else
        prefix = gBaseMCDir;

    for (i = 0; i < CONFIG_INDEX_COUNT; i++) {
        snprintf(path, sizeof(path), "%s/%s", prefix, configFilenames[i]);
        configAlloc(1 << i, &configFiles[i], path);
    }

    configPrepareNotifications(prefix);
}

void configSetMove(char *prefix)
{
    char path[256];
    int i;

    if (prefix)
        snprintf(legacyNetConfigPath, sizeof(legacyNetConfigPath), "%s/IPCONFIG.DAT", prefix);
    else
        prefix = gBaseMCDir;

    for (i = 0; i < CONFIG_INDEX_COUNT; i++) {
        snprintf(path, sizeof(path), "%s/%s", prefix, configFilenames[i]);
        configMove(&configFiles[i], path);
    }

    configPrepareNotifications(prefix);
}

void configEnd()
{
    int index = 0;
    while (index < CONFIG_INDEX_COUNT) {
        config_set_t *configSet = &configFiles[index];

        configClear(configSet);
        free(configSet->filename);
        configSet->filename = NULL;
        index++;
    }
}

config_set_t *configAlloc(int type, config_set_t *configSet, char *fileName)
{
    if (!configSet)
        configSet = (config_set_t *)malloc(sizeof(config_set_t));

    configSet->uid = ++currentUID;
    configSet->type = type;
    configSet->head = NULL;
    configSet->tail = NULL;
    if (fileName) {
        int length = strlen(fileName) + 1;
        configSet->filename = (char *)malloc(length * sizeof(char));
        memcpy(configSet->filename, fileName, length);
    } else
        configSet->filename = NULL;
    configSet->modified = 0;
    return configSet;
}

void configMove(config_set_t *configSet, const char *fileName)
{
    int length = strlen(fileName) + 1;
    configSet->filename = realloc(configSet->filename, length);
    memcpy(configSet->filename, fileName, length);
}

void configFree(config_set_t *configSet)
{
    configClear(configSet);
    free(configSet->filename);
    free(configSet);
}

config_set_t *configGetByType(int type)
{
    int index = 0;
    while (index < CONFIG_INDEX_COUNT) {
        config_set_t *configSet = &configFiles[index];

        if (configSet->type == type)
            return configSet;
        index++;
    }
    return NULL;
}

int configSetStr(config_set_t *configSet, const char *key, const char *value)
{
    if (!configKeyValidate(key))
        return 0;

    struct config_value_t *it = getConfigItemForName(configSet, key);

    if (it) {
        if (strncmp(it->val, value, sizeof(it->val)) != 0) {
            strncpy(it->val, value, sizeof(it->val));
            it->val[sizeof(it->val) - 1] = '\0';
            if (it->key[0] != '#')
                configSet->modified = 1;
        }
    } else {
        addConfigValue(configSet, key, value);
        if (key[0] != '#')
            configSet->modified = 1;
    }

    return 1;
}

// sets the value to point to the value str in the config. Do not overwrite - it will overwrite the string in config
int configGetStr(config_set_t *configSet, const char *key, const char **value)
{
    if (!configKeyValidate(key))
        return 0;

    struct config_value_t *it = getConfigItemForName(configSet, key);

    if (it) {
        *value = it->val;
        return 1;
    } else
        return 0;
}

int configGetStrCopy(config_set_t *configSet, const char *key, char *value, int length)
{
    const char *valref = NULL;
    if (configGetStr(configSet, key, &valref)) {
        strncpy(value, valref, length);
        value[length - 1] = '\0';
        return 1;
    } else {
        value[0] = '\0';
        return 0;
    }
}

int configSetInt(config_set_t *configSet, const char *key, const int value)
{
    char tmp[12];
    snprintf(tmp, sizeof(tmp), "%d", value);
    return configSetStr(configSet, key, tmp);
}

int configGetInt(config_set_t *configSet, const char *key, int *value)
{
    const char *valref = NULL;
    if (configGetStr(configSet, key, &valref)) {
        *value = atoi(valref);
        return 1;
    } else {
        return 0;
    }
}

int configSetColor(config_set_t *configSet, const char *key, unsigned char *color)
{
    char tmp[8];
    snprintf(tmp, sizeof(tmp), "#%02X%02X%02X", color[0], color[1], color[2]);
    return configSetStr(configSet, key, tmp);
}

int configGetColor(config_set_t *configSet, const char *key, unsigned char *color)
{
    const char *valref = NULL;
    if (configGetStr(configSet, key, &valref)) {
        strToColor(valref, color);
        return 1;
    } else {
        return 0;
    }
}

int configRemoveKey(config_set_t *configSet, const char *key)
{
    if (!configKeyValidate(key))
        return 0;

    struct config_value_t *val = configSet->head;
    struct config_value_t *prev = NULL;

    while (val) {
        if (strncmp(val->key, key, sizeof(val->key)) == 0) {
            if (key[0] != '#')
                configSet->modified = 1;

            if (val == configSet->tail)
                configSet->tail = prev;

            val = val->next;
            if (prev) {
                free(prev->next);
                prev->next = val;
            } else {
                free(configSet->head);
                configSet->head = val;
            }
        } else {
            prev = val;
            val = val->next;
        }
    }

    return 1;
}

static int configReadLegacyIP(void)
{
    config_set_t *configSet;
    char temp[16];

    int fd = sbOpenFile(legacyNetConfigPath, O_RDONLY);
    if (fd >= 0) {
        char ipconfig[256];
        int size = sbGetFileSize(fd);
        read(fd, &ipconfig, size);
        close(fd);

        sscanf(ipconfig, "%d.%d.%d.%d %d.%d.%d.%d %d.%d.%d.%d", &ps2_ip[0], &ps2_ip[1], &ps2_ip[2], &ps2_ip[3],
               &ps2_netmask[0], &ps2_netmask[1], &ps2_netmask[2], &ps2_netmask[3],
               &ps2_gateway[0], &ps2_gateway[1], &ps2_gateway[2], &ps2_gateway[3]);

        configSet = &configFiles[CONFIG_INDEX_NETWORK];

        snprintf(temp, sizeof(temp), "%d.%d.%d.%d", ps2_ip[0], ps2_ip[1], ps2_ip[2], ps2_ip[3]);
        configSetStr(configSet, CONFIG_NET_PS2_IP, temp);
        snprintf(temp, sizeof(temp), "%d.%d.%d.%d", ps2_netmask[0], ps2_netmask[1], ps2_netmask[2], ps2_netmask[3]);
        configSetStr(configSet, CONFIG_NET_PS2_NETM, temp);
        snprintf(temp, sizeof(temp), "%d.%d.%d.%d", ps2_gateway[0], ps2_gateway[1], ps2_gateway[2], ps2_gateway[3]);
        configSetStr(configSet, CONFIG_NET_PS2_GATEW, temp);
        // The legacy format has no setting for the DNS server, so duplicate the gateway address.
        configSetStr(configSet, CONFIG_NET_PS2_DNS, temp);

        return 1;
    }

    return 0;
}

// dst has to have 5 bytes space
void configGetDiscIDBinary(config_set_t *configSet, void *dst)
{
    memset(dst, 0, 5);

    const char *gid = NULL;
    if (configGetStr(configSet, CONFIG_ITEM_DNAS, &gid)) {
        // convert from hex to binary
        char *cdst = dst;
        int p = 0;
        while (*gid && p < 10) {
            int dv = -1;

            while (dv < 0 && *gid) // skip spaces, etc
                dv = fromHex(*(gid++));

            if (dv < 0)
                break;

            *cdst = *cdst * 16 + dv;
            if ((++p & 1) == 0)
                cdst++;
        }
    }
}

static int configReadFileBuffer(file_buffer_t *fileBuffer, config_set_t *configSet)
{
    char *line;
    unsigned int lineno = 0;

    char prefix[CONFIG_KEY_NAME_LEN];
    memset(prefix, 0, sizeof(prefix));

    while (sbReadFileBuffer(fileBuffer, &line)) {
        lineno++;

        char key[CONFIG_KEY_NAME_LEN], val[CONFIG_KEY_VALUE_LEN];
        memset(key, 0, sizeof(key));
        memset(val, 0, sizeof(val));

        if (splitAssignment(line, key, sizeof(key), val, sizeof(val))) {
            /* if the line does not start with whitespace,
             * the prefix ends and we have to reset it
             */
            if (!isWS(line[0]))
                memset(prefix, 0, sizeof(prefix));

            // insert config value
            if (prefix[0]) {
                // we have a prefix
                char composedKey[CONFIG_KEY_NAME_LEN];

                snprintf(composedKey, sizeof(composedKey), "%s_%s", prefix, key);
                configSetStr(configSet, composedKey, val);
            } else {
                configSetStr(configSet, key, val);
            }
        } else if (parsePrefix(line, prefix)) {
            // prefix is set, that's about it
        } else {
            LOG("CONFIG Malformed file '%s' line %d: '%s'\n", configSet->filename, lineno, line);
        }
    }
    configSet->modified = 0;
    return 1;
}

int configReadBuffer(config_set_t *configSet, const void *buffer, int size)
{
    int ret;
    file_buffer_t *fileBuffer = sbOpenFileBufferBuffer(0, buffer, size);
    if (!fileBuffer) {
        configSet->modified = 0;
        return 0;
    }

    ret = configReadFileBuffer(fileBuffer, configSet);

    sbCloseFileBuffer(fileBuffer);
    return ret;
}

int configRead(config_set_t *configSet)
{
    int ret;
    file_buffer_t *fileBuffer = sbOpenFileBuffer(configSet->filename, O_RDONLY, 0, 4096);
    if (!fileBuffer) {
        LOG("CONFIG No file %s.\n", configSet->filename);
        configSet->modified = 0;
        return 0;
    }

    ret = configReadFileBuffer(fileBuffer, configSet);

    sbCloseFileBuffer(fileBuffer);
    return ret;
}

int configWrite(config_set_t *configSet)
{
    if (configSet->modified) {
        file_buffer_t *fileBuffer = sbOpenFileBuffer(configSet->filename, O_WRONLY | O_CREAT | O_TRUNC, 0, 4096);
        if (fileBuffer) {
            char line[512];

            bgmMute();
            struct config_value_t *cur = configSet->head;
            while (cur) {
                if ((cur->key[0] != '\0') && (cur->key[0] != '#')) {
                    snprintf(line, sizeof(line), "%s=%s\r\n", cur->key, cur->val); // add windows CR+LF (0x0D 0x0A)
                    sbWriteFileBuffer(fileBuffer, line, strlen(line));
                }

                // and advance
                cur = cur->next;
            }

            sbCloseFileBuffer(fileBuffer);
            configSet->modified = 0;
            bgmUnMute();
            return 1;
        }
        return 0;
    }
    return 1;
}

void configClear(config_set_t *configSet)
{
    while (configSet->head) {
        struct config_value_t *cur = configSet->head;
        configSet->head = cur->next;

        free(cur);
    }

    configSet->head = NULL;
    configSet->tail = NULL;
    configSet->modified = 1;
}

int configReadMulti(int types)
{
    int result = 0, index;

    for (index = 0; index < CONFIG_INDEX_COUNT; index++) {
        config_set_t *configSet = &configFiles[index];

        if (configSet->type & types) {
            configClear(configSet);
            if (configRead(configSet))
                result |= configSet->type;
        }
    }

    // If the network configuration is to be loaded and one cannot be loaded, attempt to load from the legacy network config file.
    if ((types & CONFIG_NETWORK) && !(result & CONFIG_NETWORK))
        if (configReadLegacyIP())
            result |= CONFIG_NETWORK;

    return result;
}

int configWriteMulti(int types)
{
    int result = 0, index;

    for (index = 0; index < CONFIG_INDEX_COUNT; index++) {
        config_set_t *configSet = &configFiles[index];

        if (configSet->type & types)
            result += configWrite(configSet);
    }

    return result;
}

void configGetVMC(config_set_t *configSet, char *vmc, int length, int slot)
{
    char gkey[CONFIG_KEY_NAME_LEN];
    snprintf(gkey, sizeof(gkey), "%s_%d", CONFIG_ITEM_VMC, slot);
    configGetStrCopy(configSet, gkey, vmc, length);
}

void configSetVMC(config_set_t *configSet, const char *vmc, int slot)
{
    char gkey[CONFIG_KEY_NAME_LEN];
    if (vmc[0] == '\0') {
        configRemoveVMC(configSet, slot);
        return;
    }
    snprintf(gkey, sizeof(gkey), "%s_%d", CONFIG_ITEM_VMC, slot);
    configSetStr(configSet, gkey, vmc);
}

void configRemoveVMC(config_set_t *configSet, int slot)
{
    char gkey[CONFIG_KEY_NAME_LEN];
    snprintf(gkey, sizeof(gkey), "%s_%d", CONFIG_ITEM_VMC, slot);
    configRemoveKey(configSet, gkey);
}


void configApply(int themeID, int langID, int skipDeviceRefresh)
{
    if (gDefaultDevice < 0 || gDefaultDevice > MMCE_MODE)
        gDefaultDevice = APP_MODE;

    guiUpdateScrollSpeed();

    guiSetFrameHook(&menuUpdateHook);

    guiLock();
    int changed = rmSetMode(0);
    guiUnlock();
    if (changed) {
        bgmMute();
        // reinit the graphics...
        thmReloadScreenExtents();
        guiReloadScreenExtents();
    }

    // theme must be set after color, and lng after theme
    changed = thmSetGuiValue(themeID, changed);
    int langChanged = lngSetGuiValue(langID);

    guiUpdateScreenScale();

    // Check if we should refresh device support as well.
    if (skipDeviceRefresh == 0) {
        initAllSupport(0);

        for (int i = 0; i < MODE_COUNT; i++) {
            if (list_support[i].support == NULL)
                continue;

            moduleUpdateMenuInternal(&list_support[i], changed, langChanged);
        }
    } else {
        if (changed) {
            for (int i = 0; i < MODE_COUNT; i++) {
                if (list_support[i].support && list_support[i].subMenu)
                    submenuRebuildCache(list_support[i].subMenu);
            }
        }
    }

    bgmUnMute();

#ifdef __DEBUG
    debugApplyConfig();
#endif
}

int configLoad(int types)
{
    lscstatus = types;
    lscret = 0;

    guiHandleDeferedIO(&lscstatus, _l(_STR_LOADING_SETTINGS), IO_CUSTOM_SIMPLEACTION, &loadConfig);

    return lscret;
}

int configSave(int types, int showUI)
{
    char notification[128];
    lscstatus = types;
    lscret = 0;

    guiHandleDeferedIO(&lscstatus, _l(_STR_SAVING_SETTINGS), IO_CUSTOM_SIMPLEACTION, &saveConfig);

    if (showUI) {
        if (lscret) {
            char *path = configGetDir();
            if (path != NULL) {
                char *colpos = strchr(path, ':');
                if (colpos != NULL)
                    *(colpos + 1) = '\0';
            }

            snprintf(notification, sizeof(notification), _l(_STR_SETTINGS_SAVED), path);

            guiMsgBox(notification, 0, NULL);
        } else
            guiMsgBox(_l(_STR_ERROR_SAVING_SETTINGS), 0, NULL);
    }

    return lscret;
}