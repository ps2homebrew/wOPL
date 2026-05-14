// this need to be on top since some include miss including some headers
#include "include/common.h"

#include "include/config.h"
#include "include/extern_irx.h"
#include "include/gui.h"   // guiExecDeferredOps
#include "include/ioman.h" // ioBlockOps
#include "include/lang.h"
#include "include/lwnbd.h"
#include "include/sound.h"
#include "include/system.h"
#include <libpad.h>
#include "include/pad.h"

#include <sifrpc.h>
#include <kernel.h>

static struct lwnbd_config config;

char gExportName[32];

void reset(void);

static int loadLwnbdSvr(void)
{
    int ret, padStatus;

    // deint audio lib while nbd server is running
    audioEnd();

    // block all io ops, wait for the ones still running to finish
    ioBlockOps(1);
    guiExecDeferredOps();

    // Deinitialize all support without shutting down the HDD unit.
    deinitAllSupport(NO_EXCEPTION, IO_MODE_SELECTED_ALL);
    guiClearErrorMessage(); /* At this point, an error might have been displayed (since background tasks were completed).
                            Clear it, otherwise it will get displayed after the server is closed. */

    unloadPads();
    // sysReset(0); // usefull ? printf doesn't work with it.

    /* compat stuff for user not providing name export (useless when there was only one export) */
    ret = strlen(gExportName);
    if (ret == 0)
        strcpy(config.defaultexport, "hdd0");
    else
        strcpy(config.defaultexport, gExportName);

    config.readonly = !gEnableWrite;

    // see gETHStartMode, gNetworkStartup ? this is slow, so if we don't have to do it (like debug build).
    ret = ethLoadInitModules();
    if (ret == 0) {
        ret = sysLoadModuleBuffer(&ps2atad_irx, size_ps2atad_irx, 0, NULL); /* gHDDStartMode ? */
        if (ret >= 0) {
            ret = sysLoadModuleBuffer(&lwnbdsvr_irx, size_lwnbdsvr_irx, sizeof(config), (char *)&config);
            if (ret >= 0)
                ret = 0;
        }
    }

    padInit(0);

    // init all pads
    padStatus = 0;
    while (!padStatus)
        padStatus = startPads();

    // now ready to display some status

    return ret;
}

static void unloadLwnbdSvr(void)
{
    ethDeinitModules();
    unloadPads();

    reset();

    LOG_INIT();
    LOG_ENABLE();

    // reinit the input pads
    padInit(0);

    int ret = 0;
    while (!ret)
        ret = startPads();

    // now start io again
    ioBlockOps(0);

    // init all supports again
    initAllSupport(1);

    audioInit();
    sfxInit(0);
    if (gEnableBGM)
        bgmStart();
}

void handleLwnbdSrv()
{
    char temp[256];
    // prepare for lwnbd, display screen with info
    guiRenderTextScreen(_l(_STR_STARTINGNBD));
    if (loadLwnbdSvr() == 0) {
        snprintf(temp, sizeof(temp), "%s", _l(_STR_RUNNINGNBD));
        guiMsgBox(temp, 0, NULL);
    } else
        guiMsgBox(_l(_STR_STARTFAILNBD), 0, NULL);

    // restore normal functionality again
    guiRenderTextScreen(_l(_STR_UNLOADNBD));
    unloadLwnbdSvr();
}