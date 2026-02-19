#ifndef __POPSTARTER_H
#define __POPSTARTER_H

#include "include/opl.h"

// Resolution modes
typedef enum {
    POPS_RES_NATIVE, // Native PS1 resolution (320x240)
    POPS_RES_480P,   // Progressive scan 480p
    POPS_RES_720P,   // HD 720p
    POPS_RES_1080I,  // HD 1080i
    POPS_RES_AUTO    // Auto detect best mode
} POPS_RESOLUTION;

// Aspect ratio modes
typedef enum {
    POPS_ASPECT_4_3,  // Original 4:3 aspect ratio
    POPS_ASPECT_16_9, // Widescreen 16:9
    POPS_ASPECT_AUTO  // Auto detect based on game
} POPS_ASPECT_RATIO;

// Display quality settings
typedef enum {
    POPS_QUALITY_FAST,     // Fast rendering, minimal filtering
    POPS_QUALITY_BALANCED, // Balanced performance/quality
    POPS_QUALITY_SMOOTH    // Best quality, full filtering
} POPS_QUALITY;

// Visual enhancement options
typedef struct
{
    int enableScanlines;   // Enable CRT scanline effect
    int scanlineIntensity; // Intensity of scanlines (0-100)
    int bilinearFilter;    // Enable bilinear filtering
    int enhanceTextures;   // Enhance/sharpen textures
    int smoothPolygons;    // Smooth polygon edges
} POPS_VISUAL_OPTIONS;

// POPSTARTER settings
typedef struct
{
    int enabled;                       // POPSTARTER enabled
    POPS_RESOLUTION resolution;        // Resolution mode
    POPS_ASPECT_RATIO aspectRatio;     // Aspect ratio setting
    POPS_QUALITY quality;              // Quality preset
    POPS_VISUAL_OPTIONS visualOptions; // Visual enhancement options
    char configFile[128];              // Path to config file
} POPS_CONFIG;

// Global POPSTARTER config
extern POPS_CONFIG popsConfig;

// Initialize POPSTARTER
int popsInit(void);

// Load POPSTARTER configuration
int popsLoadConfig(void);

// Save POPSTARTER configuration
int popsSaveConfig(void);

// Set default POPSTARTER settings
void popsSetDefaultConfig(void);

// Apply POPSTARTER resolution settings
int popsApplyResolutionSettings(void);

// Launch game with POPSTARTER
int popsLaunchGame(const char *vcdPath, config_set_t *configSet);

// Check if a file is a valid PS1 VCD
int popsIsValidVCD(const char *path);

// Update PS1 game compatibility database
int popsUpdateCompatibility(void);

#endif // __POPSTARTER_H
