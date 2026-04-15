#ifndef __INITIALIZER_H
#define __INITIALIZER_H

void initSupport(item_list_t *itemList, int mode, int force_reinit);

void initAllSupport(int force_reinit);

void deinitAllSupport(int exception, int modeSelected);

void deinit(int exception, int modeSelected);

// Shutdown minimal services initiated for auto loading.
void miniDeinit(config_set_t *configSet);

void init(void);

void deferredInit(void);

void deferredAudioInit(void);

void miniInit(int mode);

#endif