#ifndef CONFIG_WOPL_H
#define CONFIG_WOPL_H

#include "include/iosupport.h"
#include <libconfig.h>

#define CONFIG_OPL     1
#define CONFIG_NETWORK 8
#define CONFIG_GAME    16
#define CONFIG_ALL     (CONFIG_OPL | CONFIG_NETWORK | CONFIG_GAME)

extern int gBDMFramesDelay;
extern int gETHFramesDelay;
extern int gHDDFramesDelay;
extern int gMMCEFramesDelay;
extern int gAPPFramesDelay;
extern int gFAVFramesDelay;

void log_config_error(const char *path, const config_t *cfg);
void cfgValidateBegin(const char *label);
void cfgValidateEnd(void);
int cfgGetInt(const config_t *cfg, const char *path, int *out);
int cfgGetStr(const config_t *cfg, const char *path, const char **out);
void cfgCheckExists(const config_t *cfg, const char *path, const char *expected);

void dnas_to_binary(const char *dnas, char *out, int out_size);

const char *wOPLGetDir(void);
const char *wOPLGetThemeName(void);
const char *wOPLGetLanguageName(void);

int wOPLLoad(int *out_theme_id, int *out_lang_id);
int wOPLSave(void);

int wOPLNetLoad(void);
int wOPLNetSave(void);

int wOPLLastLoad(void);
int wOPLLastSave(const char *startup);
const char *wOPLLastGet(void);

int wOPLGlobalGameLoad(void);
int wOPLGlobalGameSave(void);
int wOPLPerGameLoad(const char *path, per_game_cfg_t *cfg);
int wOPLPerGameSave(const char *path, const per_game_cfg_t *cfg);

int wOPLGameInfoLoad(const char *path, game_info_t *gi);
int wOPLGameInfoSave(const char *path, const game_info_t *gi);

extern char gParentalLockPassword[256];
extern char *gBaseMCDir;

void _loadConfig();
void configApply(int themeID, int langID, int skipDeviceRefresh);
int configLoad(int types);
int configSave(int types, int showUI);

#endif
