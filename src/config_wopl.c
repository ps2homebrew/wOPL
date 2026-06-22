/*
  Copyright 2026, KrahJohlito
  Licenced under Academic Free License version 3.0
  Review wOPL README & LICENSE files for further details.
*/

#include "include/common.h"
#include "include/config_wopl.h"
#include "include/ioman.h"
#include "include/util.h"
#include "include/gui.h"
#include "include/renderman.h"
#include "include/system.h"
#include "include/themes.h"
#include "include/lang.h"
#include "include/pad.h"
#include "include/sound.h"
#include "include/lwnbd.h"
#include "include/supportbase.h"
#include "include/bdmsupport.h"
#include "include/hddsupport.h"
#include "include/mmcesupport.h"
#include "include/config_migration.h" // DELETE_WITH_MIGRATION
#include "include/module.h"
#include "include/pathsupport.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <errno.h>
#include <dirent.h>
#include <unistd.h>

#ifdef __DEBUG
#include "include/debug.h"
#endif

#ifdef GSM
#include "include/pggsm.h"
#endif

#ifdef CHEAT
#include "include/cheatman.h"
#endif

#define WOPL_FILENAME     "wopl_settings.cfg"
#define WOPL_FILENAME_OLD "conf_wopl.cfg" // DELETE_WITH_MIGRATION

#define NET_FILENAME     "wopl_network.cfg"
#define NET_FILENAME_OLD "conf_network.cfg" // DELETE_WITH_MIGRATION

#define GAME_FILENAME     "wopl_global_game.cfg"
#define GAME_FILENAME_OLD "conf_game.cfg" // DELETE_WITH_MIGRATION

#define LAST_FILENAME "wopl_last_played.cfg"
#define BOOT_FILENAME "wopl_boot.cfg"

static char config_dir[128] = {0};
static char boot_dir[128] = {0};
static char last_played[256] = {0};

static char s_theme_name[128] = {0};
static char s_lang_name[128] = {0};

char gParentalLockPassword[256] = {0};

global_game_cfg_t gGlobalGameCfg = {0};

int gBDMFramesDelay = MENU_MIN_INACTIVE_FRAMES;
int gETHFramesDelay = MENU_MIN_INACTIVE_FRAMES;
int gHDDFramesDelay = MENU_MIN_INACTIVE_FRAMES;
int gMMCEFramesDelay = MENU_MIN_INACTIVE_FRAMES;
int gAPPFramesDelay = MENU_MIN_INACTIVE_FRAMES;
int gFAVFramesDelay = MENU_MIN_INACTIVE_FRAMES;

// ---------------------------------------------------------------------------
// Config error logging
// ---------------------------------------------------------------------------

static char log_label[256]; // file source label for entry logging

static void write_config_log(const char *path, const char *msg)
{
    char log_dir[256];
    if (config_dir[0]) {
        strncpy(log_dir, config_dir, sizeof(log_dir) - 1);
        log_dir[sizeof(log_dir) - 1] = '\0';
    } else if (path) {
        strncpy(log_dir, path, sizeof(log_dir) - 1);
        log_dir[sizeof(log_dir) - 1] = '\0';
        char *sep = strrchr(log_dir, '/');
        if (!sep)
            sep = strrchr(log_dir, '\\');
        if (sep)
            *(sep + 1) = '\0';
        else
            log_dir[0] = '\0';
    } else
        log_dir[0] = '\0';

    if (log_dir[0] == '\0')
        return;

    char log_path[256];
    snprintf(log_path, sizeof(log_path), "%sconfig_errors.log", log_dir);

    FILE *f = fopen(log_path, "a");
    if (!f)
        return;
    fputs(msg, f);
    fclose(f);
}

// Whole file syntax error.. FILE_IO (missing file) is normal and ignored
void log_config_error(const char *path, const config_t *cfg)
{
    if (config_error_type(cfg) != CONFIG_ERR_PARSE)
        return;

    const char *label = path ? path : "?";

    char msg[512];
    snprintf(msg, sizeof(msg), "[%s] line %d: %s\n", label, config_error_line(cfg), config_error_text(cfg));
    LOG("CONFIG: parse error %s", msg);
    write_config_log(path, msg);
}

// Per entry error.. key exists but is the wrong type for how we read it
static void log_config_entry(const config_setting_t *setting, const char *expected)
{
    if (!setting)
        return;

    const char *name = config_setting_name(setting);
    const char *label = log_label[0] ? log_label : "?";

    char msg[256];
    snprintf(msg, sizeof(msg), "[%s] line %d: '%s' wrong type, expected %s\n", label, config_setting_source_line(setting), name ? name : "?", expected);

    LOG("CONFIG: %s", msg);
    write_config_log(log_label[0] ? log_label : NULL, msg);
}

void cfgValidateBegin(const char *label)
{
    if (label) {
        strncpy(log_label, label, sizeof(log_label) - 1);
        log_label[sizeof(log_label) - 1] = '\0';
    } else
        log_label[0] = '\0';
}

void cfgValidateEnd(void)
{
    log_label[0] = '\0';
}

// Checked getters..
// present + right type -> writes *out, returns 1
// absent               -> leaves *out, returns 0  (silent.. same as before)
// present + wrong type -> logs the line, returns 0
int cfgGetInt(const config_t *cfg, const char *path, int *out)
{
    const config_setting_t *s = config_lookup(cfg, path);
    if (!s)
        return 0;
    int t = config_setting_type(s);
    if (t == CONFIG_TYPE_INT) {
        *out = config_setting_get_int(s);
        return 1;
    }
    if (t == CONFIG_TYPE_BOOL) {
        *out = config_setting_get_bool(s) ? 1 : 0;
        return 1;
    }
    log_config_entry(s, "integer");
    return 0;
}

int cfgGetStr(const config_t *cfg, const char *path, const char **out)
{
    const config_setting_t *s = config_lookup(cfg, path);
    if (!s)
        return 0;
    if (config_setting_type(s) == CONFIG_TYPE_STRING) {
        *out = config_setting_get_string(s);
        return 1;
    }
    log_config_entry(s, "string");
    return 0;
}

// For dynamic keys (value may legitimately be string or int)
// Call after both reads failed.. log only if the key actually exists
void cfgCheckExists(const config_t *cfg, const char *path, const char *expected)
{
    const config_setting_t *s = config_lookup(cfg, path);
    if (s)
        log_config_entry(s, expected);
}

// ---------------------------------------------------------------------------
// Shared helpers
// ---------------------------------------------------------------------------

void dnas_to_binary(const char *dnas, char *out, int out_size)
{
    memset(out, 0, out_size);
    if (!dnas || !dnas[0])
        return;

    const char *s = dnas;
    for (int i = 0; i < out_size && s[0] && s[1]; i++, s += 2) {
        int hi = fromHex(s[0]);
        int lo = fromHex(s[1]);
        if (hi < 0 || lo < 0)
            return;
        out[i] = (hi << 4) | lo;
    }
}

static void color_to_str(const unsigned char *color, char *out, size_t len)
{
    snprintf(out, len, "#%02X%02X%02X", color[0], color[1], color[2]);
}

static void str_to_color(const char *str, unsigned char *color)
{
    color[0] = color[1] = color[2] = 0;
    if (!str || str[0] != '#')
        return;

    str++;
    for (int i = 0; i < 3; i++) {
        int hi = fromHex(*str++);
        int lo = fromHex(*str++);
        if (hi < 0 || lo < 0)
            return;

        color[i] = (unsigned char)((hi << 4) | lo);
    }
}

static void ip_to_str(const int *ip, char *out, size_t len)
{
    snprintf(out, len, "%d.%d.%d.%d", ip[0], ip[1], ip[2], ip[3]);
}

static void str_to_ip(const char *str, int *ip)
{
    ip[0] = ip[1] = ip[2] = ip[3] = 0;
    if (str)
        sscanf(str, "%d.%d.%d.%d", &ip[0], &ip[1], &ip[2], &ip[3]);
}

static int lookup_int(config_t *cfg, const char *path, int def)
{
    int value;
    return cfgGetInt(cfg, path, &value) ? value : def;
}

static int lookup_bool(config_t *cfg, const char *path, int def)
{
    return lookup_int(cfg, path, def) != 0;
}

