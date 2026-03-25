#include "include/ioman.h"
#include "include/gui.h"
#include "include/guigame.h"
#include "include/themes.h"
#include "include/pad.h"
#include "include/texcache.h"
#include "include/menusys.h"
#include "include/system.h"

#include "include/favsupport.h"

#include "include/sound.h"
#include "include/module.h"
#include "include/iosupport.h"

void itemInitSupport(item_list_t *support)
{
    support->itemInit(support);
    moduleUpdateMenuInternal((opl_io_module_t *)support->owner, 0, 0);
    // Manual refreshing can only be done if either auto refresh is disabled or auto refresh is disabled for the item.
    if (!gAutoRefresh || (support->updateDelay == MENU_UPD_DELAY_NOUPDATE))
        ioPutRequest(IO_MENU_UPDATE_DEFFERED, &support->mode);
}

void itemExecSelect(struct menu_item *curMenu)
{
    item_list_t *support = curMenu->userdata;
    sfxPlay(SFX_CONFIRM);

    if (support) {
        if (support->enabled) {
            if (curMenu->current) {
                config_set_t *configSet = menuLoadConfig();
                cacheCancelPendingImageLoads();
                support->itemLaunch(support, curMenu->current->item.id, configSet);
            }
        } else {
            // If we're trying to enable BDM support we need to enable it for all BDM menu slots.
            if (support->mode == BDM_MODE) {
                // Initialize support for all bdm modules.
                for (int i = 0; i <= BDM_MODE4; i++) {
                    opl_io_module_t *mod = &list_support[i];
                    itemInitSupport(mod->support);
                }
            } else {
                // Normal initialization.
                itemInitSupport(support);
            }
        }
    } else
        guiMsgBox("NULL Support object. Please report", 0, NULL);
}

void itemExecRefresh(struct menu_item *curMenu)
{
    item_list_t *support = curMenu->userdata;

    if (support && support->enabled) {
        ioPutRequest(IO_MENU_UPDATE_DEFFERED, &support->mode);
        sfxPlay(SFX_CONFIRM);
    }
}

void itemExecCross(struct menu_item *curMenu)
{
    if (gSelectButton == KEY_CROSS)
        itemExecSelect(curMenu);
}

void itemExecCircle(struct menu_item *curMenu)
{
    if (gSelectButton == KEY_CIRCLE)
        itemExecSelect(curMenu);
}

void itemExecSquare(struct menu_item *curMenu)
{
    if (curMenu->current && gTheme->infoElems.first)
        guiSwitchScreen(GUI_SCREEN_INFO);
}

void itemExecTriangle(struct menu_item *curMenu)
{
    if (!curMenu->current)
        return;

    item_list_t *support = curMenu->userdata;

    if (support) {
        if (support->mode == FAV_MODE)
            support->flags = favGetFlags(support);

        if (!(support->flags & MODE_FLAG_NO_COMPAT)) {
            if (menuCheckParentalLock() == 0) {
                menuInitGameMenu();
                guiSwitchScreen(GUI_SCREEN_GAME_MENU);
                guiGameLoadConfig(support, gameMenuLoadConfig(NULL));
            }
        } else {
            if (menuCheckParentalLock() == 0 && gEnableWrite) {
                menuInitAppMenu();
                guiSwitchScreen(GUI_SCREEN_APP_MENU);
            }
        }
    } else
        guiMsgBox("NULL Support object. Please report", 0, NULL);
}

void itemExecFav(struct menu_item *curMenu)
{
    if (!curMenu->current)
        return;

    item_list_t *support = curMenu->userdata;
    opl_io_module_t *mdl = &list_support[FAV_MODE];

    if (!mdl->support || mdl->support->enabled == 0)
        return;

    if (curMenu->current->item.favourited || support->mode == FAV_MODE) {
        removeFavouriteByIdAndText(curMenu->current->item.id, curMenu->current->item.text);
        ioPutRequest(IO_CUSTOM_SIMPLEACTION, &loadFavourites);
        curMenu->current->item.favourited = 0;

        // remove favourited flag from source if item removed within fav menu
        if (support->mode == FAV_MODE) {
            item_list_t *source = curMenu->current->item.owner;
            opl_io_module_t *pOwner = (opl_io_module_t *)source->owner;
            submenu_list_t *sourceItem = submenuFindItemByIdAndText(pOwner->menuItem.submenu, curMenu->current->item.id, curMenu->current->item.text);
            sourceItem->item.favourited = 0;
        }

        sfxPlay(SFX_CANCEL);
        return;
    }

    if (support && support->enabled) {
        addFavouriteItem(&curMenu->current->item);
        ioPutRequest(IO_CUSTOM_SIMPLEACTION, &loadFavourites);
        curMenu->current->item.favourited = 1;

        sfxPlay(SFX_CONFIRM);
    }
}
