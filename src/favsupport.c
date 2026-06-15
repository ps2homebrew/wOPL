/*
 Copyright 2024
 Licenced under Academic Free License version 3.0
 Review OpenPS2Loader README & LICENSE files for further details.
 */

#include "include/ioman.h"
#include "include/gui.h"
#include "include/lang.h"
#include "include/themes.h"
#include "include/favsupport.h"
#include "include/common.h"
#include "include/config_wopl.h"
#include <malloc.h>
#include <stdio.h>

static int favItemCount = 0;

static item_list_t favItemList;

int gFAVStartMode;

void menuClearGameList(opl_io_module_t *mdl);

void favInit(item_list_t *itemList)
{
    LOG("FAVSUPPORT Init\n");
    favItemList.delay = gFAVFramesDelay;
    favItemList.enabled = 1;
    itemList->enabled = 1;
}

item_list_t *favGetObject(int initOnly)
{
    if (initOnly && !favItemList.enabled)
        return NULL;
    return &favItemList;
}

static int favNeedsUpdate(item_list_t *itemList)
{
    char filename[256];
    FILE *file;
    int fileSize = 0;

    const char *dir = wOPLGetDir();
    if (!dir)
        return 1;

    snprintf(filename, sizeof(filename), "%sfavourites.bin", dir);

    file = fopen(filename, "rb");
    if (file == NULL)
        return 1;

    fseek(file, 0, SEEK_END);
    fileSize = ftell(file);
    fclose(file);

    int currentSize = favItemCount * sizeof(submenu_item_t);

    return fileSize != currentSize ? 1 : 0;
}

static int favUpdateItemList(item_list_t *itemList)
{
    // Update list with updateFavouritesMenu() rather than updateMenuFromGameList() as id is randomly assigned based on i in count
    // we need to keep the id of the owner to use its corresponding functions
    // return a count of 0 so updateMenuFromGameList() doesn't attempt to update the list, we've done it already with loadFavourites()

    guiSetBootStatusIfActive("Loading favourites...");

    loadFavourites();
    return 0;
}

static int favGetItemCount(item_list_t *itemList)
{
    // don't update cfgs for favourites menu items

    return 0;
}

static char *favGetItemName(item_list_t *itemList, int id)
{
    opl_io_module_t *pOwner = (opl_io_module_t *)itemList->owner;
    item_list_t *favOwner = (item_list_t *)pOwner->menuItem.current->item.owner;

    return favOwner->itemGetName(favOwner, id);
}

static int favGetItemNameLength(item_list_t *itemList, int id)
{
    opl_io_module_t *pOwner = (opl_io_module_t *)itemList->owner;
    item_list_t *favOwner = (item_list_t *)pOwner->menuItem.current->item.owner;

    return favOwner->itemGetNameLength(favOwner, id);
}

static char *favGetItemStartup(item_list_t *itemList, int id)
{
    opl_io_module_t *pOwner = (opl_io_module_t *)itemList->owner;
    item_list_t *favOwner = (item_list_t *)pOwner->menuItem.current->item.owner;

    return favOwner->itemGetStartup(favOwner, id);
}

static void favDeleteItem(item_list_t *itemList, int id)
{
    // deleting items not allowed through favourites menu
    // a msgBox will display a message indicating this to the user

    return;
}

static void favRenameItem(item_list_t *itemList, int id, char *newName)
{
    // renaming items not allowed through favourites menu
    // a msgBox will display a message indicating this to the user

    return;
}

static int favGetTextId(item_list_t *itemList)
{
    return _STR_FAV;
}

static int favGetIconId(item_list_t *itemList)
{
    return FAV_ICON;
}

static void favLaunchItem(item_list_t *itemList, int id, per_game_cfg_t *pgcfg)
{
    opl_io_module_t *pOwner = (opl_io_module_t *)itemList->owner;
    item_list_t *favOwner = (item_list_t *)pOwner->menuItem.current->item.owner;
    favOwner->itemLaunch(favOwner, id, pgcfg);
}

static void favGetInfo(item_list_t *itemList, int id, game_info_t *gi)
{
    opl_io_module_t *pOwner = (opl_io_module_t *)itemList->owner;
    item_list_t *favOwner = (item_list_t *)pOwner->menuItem.current->item.owner;
    if (favOwner->itemGetInfo)
        favOwner->itemGetInfo(favOwner, id, gi);
}