static const char *lookup_str(config_t *cfg, const char *path, const char *def)
{
    const char *value;
    return cfgGetStr(cfg, path, &value) ? value : def;
}

static config_setting_t *add_group(config_setting_t *parent, const char *name)
{
    return config_setting_add(parent, name, CONFIG_TYPE_GROUP);
}

static void set_int(config_setting_t *group, const char *name, int val)
{
    config_setting_t *setting = config_setting_add(group, name, CONFIG_TYPE_INT);
    if (setting)
        config_setting_set_int(setting, val);
}

static void set_bool(config_setting_t *group, const char *name, int val)
{
    config_setting_t *setting = config_setting_add(group, name, CONFIG_TYPE_BOOL);
    if (setting)
        config_setting_set_bool(setting, val ? CONFIG_TRUE : CONFIG_FALSE);
}

static void set_str(config_setting_t *group, const char *name, const char *val)
{
    config_setting_t *setting = config_setting_add(group, name, CONFIG_TYPE_STRING);
    if (setting)
        config_setting_set_string(setting, val ? val : "");
}

static void set_color(config_setting_t *group, const char *name, const unsigned char *color)
{
    char buf[8];
    color_to_str(color, buf, sizeof(buf));
    set_str(group, name, buf);
}

static void set_ip(config_setting_t *group, const char *name, const int *ip)
{
    char buf[16];
    ip_to_str(ip, buf, sizeof(buf));
    set_str(group, name, buf);
}

static int file_exists(const char *path)
{
    FILE *fd = fopen(path, "r");
    if (!fd)
        return 0;

    fclose(fd);
    return 1;
}

static int path_exists(const char *path)
{
    struct stat st;
    return path && stat(path, &st) == 0;
}

static void copy_str(char *dst, const char *src, size_t size)
{
    if (!size)
        return;

    strncpy(dst, src, size - 1);
    dst[size - 1] = '\0';
}

static void sanitize_frame_delays(void)
{
    if (gBDMFramesDelay < 0)
        gBDMFramesDelay = MENU_MIN_INACTIVE_FRAMES;
    if (gETHFramesDelay < 0)
        gETHFramesDelay = MENU_MIN_INACTIVE_FRAMES;
    if (gHDDFramesDelay < 0)
        gHDDFramesDelay = MENU_MIN_INACTIVE_FRAMES;
    if (gMMCEFramesDelay < 0)
        gMMCEFramesDelay = MENU_MIN_INACTIVE_FRAMES;
    if (gAPPFramesDelay < 0)
        gAPPFramesDelay = MENU_MIN_INACTIVE_FRAMES;
    if (gFAVFramesDelay < 0)
        gFAVFramesDelay = MENU_MIN_INACTIVE_FRAMES;
}

static void sanitize_pad_sensitivity(void)
{
    if (gXSensitivity < 0 || gXSensitivity > 2)
        gXSensitivity = 0;
    if (gYSensitivity < 0 || gYSensitivity > 2)
        gYSensitivity = 0;
}

static int config_path_has_device_prefix(const char *path, const char *prefix)
{
    size_t len;

    if (!path || !prefix)
        return 0;

    len = strlen(prefix);

    if (strncmp(path, prefix, len))
        return 0;

    path += len;

    while (*path >= '0' && *path <= '9')
        path++;

    return *path == ':';
}

static void prepare_config_root_modules(const char *path)
{
    if (!path || !path[0])
        return;

    // BDM/FAT roots: usbN:, mx4sioN:, ilinkN:, ataN:
    bdmLoadModulesForPath(path);

    // APA/PFS internal HDD root: hddN:
    if (config_path_has_device_prefix(path, "hdd")) {
        guiSetBootStatusIfActive("Loading HDD config root...");
        LOG("CONFIG: loading HDD modules for config root '%s'\n", path);
        hddLoadModules();
        hddLoadSupportModules();
        return;
    }

    // MMCE root: mmceN:
    if (config_path_has_device_prefix(path, "mmce")) {
        guiSetBootStatusIfActive("Loading MMCE config root...");
        LOG("CONFIG: loading MMCE modules for config root '%s'\n", path);
        mmceLoadModules();
        return;
    }
}

#define CONFIG_ROOT_READY_RETRIES 10
#define CONFIG_ROOT_READY_DELAY   3

static int wait_for_config_root_ready(const char *path)
{
    int i;

    for (i = 0; i < CONFIG_ROOT_READY_RETRIES; i++) {
        if (path_exists(path)) {
            if (i > 0)
                LOG("CONFIG: config root ready after %d retries '%s'\n", i, path);

            return 1;
        }

        delay(CONFIG_ROOT_READY_DELAY);
    }

    return 0;
}

static int normalise_true_config_dir(char *out, size_t out_len, const char *dir, int allowLegacyMass)
{
    int legacyLaunchPath;

    if (!out || !out_len || !dir || !dir[0]) {
        LOG("CONFIG: normalise failed invalid dir='%s'\n", dir ? dir : "(null)");
        return 0;
    }

    legacyLaunchPath = pathIsLegacyMassPath(dir);

    LOG("CONFIG: normalise dir='%s' allowLegacyMass=%d legacy=%d\n", dir, allowLegacyMass, legacyLaunchPath);

    if (legacyLaunchPath) {
        if (!allowLegacyMass) {
            LOG("CONFIG: rejecting legacy mass path '%s'\n", dir);
            return 0;
        }

        guiSetBootStatusIfActive("Loading legacy mass support...");
        bdmLoadModulesForLegacyMass();

        delay(8);

        copy_str(out, dir, out_len);
        pathNormaliseDir(out, out_len);

        LOG("CONFIG: using legacy launch config dir '%s'\n", out);

        return 1;
    } else {
        if (!pathResolveToTrue(out, out_len, dir)) {
            LOG("CONFIG: failed to resolve true config dir '%s'\n", dir);
            return 0;
        }

        LOG("CONFIG: resolved true config dir '%s' -> '%s'\n", dir, out);
    }

    pathNormaliseDir(out, out_len);

    LOG("CONFIG: normalised config dir='%s'\n", out);

    if (!pathIsDevicePath(out)) {
        LOG("CONFIG: rejected non-device config dir '%s'\n", out);
        return 0;
    }

    // Load only the modules required by this selected boot/config root before probing it.
    prepare_config_root_modules(out);

    // BDM modules may be loaded but the filesystem/device might not be ready..
    if (!wait_for_config_root_ready(out)) {
        LOG("CONFIG: config root not ready '%s'\n", out);
        return 0;
    }

    LOG("CONFIG: config root ready '%s'\n", out);

    return 1;
}

static int load_boot_config_from_dir(const char *launch_dir)
{
    char boot_root[128];
    char boot_path[256];
    char resolved_dir[128];
    const char *value;
    config_t cfg;
    int have_config_dir = 0;
    int legacy_boot_root = 0;

    if (!launch_dir || !launch_dir[0])
        return 0;

    // Prepare the boot/cwd root before trying to read wopl_boot.cfg from it
    // Legacy massN: is allowed here only because this path came from argv0/cwd
    if (!normalise_true_config_dir(boot_root, sizeof(boot_root), launch_dir, 1))
        return 0;

    copy_str(boot_dir, boot_root, sizeof(boot_dir));
    legacy_boot_root = pathIsLegacyMassPath(boot_root);

    if (!pathJoin(boot_path, sizeof(boot_path), boot_root, BOOT_FILENAME)) {
        LOG("CONFIG: failed to build boot cfg path from '%s'\n", boot_root);
        return 0;
    }

    LOG("CONFIG: boot root='%s' boot cfg='%s'\n", boot_root, boot_path);

    if (!file_exists(boot_path)) {
        LOG("CONFIG: no boot cfg at '%s', using boot root\n", boot_path);
        copy_str(config_dir, boot_root, sizeof(config_dir));
        return 1;
    }

    config_init(&cfg);

    if (!config_read_file(&cfg, boot_path)) {
        log_config_error(boot_path, &cfg);
        config_destroy(&cfg);
        return 0;
    }

    cfgValidateBegin(boot_path);

    if (!legacy_boot_root && cfgGetStr(&cfg, "boot.config_dir", &value)) {
        // boot.config_dir is user selected config root.. Do not accept legacy massN: here
        if (normalise_true_config_dir(resolved_dir, sizeof(resolved_dir), value, 0)) {
            copy_str(config_dir, resolved_dir, sizeof(config_dir));
            have_config_dir = 1;
        } else
            LOG("CONFIG: ignoring invalid boot.config_dir '%s'\n", value);
    } else if (legacy_boot_root)
        LOG("CONFIG: ignoring boot.config_dir while using legacy launch root '%s'\n", boot_root);

    if (!have_config_dir) {
        copy_str(config_dir, boot_root, sizeof(config_dir));
        have_config_dir = 1;
    }

    cfgValidateEnd();
    config_destroy(&cfg);

    LOG("CONFIG: boot config '%s' config_dir='%s'\n", boot_path, config_dir);

    return have_config_dir;
}

