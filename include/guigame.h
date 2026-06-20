#ifndef __GUIGAME_H
#define __GUIGAME_H

#define SETTINGS_GLOBAL  0
#define SETTINGS_PERGAME 1

int guiGameAltStartupNameHandler(char *text, int maxLen);

int guiGameVmcNameHandler(char *text, int maxLen);
void guiGameShowVMCMenu(int id, item_list_t *support);
void guiGameShowCompatConfig(int id, item_list_t *support);
#ifdef GSM
void guiGameShowGSConfig(void);
#endif

#ifdef CHEAT
void guiGameShowCheatConfig(void);
#endif

#ifdef PADEMU
void guiGameShowPadEmuConfig(int forceGlobal);
void guiGameShowPadMacroConfig(int forceGlobal);
void guiGameSavePadEmuGlobalConfig(void);
void guiGameSavePadMacroGlobalConfig(void);
#endif

void guiGameShowOSDLanguageConfig(int forceGlobal);
void guiGameSaveOSDLanguageGlobalConfig(void);

void guiGameLoadConfig(item_list_t *support, per_game_cfg_t *pg);
int guiGameSaveConfig(per_game_cfg_t *pg, item_list_t *support);
void guiGameTestSettings(int id, item_list_t *support, per_game_cfg_t *pg);

void guiGameRemoveSettings(per_game_cfg_t *pg);
void guiGameRemoveGlobalSettings(void);

#endif
