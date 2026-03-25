#ifndef __MODULE_H
#define __MODULE_H

#include "include/menusys.h"

typedef struct
{
    item_list_t *support;

    /// menu item used with this list support
    menu_item_t menuItem;

    /// submenu list
    submenu_list_t *subMenu;
} opl_io_module_t;

extern opl_io_module_t list_support[MODE_COUNT];

void moduleUpdateMenuInternal(opl_io_module_t *mod, int themeChanged, int langChanged);

void moduleUpdateMenu(int mode, int themeChanged, int langChanged);

void moduleCleanup(opl_io_module_t *mod, int exception, int modeSelected);

void moduleClearIOModuleT(opl_io_module_t *mod);

#endif