static int pick_default_config_dir(void)
{
    char dir[128];
    char true_dir[128];
    int mc;

    if (pathGetBootDir(dir, sizeof(dir))) {
        if (load_boot_config_from_dir(dir))
            return 1;

        if (normalise_true_config_dir(true_dir, sizeof(true_dir), dir, 1)) {
            copy_str(boot_dir, true_dir, sizeof(boot_dir));
            copy_str(config_dir, true_dir, sizeof(config_dir));
            LOG("CONFIG: using boot/cwd config_dir='%s'\n", config_dir);
            return 1;
        }
    }

    // Fallback only if no usable boot/cwd root exists
    // Prefer the MC slot that already has the wOPL config folder
    mc = sbCheckMC();

    if (mc >= 0) {
        snprintf(config_dir, sizeof(config_dir), "mc%c:%s/", mc, WOPL_CONFIG_NAME);
        copy_str(boot_dir, config_dir, sizeof(boot_dir));
        LOG("CONFIG: falling back to MC config_dir='%s'\n", config_dir);
        return 1;
    }

    return 0;
}

static int ensure_config_dir(void)
{
    const char *launch;
    char cwd[128];
    int ok;

    if (config_dir[0])
        return 1;

    launch = pathGetLaunchPath();

    cwd[0] = '\0';
    if (getcwd(cwd, sizeof(cwd)) == NULL)
        copy_str(cwd, "(getcwd failed)", sizeof(cwd));

    LOG("CONFIG: ensure_config_dir launch='%s' cwd='%s'\n", launch ? launch : "(null)", cwd);

    ok = pick_default_config_dir();

    LOG("CONFIG: ensure_config_dir result=%d config_dir='%s' boot_dir='%s'\n", ok, config_dir, boot_dir);

    return ok;
}

static int do_save_at_dir(const char *dir, const char *filename, void (*build)(config_setting_t *))
{
    char path[256];

    if (!dir || !dir[0])
        return 0;

    if (!pathJoin(path, sizeof(path), dir, filename))
        return 0;

    config_t cfg;
    config_init(&cfg);
    config_setting_t *root = config_root_setting(&cfg);

    build(root);

    int ok = config_write_file(&cfg, path);
    config_destroy(&cfg);

    if (!ok) {
        LOG("CONFIG: failed to write '%s'\n", path);
        return 0;
    }

    copy_str(config_dir, dir, sizeof(config_dir));
    LOG("CONFIG: saved to '%s'\n", path);

    return 1;
}

static int do_save(const char *filename, void (*build)(config_setting_t *))
{
    if (!ensure_config_dir())
        return 0;

    return do_save_at_dir(config_dir, filename, build);
}

// ---------------------------------------------------------------------------
// Bootstrap config (wopl_boot.cfg)
// ---------------------------------------------------------------------------

static void build_boot(config_setting_t *root)
{
    config_setting_t *group;

    group = add_group(root, "boot");
    set_str(group, "config_dir", config_dir);
}

static int save_boot_config(void)
{
    char path[256];
    config_t cfg;
    config_setting_t *root;
    int ok;

    if (!boot_dir[0] || !config_dir[0])
        return 1;

    if (pathIsLegacyMassPath(config_dir)) {
        LOG("CONFIG: not saving unresolved legacy boot config_dir '%s'\n", config_dir);
        return 1;
    }

    if (config_path_has_device_prefix(boot_dir, "mc") && !sbEnsureMCConfigFolder(boot_dir)) {
        LOG("CONFIG: failed to prepare MC boot folder '%s'\n", boot_dir);
        return 0;
    }

    if (!pathJoin(path, sizeof(path), boot_dir, BOOT_FILENAME))
        return 0;

    config_init(&cfg);
    root = config_root_setting(&cfg);
    build_boot(root);

    ok = config_write_file(&cfg, path);
    config_destroy(&cfg);

    if (!ok) {
        LOG("CONFIG: failed to write boot config '%s'\n", path);
        return 0;
    }

    LOG("CONFIG: saved boot config '%s'\n", path);

    return 1;
}

// ---------------------------------------------------------------------------
// wOPL config (wopl_settings.cfg)
// ---------------------------------------------------------------------------

const char *wOPLGetDir(void)
{
    return config_dir[0] ? config_dir : NULL;
}

const char *wOPLGetThemeName(void)
{
    return s_theme_name[0] ? s_theme_name : NULL;
}

const char *wOPLGetLanguageName(void)
{
    return s_lang_name[0] ? s_lang_name : NULL;
}

static void parse_display(config_t *cfg)
{
    gWideScreen = lookup_bool(cfg, "display.widescreen", gWideScreen);
    gVMode = lookup_int(cfg, "display.vmode", gVMode);
    gXOff = lookup_int(cfg, "display.x_offset", gXOff);
    gYOff = lookup_int(cfg, "display.y_offset", gYOff);
    gOverscan = lookup_int(cfg, "display.overscan", gOverscan);
    gScrollSpeed = lookup_int(cfg, "display.scroll_speed", gScrollSpeed);

    const char *color;
    if ((color = lookup_str(cfg, "display.bg_color", NULL)))
        str_to_color(color, gDefaultBgColor);
    if ((color = lookup_str(cfg, "display.text_color", NULL)))
        str_to_color(color, gDefaultTextColor);
    if ((color = lookup_str(cfg, "display.ui_text_color", NULL)))
        str_to_color(color, gDefaultUITextColor);
    if ((color = lookup_str(cfg, "display.sel_text_color", NULL)))
        str_to_color(color, gDefaultSelTextColor);
    if ((color = lookup_str(cfg, "display.plasma_blend_color", NULL)))
        str_to_color(color, gDefaultPlasmaBlendColor);
}

static void parse_ui(config_t *cfg, int *out_theme_id, int *out_lang_id)
{
    const char *theme_name = lookup_str(cfg, "ui.theme", NULL);
    if (theme_name) {
        copy_str(s_theme_name, theme_name, sizeof(s_theme_name));
        if (out_theme_id)
            *out_theme_id = thmFindGuiID(theme_name);
    }

    const char *lang_name = lookup_str(cfg, "ui.language", NULL);
    if (lang_name) {
        copy_str(s_lang_name, lang_name, sizeof(s_lang_name));
        if (out_lang_id)
            *out_lang_id = lngFindGuiID(lang_name);
    }

    const char *pwd = lookup_str(cfg, "ui.parental_lock_password", NULL);
    if (pwd)
        copy_str(gParentalLockPassword, pwd, sizeof(gParentalLockPassword));

    gSelectButton = lookup_bool(cfg, "ui.swap_button", 0) ? KEY_CROSS : KEY_CIRCLE;
    gXSensitivity = lookup_int(cfg, "ui.x_sensitivity", gXSensitivity);
    gYSensitivity = lookup_int(cfg, "ui.y_sensitivity", gYSensitivity);
    gEnableNotifications = lookup_bool(cfg, "ui.notifications", gEnableNotifications);
    gDiscEnableArt = lookup_bool(cfg, "ui.disc_art", gDiscEnableArt);
}

static void parse_audio(config_t *cfg)
{
    gEnableSFX = lookup_bool(cfg, "audio.sfx", gEnableSFX);
    gSFXVolume = lookup_int(cfg, "audio.sfx_volume", gSFXVolume);
    gEnableBootSND = lookup_bool(cfg, "audio.boot_sound", gEnableBootSND);
    gBootSndVolume = lookup_int(cfg, "audio.boot_volume", gBootSndVolume);
    gEnableBGM = lookup_bool(cfg, "audio.bgm", gEnableBGM);
    gBGMVolume = lookup_int(cfg, "audio.bgm_volume", gBGMVolume);

    const char *path = lookup_str(cfg, "audio.bgm_path", NULL);
    if (path)
        copy_str(gDefaultBGMPath, path, sizeof(gDefaultBGMPath));
}

