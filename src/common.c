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
#include <libmc.h>

// frame counter
unsigned int frameCounter;

// Global data

int gEnableArt;
int gPS2Logo;
int gDefaultDevice;
int gEnableWrite;
int gRememberLastPlayed;

void reset(void)
{
    sysReset();

    mcInit(MC_TYPE_XMC);
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