static void favGetPgCfg(item_list_t *itemList, int id, per_game_cfg_t *cfg)
{
    opl_io_module_t *pOwner = (opl_io_module_t *)itemList->owner;
    item_list_t *favOwner = (item_list_t *)pOwner->menuItem.current->item.owner;
    if (favOwner->itemGetPgCfg)
        favOwner->itemGetPgCfg(favOwner, id, cfg);
}

static int favSavePgCfg(item_list_t *itemList, int id, const per_game_cfg_t *cfg)
{
    opl_io_module_t *pOwner = (opl_io_module_t *)itemList->owner;
    item_list_t *favOwner = (item_list_t *)pOwner->menuItem.current->item.owner;
    if (favOwner->itemSavePgCfg)
        return favOwner->itemSavePgCfg(favOwner, id, cfg);
    return 0;
}

static int favGetImage(item_list_t *itemList, char *folder, int isRelative, char *value, char *suffix, GSTEXTURE *resultTex, short psm)
{
    opl_io_module_t *pOwner = (opl_io_module_t *)itemList->owner;
    item_list_t *favOwner = (item_list_t *)pOwner->menuItem.current->item.owner;

    return favOwner->itemGetImage(favOwner, folder, isRelative, value, suffix, resultTex, psm);
}

static int favGetArchivedImage(item_list_t *itemList, char *folder, char *value, char *suffix, GSTEXTURE *resultTex, short psm)
{
    opl_io_module_t *pOwner = (opl_io_module_t *)itemList->owner;
    item_list_t *favOwner = (item_list_t *)pOwner->menuItem.current->item.owner;

    return favOwner->itemGetArchivedImage(favOwner, folder, value, suffix, resultTex, psm);
}

static void favCleanUp(item_list_t *itemList, int exception)
{
    if (favItemList.enabled) {
        LOG("FAVSUPPORT CleanUp\n");
        opl_io_module_t *pOwner = (opl_io_module_t *)itemList->owner;
        submenu_list_t *cur = pOwner->menuItem.submenu;

        while (cur != NULL) {
            submenu_list_t *next = cur->next;

            // free the text in the submenu item
            if (cur->item.text != NULL) {
                free(cur->item.text);
                cur->item.text = NULL;
            }

            // submenu list is freed in submenuDestroy() which is called after this ends
            cur = next;
        }
    }
}

static void favShutdown(item_list_t *itemList)
{
    favCleanUp(itemList, 0);
}

static item_list_t favItemList = {
    FAV_MODE, -1, 0, 0, MENU_MIN_INACTIVE_FRAMES, FAV_MODE_UPDATE_DELAY, NULL, NULL, &favGetTextId, NULL, &favInit, &favNeedsUpdate, &favUpdateItemList,
    &favGetItemCount, NULL, &favGetItemName, &favGetItemNameLength, &favGetItemStartup, &favDeleteItem, &favRenameItem, &favLaunchItem,
    &favGetInfo, &favGetPgCfg, &favSavePgCfg, &favGetImage, &favGetArchivedImage, &favCleanUp, &favShutdown, NULL, &favGetIconId};

unsigned char favGetFlags(item_list_t *itemList)
{
    opl_io_module_t *pOwner = (opl_io_module_t *)itemList->owner;
    item_list_t *favOwner = (item_list_t *)pOwner->menuItem.current->item.owner;

    unsigned char flags = 0;
    if (favOwner->mode == APP_MODE)
        flags |= MODE_FLAG_NO_COMPAT | MODE_FLAG_NO_UPDATE;

    if (favOwner->mode == HDD_MODE)
        flags |= MODE_FLAG_COMPAT_DMA;

    return flags;
}

void writeFavouritesFile(submenu_item_t *items, int size)
{
    char filename[256];
    FILE *file;
    int count = size / sizeof(submenu_item_t);
    int i;

    const char *dir = wOPLGetDir();
    if (!dir)
        return;

    snprintf(filename, sizeof(filename), "%sfavourites.bin", dir);
    file = fopen(filename, "wb");
    if (file != NULL) {
        for (i = 0; i < count; ++i) {
            fwrite(&items[i], sizeof(submenu_item_t), 1, file);
            int text_len = strlen(items[i].text) + 1;
            fwrite(&text_len, sizeof(int), 1, file);
            fwrite(items[i].text, text_len, 1, file);

            // write owner mode, convert to pointer at read file
            item_list_t *itemOwner = (item_list_t *)items[i].owner;
            fwrite(&itemOwner->mode, sizeof(short int), 1, file);
        }
        fclose(file);
    }
}