static void parse_startup(config_t *cfg)
{
    gDefaultDevice = lookup_int(cfg, "startup.default_device", gDefaultDevice);
    gAutosort = lookup_bool(cfg, "startup.auto_sort", gAutosort);
    gAutoRefresh = lookup_bool(cfg, "startup.auto_refresh", gAutoRefresh);
    gRememberLastPlayed = lookup_bool(cfg, "startup.remember_last", gRememberLastPlayed);
    gAutoStartLastPlayed = lookup_bool(cfg, "startup.autostart_last", gAutoStartLastPlayed);
    gBDMStartMode = lookup_int(cfg, "startup.bdm_start_mode", gBDMStartMode);
    gHDDStartMode = lookup_int(cfg, "startup.hdd_start_mode", gHDDStartMode);
    gETHStartMode = lookup_int(cfg, "startup.eth_start_mode", gETHStartMode);
    gAPPStartMode = lookup_int(cfg, "startup.app_start_mode", gAPPStartMode);
    gFAVStartMode = lookup_int(cfg, "startup.fav_start_mode", gFAVStartMode);
    gMMCEStartMode = lookup_int(cfg, "startup.mmce_start_mode", gMMCEStartMode);

    const char *path = lookup_str(cfg, "startup.exit_path", NULL);
    if (path)
        copy_str(gExitPath, path, sizeof(gExitPath));
}

static void parse_devices(config_t *cfg)
{
    gEnableUSB = lookup_bool(cfg, "devices.usb_enabled", gEnableUSB);
    gEnableILK = lookup_bool(cfg, "devices.ilink_enabled", gEnableILK);
    gEnableMX4SIO = lookup_bool(cfg, "devices.mx4sio_enabled", gEnableMX4SIO);
    gEnableBdmHDD = lookup_bool(cfg, "devices.bdm_hdd_enabled", gEnableBdmHDD);
    bdmCacheSize = lookup_int(cfg, "devices.bdm_cache", bdmCacheSize);
    hddCacheSize = lookup_int(cfg, "devices.hdd_cache", hddCacheSize);
    smbCacheSize = lookup_int(cfg, "devices.smb_cache", smbCacheSize);
    gHDDSpindown = lookup_int(cfg, "devices.hdd_spindown", gHDDSpindown);
    gHDDGameListCache = lookup_bool(cfg, "devices.hdd_game_list_cache", gHDDGameListCache);
    gEnableWrite = lookup_bool(cfg, "devices.enable_write", gEnableWrite);
}

static void parse_paths(config_t *cfg)
{
    const char *path;

    if ((path = lookup_str(cfg, "paths.bdm_prefix", NULL)))
        copy_str(gBDMPrefix, path, sizeof(gBDMPrefix));

    if ((path = lookup_str(cfg, "paths.eth_prefix", NULL)))
        copy_str(gETHPrefix, path, sizeof(gETHPrefix));

    if ((path = lookup_str(cfg, "paths.mmce_prefix", NULL)))
        copy_str(gMMCEPrefix, path, sizeof(gMMCEPrefix));
}

static void parse_mmce(config_t *cfg)
{
    gMMCESlot = lookup_int(cfg, "mmce.slot", gMMCESlot);
    gMMCEIGRSlot = lookup_int(cfg, "mmce.igr_slot", gMMCEIGRSlot);
    gMMCEAckWaitCycles = lookup_int(cfg, "mmce.mmce_wait_cycles", gMMCEAckWaitCycles);
    gMMCEUseAlarms = lookup_bool(cfg, "mmce.use_alarms", gMMCEUseAlarms);
}

static void parse_debug(config_t *cfg)
{
    gEnableDebug = lookup_bool(cfg, "debug.enable_debug", gEnableDebug);
    gBDMDebug = lookup_bool(cfg, "debug.bdm_debug", gBDMDebug);
    gPS2Logo = lookup_bool(cfg, "debug.ps2_logo", gPS2Logo);
#ifdef __DEBUG
    gMMCEEnableGameID = lookup_bool(cfg, "debug.mmce_gameid", gMMCEEnableGameID);
#endif
}

static void parse_coverflow(config_t *cfg)
{
    gCoverflowCount = lookup_int(cfg, "coverflow.count", gCoverflowCount);
    gCoverflowCenterScale = lookup_int(cfg, "coverflow.center_scale", gCoverflowCenterScale);
    gCoverflowAnimSpeed = lookup_int(cfg, "coverflow.anim_speed", gCoverflowAnimSpeed);
    gCoverflowDimCovers = lookup_int(cfg, "coverflow.dim_covers", gCoverflowDimCovers);
}

static void parse_frames_delay(config_t *cfg)
{
    gAPPFramesDelay = lookup_int(cfg, "frames_delay.app_frames_delay", gAPPFramesDelay);
    gFAVFramesDelay = lookup_int(cfg, "frames_delay.fav_frames_delay", gFAVFramesDelay);
    gBDMFramesDelay = lookup_int(cfg, "frames_delay.bdm_frames_delay", gBDMFramesDelay);
    gETHFramesDelay = lookup_int(cfg, "frames_delay.eth_frames_delay", gETHFramesDelay);
    gHDDFramesDelay = lookup_int(cfg, "frames_delay.hdd_frames_delay", gHDDFramesDelay);
    gMMCEFramesDelay = lookup_int(cfg, "frames_delay.mmce_frames_delay", gMMCEFramesDelay);
}

static void build_opl(config_setting_t *root)
{
    config_setting_t *group;

    group = add_group(root, "display");
    set_bool(group, "widescreen", gWideScreen);
    set_int(group, "vmode", gVMode);
    set_int(group, "x_offset", gXOff);
    set_int(group, "y_offset", gYOff);
    set_int(group, "overscan", gOverscan);
    set_int(group, "scroll_speed", gScrollSpeed);
    set_color(group, "bg_color", gDefaultBgColor);
    set_color(group, "text_color", gDefaultTextColor);
    set_color(group, "ui_text_color", gDefaultUITextColor);
    set_color(group, "sel_text_color", gDefaultSelTextColor);
    set_color(group, "plasma_blend_color", gDefaultPlasmaBlendColor);

    group = add_group(root, "ui");
    set_str(group, "theme", thmGetValue());
    set_str(group, "language", lngGetValue());
    set_str(group, "parental_lock_password", gParentalLockPassword);
    set_bool(group, "swap_button", gSelectButton == KEY_CROSS);
    set_int(group, "x_sensitivity", gXSensitivity);
    set_int(group, "y_sensitivity", gYSensitivity);
    set_bool(group, "notifications", gEnableNotifications);
    set_bool(group, "disc_art", gDiscEnableArt);

    group = add_group(root, "audio");
    set_bool(group, "sfx", gEnableSFX);
    set_int(group, "sfx_volume", gSFXVolume);
    set_bool(group, "boot_sound", gEnableBootSND);
    set_int(group, "boot_volume", gBootSndVolume);
    set_bool(group, "bgm", gEnableBGM);
    set_int(group, "bgm_volume", gBGMVolume);
    set_str(group, "bgm_path", gDefaultBGMPath);

    group = add_group(root, "startup");
    set_int(group, "default_device", gDefaultDevice);
    set_bool(group, "auto_sort", gAutosort);
    set_bool(group, "auto_refresh", gAutoRefresh);
    set_bool(group, "remember_last", gRememberLastPlayed);
    set_bool(group, "autostart_last", gAutoStartLastPlayed);
    set_str(group, "exit_path", gExitPath);
    set_int(group, "bdm_start_mode", gBDMStartMode);
    set_int(group, "hdd_start_mode", gHDDStartMode);
    set_int(group, "eth_start_mode", gETHStartMode);
    set_int(group, "app_start_mode", gAPPStartMode);
    set_int(group, "fav_start_mode", gFAVStartMode);
    set_int(group, "mmce_start_mode", gMMCEStartMode);

    group = add_group(root, "devices");
    set_bool(group, "usb_enabled", gEnableUSB);
    set_bool(group, "ilink_enabled", gEnableILK);
    set_bool(group, "mx4sio_enabled", gEnableMX4SIO);
    set_bool(group, "bdm_hdd_enabled", gEnableBdmHDD);
    set_int(group, "bdm_cache", bdmCacheSize);
    set_int(group, "hdd_cache", hddCacheSize);
    set_int(group, "smb_cache", smbCacheSize);
    set_int(group, "hdd_spindown", gHDDSpindown);
    set_bool(group, "hdd_game_list_cache", gHDDGameListCache);
    set_bool(group, "enable_write", gEnableWrite);

    group = add_group(root, "paths");
    set_str(group, "bdm_prefix", gBDMPrefix);
    set_str(group, "eth_prefix", gETHPrefix);
    set_str(group, "mmce_prefix", gMMCEPrefix);

    group = add_group(root, "mmce");
    set_int(group, "slot", gMMCESlot);
    set_int(group, "igr_slot", gMMCEIGRSlot);
    set_int(group, "mmce_wait_cycles", gMMCEAckWaitCycles);
    set_bool(group, "use_alarms", gMMCEUseAlarms);

    group = add_group(root, "debug");
    set_bool(group, "enable_debug", gEnableDebug);
    set_bool(group, "bdm_debug", gBDMDebug);
    set_bool(group, "ps2_logo", gPS2Logo);
#ifdef __DEBUG
    set_bool(group, "mmce_gameid", gMMCEEnableGameID);
#endif

    group = add_group(root, "coverflow");
    set_int(group, "count", gCoverflowCount);
    set_int(group, "center_scale", gCoverflowCenterScale);
    set_int(group, "anim_speed", gCoverflowAnimSpeed);
    set_int(group, "dim_covers", gCoverflowDimCovers);

    group = add_group(root, "frames_delay");
    set_int(group, "app_frames_delay", gAPPFramesDelay);
    set_int(group, "fav_frames_delay", gFAVFramesDelay);
    set_int(group, "bdm_frames_delay", gBDMFramesDelay);
    set_int(group, "eth_frames_delay", gETHFramesDelay);
    set_int(group, "hdd_frames_delay", gHDDFramesDelay);
    set_int(group, "mmce_frames_delay", gMMCEFramesDelay);
}

