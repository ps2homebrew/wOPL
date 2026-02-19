// GUI implementation for diagnostic features
#include "include/opl.h"
#include "include/dialogs.h"
#include "include/gui.h"
#include "include/textures.h"
#include "include/system.h"

// Diagnostic status icons
static GSTEXTURE diagIconNotRun;
static GSTEXTURE diagIconRunning;
static GSTEXTURE diagIconPassed;
static GSTEXTURE diagIconFailed;
static GSTEXTURE diagIconWarning;

// Extern diagnostic results from dialogs