static item_list_t *getFavouritesOwnerPointer(short int mode)
{
    return list_support[mode].support;
}

submenu_item_t *readFavouritesFile(int *out_size)
{
    char filename[256];
    FILE *file;
    submenu_item_t *items = NULL;
    int size, count = 0, i;

    const char *dir = wOPLGetDir();
    if (!dir)
        return NULL;

    snprintf(filename, sizeof(filename), "%sfavourites.bin", dir);
    file = fopen(filename, "rb");
    if (file != NULL) {
        fseek(file, 0, SEEK_END);
        size = ftell(file);
        rewind(file);

        items = memalign(64, size);
        if (items != NULL) {
            while (ftell(file) < size) {
                fread(&items[count], sizeof(submenu_item_t), 1, file);
                int text_len;
                fread(&text_len, sizeof(int), 1, file);
                items[count].text = memalign(64, text_len);
                if (items[count].text != NULL)
                    fread(items[count].text, text_len, 1, file);
                else {
                    for (i = 0; i < count; ++i)
                        free(items[i].text);

                    free(items);
                    fclose(file);
                    return NULL;
                }

                // read owner mode and convert it to a pointer
                short int mode;
                fread(&mode, sizeof(short int), 1, file);
                items[count].owner = (void *)getFavouritesOwnerPointer(mode);

                count++;
            }
            *out_size = count * sizeof(submenu_item_t);
        }

        fclose(file);
    }

    favItemCount = count;
    return items;
}

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


void addFavouriteItem(const submenu_item_t *item)
{
    int size, i;
    submenu_item_t *items = readFavouritesFile(&size);

    if (items != NULL) {
        // add the new item
        int count = size / sizeof(submenu_item_t);
        int new_size = size + sizeof(submenu_item_t);
        submenu_item_t *new_items = memalign(64, new_size);

        if (new_items != NULL) {
            memcpy(new_items, items, size);
            memcpy((char *)new_items + size, item, sizeof(submenu_item_t));
            new_items[count].text = strdup(item->text);

            if (new_items[count].text != NULL) {
                writeFavouritesFile(new_items, new_size);
                free(new_items[count].text);
            } else
                LOG("Failed to allocate memory for new favourite text.\n");

            free(new_items);
        } else
            LOG("Failed to allocate memory for new favourite.\n");

        for (i = 0; i < count; ++i)
            free(items[i].text);
        free(items);
    } else {
        // if no existing items, create new list
        int new_size = sizeof(submenu_item_t);
        submenu_item_t *new_items = memalign(64, new_size);

        if (new_items != NULL) {
            memcpy(new_items, item, sizeof(submenu_item_t));
            new_items[0].text = strdup(item->text);

            if (new_items[0].text != NULL) {
                writeFavouritesFile(new_items, new_size);
                free(new_items[0].text);
            } else
                LOG("Failed to allocate memory for new favourite text.\n");

            free(new_items);
        } else
            LOG("Failed to allocate memory for new favourite.\n");
    }
}

static submenu_item_t *removeFavouriteItem(submenu_item_t *items, int *size, int id, const char *text, int item_size)
{
    int new_size = 0, i;
    int count = *size / item_size;
    submenu_item_t *new_items = memalign(64, *size);

    if (new_items == NULL)
        return NULL;

    for (i = 0; i < count; ++i) {
        if (items[i].id != id || (text != NULL && strcmp(items[i].text, text) != 0)) {
            memcpy((char *)new_items + new_size, &items[i], item_size);
            new_size += item_size;
        } else
            free(items[i].text);
    }

    free(items);
    *size = new_size;

    submenu_item_t *reallocated_items = memalign(64, new_size);
    if (reallocated_items != NULL) {
        memcpy(reallocated_items, new_items, new_size);
        free(new_items);
        return reallocated_items;
    } else {
        free(new_items);
        return NULL;
    }
}

void removeFavouriteByIdAndText(int id, const char *text)
{
    int size;
    submenu_item_t *items = readFavouritesFile(&size);

    if (items != NULL) {
        submenu_item_t *updated_items = removeFavouriteItem(items, &size, id, text, sizeof(submenu_item_t));
        if (updated_items != NULL) {
            writeFavouritesFile(updated_items, size);
            free(updated_items);
        } else
            LOG("Failed to update favourites.\n");
    } else
        LOG("Failed to read favourites file.\n");
}