static void parse_opl_cfg(config_t *cfg, int *out_theme_id, int *out_lang_id)
{
    parse_display(cfg);
    parse_ui(cfg, out_theme_id, out_lang_id);
    parse_audio(cfg);
    parse_startup(cfg);
    parse_devices(cfg);
    parse_paths(cfg);
    parse_mmce(cfg);
    parse_debug(cfg);
    parse_coverflow(cfg);
    parse_frames_delay(cfg);

    sanitize_frame_delays();
    sanitize_pad_sensitivity();
}

int wOPLLoad(int *out_theme_id, int *out_lang_id)
{
    char path[256];
    char old_path[256];
    config_t cfg;

    if (out_theme_id)
        *out_theme_id = 0;
    if (out_lang_id)
        *out_lang_id = 0;

    LOG("CONFIG_WOPL: enter config_dir='%s'\n", config_dir);

    if (!ensure_config_dir())
        return 0;

    // 1. Try new filename
    if (!pathJoin(path, sizeof(path), config_dir, WOPL_FILENAME))
        return 0;

    LOG("CONFIG_WOPL: trying '%s'\n", path);

    config_init(&cfg);
    if (config_read_file(&cfg, path)) {
        cfgValidateBegin(path);
        parse_opl_cfg(&cfg, out_theme_id, out_lang_id);
        cfgValidateEnd();
        config_destroy(&cfg);

        LOG("CONFIG_WOPL: loaded from '%s'\n", path);
        return 1;
    }

    LOG("CONFIG_WOPL: failed new config '%s'\n", path);
    log_config_error(path, &cfg);
    config_destroy(&cfg);

    // DELETE_WITH_MIGRATION
    // 2. Try old filename from the selected config root only.
    if (!pathJoin(old_path, sizeof(old_path), config_dir, WOPL_FILENAME_OLD))
        return 0;

    config_init(&cfg);
    int ok = 0;

    if (config_read_file(&cfg, old_path) && config_lookup(&cfg, "display") != NULL) {
        parse_opl_cfg(&cfg, out_theme_id, out_lang_id);
        ok = 1;
    }
    config_destroy(&cfg);

    if (!ok) {
        LOG("CONFIG_WOPL: old format detected at selected config root, attempting legacy migration\n");
        ok = cfgMigrateLegacyOPL(old_path, out_theme_id, out_lang_id);
    }

    if (!ok)
        return 0;

    if (wOPLSave()) {
        char bak[256];

        snprintf(bak, sizeof(bak), "%s.bak", old_path);
        rename(old_path, bak);
        LOG("CONFIG_WOPL: migrated to '%s'\n", WOPL_FILENAME);
    }

    return 1;
    // DELETE_WITH_MIGRATION
}

int wOPLSave(void)
{
    return do_save(WOPL_FILENAME, build_opl);
}

// ---------------------------------------------------------------------------
// Network config (wopl_network.cfg)
// ---------------------------------------------------------------------------

static void parse_net(config_t *cfg)
{
    const char *string;

    gETHOpMode = lookup_int(cfg, "eth.link_mode", gETHOpMode);

    ps2_ip_use_dhcp = lookup_bool(cfg, "ps2.dhcp", ps2_ip_use_dhcp);
    if ((string = lookup_str(cfg, "ps2.ip", NULL)))
        str_to_ip(string, ps2_ip);
    if ((string = lookup_str(cfg, "ps2.netmask", NULL)))
        str_to_ip(string, ps2_netmask);
    if ((string = lookup_str(cfg, "ps2.gateway", NULL)))
        str_to_ip(string, ps2_gateway);
    if ((string = lookup_str(cfg, "ps2.dns", NULL)))
        str_to_ip(string, ps2_dns);

    gPCShareAddressIsNetBIOS = lookup_bool(cfg, "smb.use_netbios", gPCShareAddressIsNetBIOS);
    if ((string = lookup_str(cfg, "smb.nb_address", NULL)))
        copy_str(gPCShareNBAddress, string, sizeof(gPCShareNBAddress));
    if ((string = lookup_str(cfg, "smb.ip", NULL)))
        str_to_ip(string, pc_ip);

    gPCPort = lookup_int(cfg, "smb.port", gPCPort);
    if ((string = lookup_str(cfg, "smb.share", NULL)))
        copy_str(gPCShareName, string, sizeof(gPCShareName));
    if ((string = lookup_str(cfg, "smb.username", NULL)))
        copy_str(gPCUserName, string, sizeof(gPCUserName));
    if ((string = lookup_str(cfg, "smb.password", NULL)))
        copy_str(gPCPassword, string, sizeof(gPCPassword));
    if ((string = lookup_str(cfg, "nbd.export", NULL)))
        copy_str(gExportName, string, sizeof(gExportName));
}

static void build_net(config_setting_t *root)
{
    config_setting_t *group;

    group = add_group(root, "eth");
    set_int(group, "link_mode", gETHOpMode);

    group = add_group(root, "ps2");
    set_bool(group, "dhcp", ps2_ip_use_dhcp);
    set_ip(group, "ip", ps2_ip);
    set_ip(group, "netmask", ps2_netmask);
    set_ip(group, "gateway", ps2_gateway);
    set_ip(group, "dns", ps2_dns);

    group = add_group(root, "smb");
    set_bool(group, "use_netbios", gPCShareAddressIsNetBIOS);
    set_str(group, "nb_address", gPCShareNBAddress);
    set_ip(group, "ip", pc_ip);
    set_int(group, "port", gPCPort);
    set_str(group, "share", gPCShareName);
    set_str(group, "username", gPCUserName);
    set_str(group, "password", gPCPassword);

    group = add_group(root, "nbd");
    set_str(group, "export", gExportName);
}

