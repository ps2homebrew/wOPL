/**
 * Graphics Synthesizer Mode Handler
 *
 * Provides resolution enhancement features for PS2 games on modern HDTVs
 * Implements dynamic mode switching, compatibility checking, and performance monitoring
 */

#ifndef GSM_H
#define GSM_H

#include <tamtypes.h>
#include <kernel.h>
#include <gsKit.h>

// GSM Mode definitions
#define GSM_MODE_DEFAULT 0
#define GSM_MODE_480P    1
#define GSM_MODE_576P    2
#define GSM_MODE_720P    3
#define GSM_MODE_1080I   4
#define GSM_MODE_1080P   5
#define GSM_MODE_VGA_60  6
#define GSM_MODE_VGA_75  7
#define GSM_MODE_COUNT   8

// Aspect Ratio definitions
#define GSM_ASPECT_4_3  0
#define GSM_ASPECT_16_9 1
#define GSM_ASPECT_AUTO 2

// Quality Presets
#define GSM_QUALITY_LOW    0
#define GSM_QUALITY_MEDIUM 1
#define GSM_QUALITY_HIGH   2
#define GSM_QUALITY_ULTRA  3

// Compatibility status
#define GSM_COMPAT_UNKNOWN 0
#define GSM_COMPAT_LOW     1
#define GSM_COMPAT_MEDIUM  2
#define GSM_COMPAT_HIGH    3
#define GSM_COMPAT_PERFECT 4

// Error codes
#define GSM_ERROR_NONE     0
#define GSM_ERROR_INIT     1
#define GSM_ERROR_PARAM    2
#define GSM_ERROR_MODE     3
#define GSM_ERROR_MEMORY   4
#define GSM_ERROR_COMPAT   5
#define GSM_ERROR_HARDWARE 6
#define GSM_ERROR_RESOURCE 7

// Performance thresholds
#define GSM_PERF_POOR       0
#define GSM_PERF_ACCEPTABLE 1
#define GSM_PERF_GOOD       2
#define GSM_PERF_EXCELLENT  3

// Feature flags
#define GSM_FEATURE_SCANLINES (1 << 0)
#define GSM_FEATURE_BILINEAR  (1 << 1)
#define GSM_FEATURE_SMOOTH    (1 << 2)
#define GSM_FEATURE_VSYNC     (1 << 3)
#define GSM_FEATURE_GAMMA     (1 << 4)
#define GSM_FEATURE_OVERSCAN  (1 << 5)
#define GSM_FEATURE_DEBUG     (1 << 15)

typedef struct
{
    int mode;              // Current GSM mode
    int aspect;            // Aspect ratio setting
    int quality;           // Quality preset
    int compatibility;     // Compatibility status
    unsigned int features; // Enabled features bitfield

    // Advanced settings
    int scanlineIntensity; // 0-100%
    int overscanX;         // X overscan compensation
    int overscanY;         // Y overscan compensation
    float gamma;           // Gamma correction value

    // Performance monitoring
    int frameCount;    // Frame counter for performance
    int lastFrameTime; // Last frame time in microseconds
    int avgFrameTime;  // Average frame time
    int minFrameTime;  // Minimum frame time
    int maxFrameTime;  // Maximum frame time
    int perfLevel;     // Current performance level

    // Memory tracking
    void *gsmBuffer;       // Main GSM buffer
    int gsmBufferSize;     // Size of GSM buffer
    void *textureBuffer;   // Texture buffer if needed
    int textureBufferSize; // Size of texture buffer

    // Error handling
    int lastError;          // Last error code
    char errorMessage[128]; // Detailed error message

    // Game-specific configuration
    unsigned int gameID; // Game ID for per-game settings
    int initialized;     // Whether GSM is initialized
} GSMConfig;

// Core functions
int InitGSMConfig(GSMConfig *config, int mode, int aspect, int quality);
int PrepareGSM(GSMConfig *config);
int ApplyGSM(GSMConfig *config);
int ShutdownGSM(GSMConfig *config);
int ResetGSM(GSMConfig *config);

// Memory management
int GSMAllocateMemory(GSMConfig *config);
void GSMFreeMemory(GSMConfig *config);
int GSMCheckMemory(GSMConfig *config);

// Mode management
int GSMSetMode(GSMConfig *config, int mode);
int GSMGetModeWidth(int mode);
int GSMGetModeHeight(int mode);
const char *GSMGetModeName(int mode);
int GSMIsModeSupported(int mode);

// Performance monitoring
void GSMStartPerformanceMonitoring(GSMConfig *config);
void GSMUpdatePerformance(GSMConfig *config);
int GSMGetPerformanceLevel(GSMConfig *config);
void GSMResetPerformanceStats(GSMConfig *config);

// Compatibility functions
int GSMCheckCompatibility(GSMConfig *config, unsigned int gameID);
int GSMGetRecommendedMode(unsigned int gameID);
const char *GSMGetCompatibilityString(int compatLevel);

// Feature management
int GSMEnableFeature(GSMConfig *config, unsigned int feature);
int GSMDisableFeature(GSMConfig *config, unsigned int feature);
int GSMIsFeatureEnabled(GSMConfig *config, unsigned int feature);
int GSMIsFeatureSupported(unsigned int feature);

// Error handling
int GSMGetLastError(GSMConfig *config);
const char *GSMGetErrorMessage(GSMConfig *config);
void GSMClearErrors(GSMConfig *config);
void GSMSetError(GSMConfig *config, int error, const char *message);

// Utility functions
GSMConfig *GSMGetDefaultConfig(void);
int GSMSaveConfig(GSMConfig *config, const char *path);
int GSMLoadConfig(GSMConfig *config, const char *path);
int GSMValidateConfig(GSMConfig *config);

#endif /* GSM_H */
