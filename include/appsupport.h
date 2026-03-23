#ifndef __APP_SUPPORT_H
#define __APP_SUPPORT_H

#include "include/iosupport.h"

#define APP_TITLE_CONFIG_FILE "title.cfg"

extern int gAPPStartMode;

item_list_t *appGetObject(int initOnly);

#endif