int wOPLNetLoad(void)
{
    LOG("CONFIG_NET: enter config_dir='%s'\n", config_dir);

    if (!ensure_config_dir())
        return 0;

    char path[256];
    char old_path[256];
    config_t cfg;

    if (!pathJoin(path, sizeof(path), config_dir, NET_FILENAME))
        return 0;

    LOG("CONFIG_NET: trying '%s'\n", path);

    // 1. Try new filename
    config_init(&cfg);
    if (config_read_file(&cfg, path)) {
        cfgValidateBegin(path);
        parse_net(&cfg);
        cfgValidateEnd();
        config_destroy(&cfg);
        LOG("CONFIG_NET: loaded from '%s'\n", path);
        return 1;
    }
    log_config_error(path, &cfg);
    config_destroy(&cfg);

    // DELETE_WITH_MIGRATION
    // 2. Try old filename.. migrate to new filename and delete old
    if (!pathJoin(old_path, sizeof(old_path), config_dir, NET_FILENAME_OLD))
        return 0;

    config_init(&cfg);
    int ok = 0;
    if (config_read_file(&cfg, old_path) && config_lookup(&cfg, "ps2") != NULL) {
        parse_net(&cfg);
        ok = 1;
    }
    config_destroy(&cfg);

    if (!ok) {
        LOG("CONFIG_NET: old format detected, attempting legacy migration\n");
        ok = cfgMigrateLegacyNet(old_path);
    }

    if (!ok)
        return 0;

    if (wOPLNetSave()) {
        char bak[256];
        snprintf(bak, sizeof(bak), "%s.bak", old_path);
        rename(old_path, bak);
        LOG("CONFIG_NET: migrated to '%s'\n", NET_FILENAME);
    }
    // DELETE_WITH_MIGRATION

    return 1;
}

int wOPLNetSave(void)
{
    return do_save(NET_FILENAME, build_net);
}

// ---------------------------------------------------------------------------
// Last config (wopl_last_played.cfg)
// ---------------------------------------------------------------------------

// No legacy migration.. pointless
int wOPLLastLoad(void)
{
    if (!ensure_config_dir())
        return 0;

    char path[256];

    if (!pathJoin(path, sizeof(path), config_dir, LAST_FILENAME))
        return 0;

    config_t cfg;
    config_init(&cfg);

    if (!config_read_file(&cfg, path)) {
        log_config_error(path, &cfg);
        config_destroy(&cfg);
        return 0;
    }

    cfgValidateBegin(path);
    const char *string = lookup_str(&cfg, "last_played", NULL);
    if (string)
        copy_str(last_played, string, sizeof(last_played));
    cfgValidateEnd();

    config_destroy(&cfg);

    return 1;
}

int wOPLLastSave(const char *startup)
{
    if (!ensure_config_dir() || !startup)
        return 0;

    char path[256];

    if (!pathJoin(path, sizeof(path), config_dir, LAST_FILENAME))
        return 0;

    config_t cfg;
    config_init(&cfg);
    config_setting_t *root = config_root_setting(&cfg);

    config_setting_t *setting = config_setting_add(root, "last_played", CONFIG_TYPE_STRING);
    if (setting)
        config_setting_set_string(setting, startup);

    int ok = config_write_file(&cfg, path);
    config_destroy(&cfg);

    if (ok)
        copy_str(last_played, startup, sizeof(last_played));

    return ok;
}

const char *wOPLLastGet(void)
{
    return last_played[0] ? last_played : NULL;
}

// ---------------------------------------------------------------------------
// Global Game config (wopl_global_game.cfg)
// ---------------------------------------------------------------------------

static void parse_global_game(config_t *cfg)
{
    int val;
#ifdef GSM
    if (cfgGetInt(cfg, "gsm.enable", &val))
        gGlobalGameCfg.gsm_enable = val;
    if (cfgGetInt(cfg, "gsm.vmode", &val))
        gGlobalGameCfg.gsm_vmode = val;
    if (cfgGetInt(cfg, "gsm.x_offset", &val))
        gGlobalGameCfg.gsm_xoffset = val;
    if (cfgGetInt(cfg, "gsm.y_offset", &val))
        gGlobalGameCfg.gsm_yoffset = val;
    if (cfgGetInt(cfg, "gsm.field_fix", &val))
        gGlobalGameCfg.gsm_fieldfix = val;
#endif
#ifdef CHEAT
    if (cfgGetInt(cfg, "cheat.enable", &val))
        gGlobalGameCfg.cheat_enable = val;
    if (cfgGetInt(cfg, "cheat.mode", &val))
        gGlobalGameCfg.cheat_mode = val;
    if (cfgGetInt(cfg, "cheat.enable_image", &val))
        gGlobalGameCfg.cheat_enable_image = val;
#endif
#ifdef PADEMU
    if (cfgGetInt(cfg, "pademu.enable", &val))
        gGlobalGameCfg.pademu_enable = val;
    if (cfgGetInt(cfg, "pademu.settings", &val))
        gGlobalGameCfg.pademu_settings = val;
    if (cfgGetInt(cfg, "padmacro.settings", &val))
        gGlobalGameCfg.padmacro_settings = val;
#endif
    if (cfgGetInt(cfg, "osd.enable", &val))
        gGlobalGameCfg.osd_enable = val;
    if (cfgGetInt(cfg, "osd.lang_id", &val))
        gGlobalGameCfg.osd_langid = val;
    if (cfgGetInt(cfg, "osd.tv_aspect", &val))
        gGlobalGameCfg.osd_tv_aspect = val;
    if (cfgGetInt(cfg, "osd.vmode", &val))
        gGlobalGameCfg.osd_vmode = val;
}

static void build_global_game(config_setting_t *root)
{
    config_setting_t *group;
#ifdef GSM
    group = add_group(root, "gsm");
    set_int(group, "enable", gGlobalGameCfg.gsm_enable);
    set_int(group, "vmode", gGlobalGameCfg.gsm_vmode);
    set_int(group, "x_offset", gGlobalGameCfg.gsm_xoffset);
    set_int(group, "y_offset", gGlobalGameCfg.gsm_yoffset);
    set_int(group, "field_fix", gGlobalGameCfg.gsm_fieldfix);
#endif
#ifdef CHEAT
    group = add_group(root, "cheat");
    set_int(group, "enable", gGlobalGameCfg.cheat_enable);
    set_int(group, "mode", gGlobalGameCfg.cheat_mode);
    set_int(group, "enable_image", gGlobalGameCfg.cheat_enable_image);
#endif
#ifdef PADEMU
    group = add_group(root, "pademu");
    set_int(group, "enable", gGlobalGameCfg.pademu_enable);
    set_int(group, "settings", gGlobalGameCfg.pademu_settings);
    group = add_group(root, "padmacro");
    set_int(group, "settings", gGlobalGameCfg.padmacro_settings);
#endif
    group = add_group(root, "osd");
    set_int(group, "enable", gGlobalGameCfg.osd_enable);
    set_int(group, "lang_id", gGlobalGameCfg.osd_langid);
    set_int(group, "tv_aspect", gGlobalGameCfg.osd_tv_aspect);
    set_int(group, "vmode", gGlobalGameCfg.osd_vmode);
}

int wOPLGlobalGameLoad(void)
{
    LOG("CONFIG_GAME: enter config_dir='%s'\n", config_dir);

    if (!ensure_config_dir())
        return 0;

    char path[256];
    config_t cfg;

    if (!pathJoin(path, sizeof(path), config_dir, GAME_FILENAME))
        return 0;

    LOG("CONFIG_GAME: trying '%s'\n", path);

    config_init(&cfg);
    if (config_read_file(&cfg, path)) {
        cfgValidateBegin(path);
        parse_global_game(&cfg);
        cfgValidateEnd();
        config_destroy(&cfg);
        LOG("CONFIG_GAME: loaded from '%s'\n", path);
        return 1;
    }
    log_config_error(path, &cfg);
    config_destroy(&cfg);

    // DELETE_WITH_MIGRATION v
    if (!pathJoin(path, sizeof(path), config_dir, GAME_FILENAME_OLD))
        return 0;

    if (cfgMigrateLegacyGlobalGame(path)) {
        if (wOPLGlobalGameSave()) {
            char bak[256];
            snprintf(bak, sizeof(bak), "%s.bak", path);
            rename(path, bak);
            LOG("CONFIG_GAME: migrated to '%s'\n", GAME_FILENAME);
        }
        return 1;
    }
    // DELETE_WITH_MIGRATION ^

    return 0;
}

