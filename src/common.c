/*
  Copyright 2009, Volca
  Licenced under Academic Free License version 3.0
  Review OpenUsbLd README & LICENSE files for further details.
*/

#include "include/common.h"
#include "include/ioman.h"
#include "include/gui.h"
#include "include/lang.h"
#include "include/pad.h"
#include "include/system.h"
#include "include/extern_irx.h"

#include "include/sound.h"

#include <libpad.h>

void menuClearGameList(opl_io_module_t *mdl);

// frame counter
unsigned int frameCounter;

// Global data

int gAutosort;
int gAutoRefresh;
int gEnableArt;
int gEnableArchivedArt;
int gPS2Logo;
int gDefaultDevice;
int gEnableWrite;
int gRememberLastPlayed;
char gExportName[32];

static void updateFavouritesMenu(submenu_item_t *item, opl_io_module_t *mdl)
{
    struct gui_update_t *gup = NULL;
    gup = guiOpCreate(GUI_OP_APPEND_MENU);

    gup->menu.menu = &mdl->menuItem;
    gup->menu.subMenu = &mdl->subMenu;

    gup->submenu.icon_id = item->icon_id;
    gup->submenu.id = item->id;
    gup->submenu.text = item->text;
    gup->submenu.text_id = item->text_id;
    gup->submenu.selected = 0;
    gup->submenu.owner = (void *)item->owner;

    guiDeferUpdate(gup);

    if (gAutosort) {
        gup = guiOpCreate(GUI_OP_SORT);
        gup->menu.menu = &mdl->menuItem;
        gup->menu.subMenu = &mdl->subMenu;
        guiDeferUpdate(gup);
    }
}

item_list_t *getFavouritesOwnerPointer(short int mode)
{
    return list_support[mode].support;
}

static int validateFavouriteItem(submenu_item_t *item)
{
    item_list_t *itemOwner = (item_list_t *)item->owner;
    int i, startMode = itemOwner->mode;

    LOG("Validating favourite: text=%s, id=%d, startMode=%d\n", item->text, item->id, startMode);

    // make sure item from favourites.bin is on a connected device before adding it to the favourites submenu list
    // if item was on a bdm device, hot plugging could result in a different mount point.. might need to check them all..
    if (startMode >= BDM_MODE && startMode <= BDM_MODE4) {
        for (i = BDM_MODE; i <= BDM_MODE4; i++) {
            opl_io_module_t *mdl = &list_support[i];

            if (mdl->support && mdl->support->enabled) {
                submenu_list_t *cur = submenuFindItemByIdAndText(mdl->menuItem.submenu, item->id, item->text);
                if (cur != NULL) {
                    if (startMode != i) {                   // item found on a new mount point
                        item->owner = (void *)mdl->support; // update submenu_item owner to the new mode, only in the list.. leave the file alone
                        LOG("Favourite item found on new mount point, adjusting startMode to %d\n", i);
                    }
                    cur->item.favourited = 1;
                    return 1;
                }
            }
        }
    } else {
        opl_io_module_t *mdl = &list_support[startMode];

        if (mdl->support && mdl->support->enabled) {
            submenu_list_t *cur = submenuFindItemByIdAndText(mdl->menuItem.submenu, item->id, item->text);
            if (cur != NULL) {
                cur->item.favourited = 1;
                return 1;
            }
        }
    }

    LOG("Favourite item not found %s\n", item->text);

    // can't find the item on a connected device.. keep it in favourites.bin but don't add it to the list for render or execution.
    return 0;
}

void loadFavourites(void)
{
    int size, i;
    submenu_item_t *items = readFavouritesFile(&size);

    guiExecDeferredOps();
    menuClearGameList(&list_support[FAV_MODE]);

    if (items != NULL) {
        int count = size / sizeof(submenu_item_t);
        for (i = 0; i < count; ++i) {
            if (validateFavouriteItem(&items[i])) {
                LOG("Favourite found, adding to list\n");
                updateFavouritesMenu(&items[i], &list_support[FAV_MODE]);
            }
        }

        free(items);
    } else
        LOG("Failed to load favourites.\n");
}

void setDefaultColors(void)
{
    gDefaultBgColor[0] = 0xFF;
    gDefaultBgColor[1] = 0xFF;
    gDefaultBgColor[2] = 0xFF;

    gDefaultTextColor[0] = 0x5C;
    gDefaultTextColor[1] = 0x5C;
    gDefaultTextColor[2] = 0x5C;

    gDefaultSelTextColor[0] = 0x2a;
    gDefaultSelTextColor[1] = 0x2a;
    gDefaultSelTextColor[2] = 0x2a;

    gDefaultUITextColor[0] = 0xFF;
    gDefaultUITextColor[1] = 0xFF;
    gDefaultUITextColor[2] = 0xFF;

    gDefaultPlasmaBlendColor[0] = 0xFF;
    gDefaultPlasmaBlendColor[1] = 0xFF;
    gDefaultPlasmaBlendColor[2] = 0xFF;
}