int wOPLGlobalGameSave(void)
{
    return do_save(GAME_FILENAME, build_global_game);
}

// ---------------------------------------------------------------------------
// Per Game config (SLES1234.cfg)
// ---------------------------------------------------------------------------

static void init_per_game_cfg(per_game_cfg_t *cfg)
{
    memset(cfg, 0, sizeof(*cfg));
    cfg->dma = 7; // 7 = not set, use device default
}

static void parse_per_game(config_t *cfg, per_game_cfg_t *pg)
{
    int val;
    const char *str;

    if (cfgGetInt(cfg, "compat", &val))
        pg->compat = val;
    if (cfgGetInt(cfg, "dma", &val))
        pg->dma = val;
    if (cfgGetInt(cfg, "core_loader", &val))
        pg->core_loader = val;
    if (cfgGetStr(cfg, "dnas", &str))
        copy_str(pg->dnas, str, sizeof(pg->dnas));
    if (cfgGetStr(cfg, "alt_startup", &str))
        copy_str(pg->alt_startup, str, sizeof(pg->alt_startup));
    if (cfgGetStr(cfg, "vmc1", &str))
        copy_str(pg->vmc1, str, sizeof(pg->vmc1));
    if (cfgGetStr(cfg, "vmc2", &str))
        copy_str(pg->vmc2, str, sizeof(pg->vmc2));

#ifdef GSM
    if (cfgGetInt(cfg, "gsm.source", &val))
        pg->gsm_source = val;
    if (cfgGetInt(cfg, "gsm.enable", &val))
        pg->gsm_enable = val;
    if (cfgGetInt(cfg, "gsm.vmode", &val))
        pg->gsm_vmode = val;
    if (cfgGetInt(cfg, "gsm.x_offset", &val))
        pg->gsm_xoffset = val;
    if (cfgGetInt(cfg, "gsm.y_offset", &val))
        pg->gsm_yoffset = val;
    if (cfgGetInt(cfg, "gsm.field_fix", &val))
        pg->gsm_fieldfix = val;
#endif
#ifdef CHEAT
    if (cfgGetInt(cfg, "cheat.source", &val))
        pg->cheat_source = val;
    if (cfgGetInt(cfg, "cheat.enable", &val))
        pg->cheat_enable = val;
    if (cfgGetInt(cfg, "cheat.mode", &val))
        pg->cheat_mode = val;
    if (cfgGetInt(cfg, "cheat.enable_image", &val))
        pg->cheat_enable_image = val;
#endif
#ifdef PADEMU
    if (cfgGetInt(cfg, "pademu.source", &val))
        pg->pademu_source = val;
    if (cfgGetInt(cfg, "pademu.enable", &val))
        pg->pademu_enable = val;
    if (cfgGetInt(cfg, "pademu.settings", &val))
        pg->pademu_settings = val;
    if (cfgGetInt(cfg, "padmacro.source", &val))
        pg->padmacro_source = val;
    if (cfgGetInt(cfg, "padmacro.settings", &val))
        pg->padmacro_settings = val;
#endif
    if (cfgGetInt(cfg, "osd.source", &val))
        pg->osd_source = val;
    if (cfgGetInt(cfg, "osd.enable", &val))
        pg->osd_enable = val;
    if (cfgGetInt(cfg, "osd.lang_id", &val))
        pg->osd_langid = val;
    if (cfgGetInt(cfg, "osd.tv_aspect", &val))
        pg->osd_tv_aspect = val;
    if (cfgGetInt(cfg, "osd.vmode", &val))
        pg->osd_vmode = val;
}

static void build_per_game(config_setting_t *root, const per_game_cfg_t *pg)
{
    set_int(root, "compat", pg->compat);
    set_int(root, "dma", pg->dma);
    set_int(root, "core_loader", pg->core_loader);
    set_str(root, "dnas", pg->dnas);
    set_str(root, "alt_startup", pg->alt_startup);
    set_str(root, "vmc1", pg->vmc1);
    set_str(root, "vmc2", pg->vmc2);

    config_setting_t *group;
#ifdef GSM
    group = add_group(root, "gsm");
    set_int(group, "source", pg->gsm_source);
    set_int(group, "enable", pg->gsm_enable);
    set_int(group, "vmode", pg->gsm_vmode);
    set_int(group, "x_offset", pg->gsm_xoffset);
    set_int(group, "y_offset", pg->gsm_yoffset);
    set_int(group, "field_fix", pg->gsm_fieldfix);
#endif
#ifdef CHEAT
    group = add_group(root, "cheat");
    set_int(group, "source", pg->cheat_source);
    set_int(group, "enable", pg->cheat_enable);
    set_int(group, "mode", pg->cheat_mode);
    set_int(group, "enable_image", pg->cheat_enable_image);
#endif
#ifdef PADEMU
    group = add_group(root, "pademu");
    set_int(group, "source", pg->pademu_source);
    set_int(group, "enable", pg->pademu_enable);
    set_int(group, "settings", pg->pademu_settings);
    group = add_group(root, "padmacro");
    set_int(group, "source", pg->padmacro_source);
    set_int(group, "settings", pg->padmacro_settings);
#endif
    group = add_group(root, "osd");
    set_int(group, "source", pg->osd_source);
    set_int(group, "enable", pg->osd_enable);
    set_int(group, "lang_id", pg->osd_langid);
    set_int(group, "tv_aspect", pg->osd_tv_aspect);
    set_int(group, "vmode", pg->osd_vmode);
}

int wOPLPerGameLoad(const char *path, per_game_cfg_t *cfg)
{
    init_per_game_cfg(cfg);

    config_t lcfg;
    config_init(&lcfg);
    if (config_read_file(&lcfg, path)) {
        cfgValidateBegin(path);
        parse_per_game(&lcfg, cfg);
        cfgValidateEnd();
        config_destroy(&lcfg);
        LOG("CONFIG_PERGAME: loaded from '%s'\n", path);

        return 1;
    }

    log_config_error(path, &lcfg);
    config_destroy(&lcfg);

    return 0;
}

int wOPLPerGameSave(const char *path, const per_game_cfg_t *cfg)
{
    config_t lcfg;
    config_init(&lcfg);
    config_setting_t *root = config_root_setting(&lcfg);
    build_per_game(root, cfg);

    int ok = config_write_file(&lcfg, path);
    config_destroy(&lcfg);

    if (!ok)
        LOG("CONFIG_PERGAME: failed to write '%s'\n", path);
    else
        LOG("CONFIG_PERGAME: saved to '%s'\n", path);

    return ok;
}

// ---------------------------------------------------------------------------
// Per Game info (SLES1234.info)
// ---------------------------------------------------------------------------

static int lookup_info_int(config_t *cfg, const char *key, int *out)
{
    const config_setting_t *setting = config_lookup(cfg, key);

    if (!setting)
        return 0;

    switch (config_setting_type(setting)) {
        case CONFIG_TYPE_INT:
            *out = config_setting_get_int(setting);
            return 1;
        case CONFIG_TYPE_BOOL:
            *out = config_setting_get_bool(setting) ? 1 : 0;
            return 1;
        case CONFIG_TYPE_STRING: {
            const char *str = config_setting_get_string(setting);
            const char *slash = strrchr(str, '/');

            if (slash && slash[1])
                str = slash + 1;

            *out = atoi(str);
            return 1;
        }
        default:
            cfgCheckExists(cfg, key, "integer or string");
            return 0;
    }
}

static void parse_game_info(config_t *cfg, game_info_t *gi)
{
    const char *str;

    if (cfgGetStr(cfg, "Title", &str))
        copy_str(gi->title, str, sizeof(gi->title));
    if (cfgGetStr(cfg, "Serial", &str))
        copy_str(gi->serial, str, sizeof(gi->serial));
    if (cfgGetStr(cfg, "Description", &str))
        copy_str(gi->description, str, sizeof(gi->description));
    if (cfgGetStr(cfg, "Developer", &str))
        copy_str(gi->developer, str, sizeof(gi->developer));
    if (cfgGetStr(cfg, "Genre", &str))
        copy_str(gi->genre, str, sizeof(gi->genre));
    if (cfgGetStr(cfg, "Publisher", &str))
        copy_str(gi->publisher, str, sizeof(gi->publisher));
    if (cfgGetStr(cfg, "Release", &str))
        copy_str(gi->release, str, sizeof(gi->release));
    if (cfgGetStr(cfg, "Aspect", &str))
        copy_str(gi->aspect, str, sizeof(gi->aspect));
    if (cfgGetStr(cfg, "Parental", &str))
        copy_str(gi->parental, str, sizeof(gi->parental));
    if (cfgGetStr(cfg, "Region", &str))
        copy_str(gi->region, str, sizeof(gi->region));

    lookup_info_int(cfg, "Players", &gi->players);
    lookup_info_int(cfg, "UserRating", &gi->user_rating);

    if (cfgGetStr(cfg, "Version", &str))
        copy_str(gi->version, str, sizeof(gi->version));
    if (cfgGetStr(cfg, "Package", &str))
        copy_str(gi->package, str, sizeof(gi->package));
    if (cfgGetStr(cfg, "Source", &str))
        copy_str(gi->source, str, sizeof(gi->source));
}

static void build_game_info(config_setting_t *root, const game_info_t *gi)
{
    if (gi->title[0])
        set_str(root, "Title", gi->title);
    if (gi->serial[0])
        set_str(root, "Serial", gi->serial);
    if (gi->description[0])
        set_str(root, "Description", gi->description);
    if (gi->developer[0])
        set_str(root, "Developer", gi->developer);
    if (gi->genre[0])
        set_str(root, "Genre", gi->genre);
    if (gi->publisher[0])
        set_str(root, "Publisher", gi->publisher);
    if (gi->release[0])
        set_str(root, "Release", gi->release);
    if (gi->aspect[0])
        set_str(root, "Aspect", gi->aspect);
    if (gi->parental[0])
        set_str(root, "Parental", gi->parental);
    if (gi->players)
        set_int(root, "Players", gi->players);
    if (gi->region[0])
        set_str(root, "Region", gi->region);
    if (gi->user_rating)
        set_int(root, "UserRating", gi->user_rating);

    if (gi->version[0])
        set_str(root, "Version", gi->version);
    if (gi->package[0])
        set_str(root, "Package", gi->package);
    if (gi->source[0])
        set_str(root, "Source", gi->source);
}

int wOPLGameInfoLoad(const char *path, game_info_t *gi)
{
    memset(gi, 0, sizeof(*gi));
    config_t cfg;
    config_init(&cfg);
    if (config_read_file(&cfg, path)) {
        cfgValidateBegin(path);
        parse_game_info(&cfg, gi);
        cfgValidateEnd();
        config_destroy(&cfg);

        return 1;
    }
    log_config_error(path, &cfg);
    config_destroy(&cfg);

    return 0;
}

int wOPLGameInfoSave(const char *path, const game_info_t *gi)
{
    config_t cfg;
    config_init(&cfg);
    config_setting_t *root = config_root_setting(&cfg);
    build_game_info(root, gi);
    int ok = config_write_file(&cfg, path);
    config_destroy(&cfg);

    return ok;
}

// ---------------------------------------------------------------------------
// Application level config handling
// ---------------------------------------------------------------------------

char *gBaseMCDir; // used for thm/lang even after migration

static int lscstatus = CONFIG_ALL;
static int lscret = 0;

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
        bdmLoadEnabledDeviceModules();

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

#ifdef __DEBUG
    debugApplyConfig();
#endif
}

static int resolve_legacy_config_dir(void)
{
    char resolved[128];

    if (!pathIsLegacyMassPath(config_dir))
        return 1;

    LOG("CONFIG: resolving legacy config_dir '%s'\n", config_dir);

    if (!bdmResolveLegacyPathFromDeviceList(resolved, sizeof(resolved), config_dir)) {
        LOG("CONFIG: could not resolve legacy config_dir '%s'\n", config_dir);
        return 0;
    }

    pathNormaliseDir(resolved, sizeof(resolved));

    if (!pathIsDevicePath(resolved) || pathIsLegacyMassPath(resolved)) {
        LOG("CONFIG: rejected resolved config_dir '%s'\n", resolved);
        return 0;
    }

    LOG("CONFIG: resolved config_dir '%s' -> '%s'\n", config_dir, resolved);
    copy_str(config_dir, resolved, sizeof(config_dir));

    return 1;
}

void _loadConfig() // called directly by initializer at boot before GUI is ready
{
    int themeID = -1, langID = -1;
    int result = 0;
    int have_config_dir;

    have_config_dir = ensure_config_dir();

    LOG("CONFIG: initial config root result=%d config_dir='%s' boot_dir='%s'\n", have_config_dir, config_dir, boot_dir);

    if (lscstatus & CONFIG_OPL) {
        if (wOPLLoad(&themeID, &langID))
            result |= CONFIG_OPL;
        if (getKeyPressed(KEY_TRIANGLE) && getKeyPressed(KEY_CROSS)) {
            LOG("--- Triangle+Cross held at boot - setting Video Mode to Auto ---\n");
            gVMode = 0;
        }
    }
    if (lscstatus & CONFIG_NETWORK) {
        if (wOPLNetLoad())
            result |= CONFIG_NETWORK;
    }
    if (lscstatus & CONFIG_GAME)
        if (wOPLGlobalGameLoad())
            result |= CONFIG_GAME;

    LOG("CONFIG: load requested=0x%X result=0x%X config_dir='%s'\n", lscstatus, result, config_dir);

    configApply(themeID, langID, 0);
    resolve_legacy_config_dir();

    lscret = result;
    lscstatus = 0;
    if (result)
        showCfgPopup = 1;

#ifdef PADEMU
    gEnablePadEmu = gGlobalGameCfg.pademu_enable;
    sysInitPadEmu();
#endif
}

int configLoad(int types)
{
    lscstatus = types;
    lscret = 0;

    guiHandleDeferedIO(&lscstatus, _l(_STR_LOADING_SETTINGS), IO_CUSTOM_SIMPLEACTION, &_loadConfig);

    return lscret;
}

static int config_type_count(int types)
{
    int count = 0;

    if (types & CONFIG_OPL)
        count++;
    if (types & CONFIG_NETWORK)
        count++;
    if (types & CONFIG_GAME)
        count++;

    return count;
}

static int save_all_to_current_dir(int types) // like the old configWriteMulti()
{
    int result = 0;
    int expected = config_type_count(types);

    if (!ensure_config_dir()) {
        LOG("CONFIG: no config_dir selected for save\n");
        return 0;
    }

    if (!resolve_legacy_config_dir())
        return 0;

    LOG("CONFIG: saving to config_dir '%s'\n", config_dir);

    if (config_path_has_device_prefix(config_dir, "mc") && !sbEnsureMCConfigFolder(config_dir)) {
        LOG("CONFIG: failed to prepare MC config folder '%s'\n", config_dir);
        return 0;
    }

    if (types & CONFIG_OPL)
        result += do_save_at_dir(config_dir, WOPL_FILENAME, build_opl);
    if (types & CONFIG_NETWORK)
        result += do_save_at_dir(config_dir, NET_FILENAME, build_net);
    if (types & CONFIG_GAME)
        result += do_save_at_dir(config_dir, GAME_FILENAME, build_global_game);

    if (result != expected)
        return result;

    if (!save_boot_config())
        return 0;

    return result;
}

static void _saveConfig()
{
    lscret = save_all_to_current_dir(lscstatus);
    lscstatus = 0;
}

int configSave(int types, int showUI)
{
    lscstatus = types;
    lscret = 0;

    guiHandleDeferedIO(&lscstatus, _l(_STR_SAVING_SETTINGS), IO_CUSTOM_SIMPLEACTION, &_saveConfig);

    if (showUI) {
        if (lscret) {
            char notification[128];
            char path[128] = {0};
            const char *rawPath = wOPLGetDir();
            if (rawPath) {
                strncpy(path, rawPath, sizeof(path) - 1);
                char *colpos = strchr(path, ':');
                if (colpos != NULL)
                    *(colpos + 1) = '\0';
            }
            snprintf(notification, sizeof(notification), _l(_STR_SETTINGS_SAVED), path[0] ? path : "?");
            guiMsgBox(notification, 0, NULL);
        } else
            guiMsgBox(_l(_STR_ERROR_SAVING_SETTINGS), 0, NULL);
    }

    return lscret;
}
