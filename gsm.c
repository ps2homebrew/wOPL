/**
 * Graphics Synthesizer Mode Handler Implementation
 */

#include "gsm.h"
#include <stdio.h>
#include <string.h>
#include <malloc.h>
#include <osd_config.h>
#include "util.h"
#include "system.h"
#include "config.h"

// Static global data
static GSMConfig defaultConfig = {
    .mode = GSM_MODE_DEFAULT,
    .aspect = GSM_ASPECT_4_3,
    .quality = GSM_QUALITY_MEDIUM,
    .compatibility = GSM_COMPAT_UNKNOWN,
    .features = GSM_FEATURE_BILINEAR | GSM_FEATURE_VSYNC,
    .scanlineIntensity = 50,
    .overscanX = 0,
    .overscanY = 0,
    .gamma = 1.0f,
    .frameCount = 0,
    .lastFrameTime = 0,
    .avgFrameTime = 0,
    .minFrameTime = 0xFFFFFF,
    .maxFrameTime = 0,
    .perfLevel = GSM_PERF_UNKNOWN,
    .gsmBuffer = NULL,
    .gsmBufferSize = 0,
    .textureBuffer = NULL,
    .textureBufferSize = 0,
    .lastError = GSM_ERROR_NONE,
    .errorMessage = "",
    .gameID = 0,
    .initialized = 0};

// Mode information table
static const struct
{
    const char *name;
    int width;
    int height;
    int interlaced; // 0 for progressive, 1 for interlaced
    int supported;  // Hardware support level (0 = not supported, 100 = fully supported)
} gsmModes[GSM_MODE_COUNT] = {
    {"Default", 640, 448, 1, 100}, // GSM_MODE_DEFAULT
    {"480p", 720, 480, 0, 100},    // GSM_MODE_480P
    {"576p", 720, 576, 0, 95},     // GSM_MODE_576P
    {"720p", 1280, 720, 0, 90},    // GSM_MODE_720P
    {"1080i", 1920, 1080, 1, 75},  // GSM_MODE_1080I
    {"1080p", 1920, 1080, 0, 60},  // GSM_MODE_1080P
    {"VGA 60Hz", 640, 480, 0, 95}, // GSM_MODE_VGA_60
    {"VGA 75Hz", 640, 480, 0, 85}  // GSM_MODE_VGA_75
};

// Compatibility database (could be expanded to an external file)
static const struct
{
    unsigned int gameID;
    int compatLevel;
    int recommendedMode;
} gsmCompatDB[] = {
    {0x12345678, GSM_COMPAT_PERFECT, GSM_MODE_480P},
    {0x87654321, GSM_COMPAT_HIGH, GSM_MODE_720P},
    {0x11223344, GSM_COMPAT_MEDIUM, GSM_MODE_DEFAULT},
    {0x55667788, GSM_COMPAT_LOW, GSM_MODE_DEFAULT},
    {0, GSM_COMPAT_UNKNOWN, GSM_MODE_DEFAULT} // Terminator
};

// Performance monitoring state
static int perfMonitoringActive = 0;
static int perfTimerStart = 0;

// Error message table
static const char *errorMessages[] = {
    "No error",                 // GSM_ERROR_NONE
    "GSM initialization error", // GSM_ERROR_INIT
    "Invalid parameter",        // GSM_ERROR_PARAM
    "Unsupported display mode", // GSM_ERROR_MODE
    "Memory allocation error",  // GSM_ERROR_MEMORY
    "Game compatibility issue", // GSM_ERROR_COMPAT
    "Hardware limitation",      // GSM_ERROR_HARDWARE
    "Resource allocation error" // GSM_ERROR_RESOURCE
};

// Compatibility level strings
static const char *compatStrings[] = {
    "Unknown",                        // GSM_COMPAT_UNKNOWN
    "Low - Expect issues",            // GSM_COMPAT_LOW
    "Medium - Minor issues possible", // GSM_COMPAT_MEDIUM
    "High - Should work well",        // GSM_COMPAT_HIGH
    "Perfect - Fully compatible"      // GSM_COMPAT_PERFECT
};

// Performance level strings
static const char *perfStrings[] = {
    "Poor",       // GSM_PERF_POOR
    "Acceptable", // GSM_PERF_ACCEPTABLE
    "Good",       // GSM_PERF_GOOD
    "Excellent"   // GSM_PERF_EXCELLENT
};

// Forward declarations for internal functions
static int gsmValidateModeParams(int mode, int aspect, int quality);
static void gsmUpdateCompatibility(GSMConfig *config);
static int gsmCalculateBufferRequirements(GSMConfig *config);
static int gsmSetupHardwareRegisters(GSMConfig *config);
static void gsmUpdatePerformanceStats(GSMConfig *config, int frameTime);

//------------------------------------------------------------------------------
// Core Functions
//------------------------------------------------------------------------------

/**
 * Initialize a new GSM configuration with specified parameters
 *
 * @param config Pointer to GSMConfig structure to initialize
 * @param mode Display mode to use
 * @param aspect Aspect ratio setting
 * @param quality Quality preset
 * @return GSM_ERROR_NONE on success, error code otherwise
 */
int InitGSMConfig(GSMConfig *config, int mode, int aspect, int quality)
{
    if (!config) {
        return GSM_ERROR_PARAM;
    }

    // Start with default configuration
    memcpy(config, &defaultConfig, sizeof(GSMConfig));

    // Validate and set parameters
    if (!gsmValidateModeParams(mode, aspect, quality)) {
        GSMSetError(config, GSM_ERROR_PARAM, "Invalid mode parameters");
        return GSM_ERROR_PARAM;
    }

    config->mode = mode;
    config->aspect = aspect;
    config->quality = quality;

    // Set quality-dependent defaults
    switch (quality) {
        case GSM_QUALITY_LOW:
            config->features = GSM_FEATURE_VSYNC;
            break;
        case GSM_QUALITY_MEDIUM:
            config->features = GSM_FEATURE_VSYNC | GSM_FEATURE_BILINEAR;
            break;
        case GSM_QUALITY_HIGH:
            config->features = GSM_FEATURE_VSYNC | GSM_FEATURE_BILINEAR | GSM_FEATURE_SMOOTH;
            break;
        case GSM_QUALITY_ULTRA:
            config->features = GSM_FEATURE_VSYNC | GSM_FEATURE_BILINEAR | GSM_FEATURE_SMOOTH |
                               GSM_FEATURE_GAMMA | GSM_FEATURE_SCANLINES;
            break;
    }

    // Reset performance metrics
    config->frameCount = 0;
    config->lastFrameTime = 0;
    config->avgFrameTime = 0;
    config->minFrameTime = 0xFFFFFF;
    config->maxFrameTime = 0;
    config->perfLevel = GSM_PERF_ACCEPTABLE;

    // Not initialized yet
    config->initialized = 0;

    return GSM_ERROR_NONE;
}

/**
 * Prepare GSM by allocating resources and verifying hardware compatibility
 *
 * @param config Pointer to initialized GSMConfig structure
 * @return GSM_ERROR_NONE on success, error code otherwise
 */
int PrepareGSM(GSMConfig *config)
{
    if (!config) {
        return GSM_ERROR_PARAM;
    }

    // Clear any previous errors
    GSMClearErrors(config);

    // Validate configuration
    if (GSMValidateConfig(config) != GSM_ERROR_NONE) {
        return config->lastError;
    }

    // Check hardware compatibility for selected mode
    if (gsmModes[config->mode].supported < 50) {
        GSMSetError(config, GSM_ERROR_HARDWARE,
                    "Selected display mode not supported by hardware");
        return GSM_ERROR_HARDWARE;
    }

    // Allocate memory required for this configuration
    if (GSMAllocateMemory(config) != GSM_ERROR_NONE) {
        GSMSetError(config, GSM_ERROR_MEMORY, "Failed to allocate required memory");
        return GSM_ERROR_MEMORY;
    }

    // Check game compatibility
    gsmUpdateCompatibility(config);

    // Setup hardware registers
    if (gsmSetupHardwareRegisters(config) != GSM_ERROR_NONE) {
        GSMSetError(config, GSM_ERROR_HARDWARE, "Failed to setup hardware registers");
        return GSM_ERROR_HARDWARE;
    }

    // Initialize performance monitoring
    config->frameCount = 0;
    config->avgFrameTime = 0;
    config->minFrameTime = 0xFFFFFF;
    config->maxFrameTime = 0;

    config->initialized = 1;
    return GSM_ERROR_NONE;
}

/**
 * Applies the GSM configuration to the hardware
 *
 * @param config Pointer to initialized GSMConfig structure
 * @return GSM_ERROR_NONE on success, error code otherwise
 */
int ApplyGSM(GSMConfig *config)
{
    if (!config) {
        return GSM_ERROR_PARAM;
    }

    if (!config->initialized) {
        GSMSetError(config, GSM_ERROR_INIT, "GSM not initialized");
        return GSM_ERROR_INIT;
    }

    // Apply mode-specific settings
    int width = gsmModes[config->mode].width;
    int height = gsmModes[config->mode].height;
    int interlaced = gsmModes[config->mode].interlaced;

    // Apply settings to hardware
    // (This would require direct GS register manipulation)

    // Start performance monitoring if not already started
    GSMStartPerformanceMonitoring(config);

    return GSM_ERROR_NONE;
}

/**
 * Shuts down GSM and frees resources
 *
 * @param config Pointer to GSMConfig structure
 * @return GSM_ERROR_NONE on success, error code otherwise
 */
int ShutdownGSM(GSMConfig *config)
{
    if (!config) {
        return GSM_ERROR_PARAM;
    }

    if (!config->initialized) {
        return GSM_ERROR_NONE; // Already shut down
    }

    // Free memory
    GSMFreeMemory(config);

    // Reset hardware to default state

    config->initialized = 0;
    return GSM_ERROR_NONE;
}

/**
 * Resets GSM to default configuration
 *
 * @param config Pointer to GSMConfig structure
 * @return GSM_ERROR_NONE on success, error code otherwise
 */
int ResetGSM(GSMConfig *config)
{
    if (!config) {
        return GSM_ERROR_PARAM;
    }

    int wasInitialized = config->initialized;

    // Shutdown if currently running
    if (wasInitialized) {
        ShutdownGSM(config);
    }

    // Reset to default config
    memcpy(config, &defaultConfig, sizeof(GSMConfig));

    // Reinitialize if it was previously running
    if (wasInitialized) {
        return PrepareGSM(config);
    }

    return GSM_ERROR_NONE;
}

//------------------------------------------------------------------------------
// Memory Management
//------------------------------------------------------------------------------

/**
 * Allocate memory required for GSM operation
 *
 * @param config Pointer to GSMConfig structure
 * @return GSM_ERROR_NONE on success, error code otherwise
 */
int GSMAllocateMemory(GSMConfig *config)
{
    if (!config) {
        return GSM_ERROR_PARAM;
    }

    // Free any previously allocated memory
    GSMFreeMemory(config);

    // Calculate required buffer sizes
    int bufferSize = gsmCalculateBufferRequirements(config);
    if (bufferSize <= 0) {
        GSMSetError(config, GSM_ERROR_PARAM, "Invalid buffer requirements");
        return GSM_ERROR_PARAM;
    }

    // Allocate main buffer with alignment for DMA transfers
    config->gsmBuffer = memalign(64, bufferSize);
    if (!config->gsmBuffer) {
        GSMSetError(config, GSM_ERROR_MEMORY, "Failed to allocate GSM buffer");
        return GSM_ERROR_MEMORY;
    }
    config->gsmBufferSize = bufferSize;

    // Allocate texture buffer if needed for enhanced features
    if (config->quality >= GSM_QUALITY_HIGH) {
        int textureSize = gsmModes[config->mode].width * gsmModes[config->mode].height * 4;
        config->textureBuffer = memalign(64, textureSize);
        if (!config->textureBuffer) {
            GSMSetError(config, GSM_ERROR_MEMORY, "Failed to allocate texture buffer");
            free(config->gsmBuffer);
            config->gsmBuffer = NULL;
            config->gsmBufferSize = 0;
            return GSM_ERROR_MEMORY;
        }
        config->textureBufferSize = textureSize;
    }

    return GSM_ERROR_NONE;
}

/**
 * Free memory allocated for GSM
 *
 * @param config Pointer to GSMConfig structure
 */
void GSMFreeMemory(GSMConfig *config)
{
    if (!config) {
        return;
    }

    // Free main buffer
    if (config->gsmBuffer) {
        free(config->gsmBuffer);
        config->gsmBuffer = NULL;
        config->gsmBufferSize = 0;
    }

    // Free texture buffer
    if (config->textureBuffer) {
        free(config->textureBuffer);
        config->textureBuffer = NULL;
        config->textureBufferSize = 0;
    }
}

/**
 * Check memory allocations for validity
 *
 * @param config Pointer to GSMConfig structure
 * @return 1 if memory is valid, 0 otherwise
 */
int GSMCheckMemory(GSMConfig *config)
{
    if (!config) {
        return 0;
    }

    // Check if buffers are allocated as expected
    if (config->gsmBufferSize > 0 && !config->gsmBuffer) {
        return 0;
    }

    if (config->textureBufferSize > 0 && !config->textureBuffer) {
        return 0;
    }

    // For high quality modes, ensure texture buffer is allocated
    if (config->quality >= GSM_QUALITY_HIGH && !config->textureBuffer) {
        return 0;
    }

    return 1;
}

//------------------------------------------------------------------------------
// Performance Monitoring
//------------------------------------------------------------------------------

/**
 * Start performance monitoring
 *
 * @param config Pointer to GSMConfig structure
 */
void GSMStartPerformanceMonitoring(GSMConfig *config)
{
    if (!config) {
        return;
    }

    if (!perfMonitoringActive) {
        perfMonitoringActive = 1;
        perfTimerStart = GetTimerAll();
    }

    // Reset performance statistics
    GSMResetPerformanceStats(config);
}

/**
 * Update performance statistics
 *
 * @param config Pointer to GSMConfig structure
 */
void GSMUpdatePerformance(GSMConfig *config)
{
    if (!config || !perfMonitoringActive) {
        return;
    }

    int currentTime = GetTimerAll();
    int frameTime = currentTime - perfTimerStart;
    perfTimerStart = currentTime;

    // Update performance metrics
    gsmUpdatePerformanceStats(config, frameTime);
}

/**
 * Get current performance level
 *
 * @param config Pointer to GSMConfig structure
 * @return Current performance level
 */
int GSMGetPerformanceLevel(GSMConfig *config)
{
    if (!config) {
        return GSM_PERF_POOR;
    }

    return config->perfLevel;
}

/**
 * Reset performance statistics
 *
 * @param config Pointer to GSMConfig structure
 */
void GSMResetPerformanceStats(GSMConfig *config)
{
    if (!config) {
        return;
    }

    config->frameCount = 0;
    config->lastFrameTime = 0;
    config->avgFrameTime = 0;
    config->minFrameTime = 0xFFFFFF;
    config->maxFrameTime = 0;
    config->perfLevel = GSM_PERF_ACCEPTABLE;
}

//------------------------------------------------------------------------------
// Feature Management
//------------------------------------------------------------------------------

/**
 * Enable a feature
 *
 * @param config Pointer to GSMConfig structure
 * @param feature Feature to enable
 * @return GSM_ERROR_NONE on success, error code otherwise
 */
int GSMEnableFeature(GSMConfig *config, unsigned int feature)
{
    if (!config) {
        return GSM_ERROR_PARAM;
    }

    // Check if feature is supported
    if (!GSMIsFeatureSupported(feature)) {
        GSMSetError(config, GSM_ERROR_PARAM, "Feature not supported");
        return GSM_ERROR_PARAM;
    }

    // Enable the feature
    config->features |= feature;

    return GSM_ERROR_NONE;
}

/**
 * Disable a feature
 *
 * @param config Pointer to GSMConfig structure
 * @param feature Feature to disable
 * @return GSM_ERROR_NONE on success, error code otherwise
 */
int GSMDisableFeature(GSMConfig *config, unsigned int feature)
{
    if (!config) {
        return GSM_ERROR_PARAM;
    }

    // Disable the feature
    config->features &= ~feature;

    return GSM_ERROR_NONE;
}

/**
 * Check if a feature is enabled
 *
 * @param config Pointer to GSMConfig structure
 * @param feature Feature to check
 * @return 1 if enabled, 0 otherwise
 */
int GSMIsFeatureEnabled(GSMConfig *config, unsigned int feature)
{
    if (!config) {
        return 0;
    }

    return (config->features & feature) ? 1 : 0;
}

/**
 * Check if a feature is supported by the hardware
 *
 * @param feature Feature to check
 * @return 1 if supported, 0 otherwise
 */
int GSMIsFeatureSupported(unsigned int feature)
{
    // List of supported features based on hardware capabilities
    unsigned int supportedFeatures =
        GSM_FEATURE_SCANLINES |
        GSM_FEATURE_BILINEAR |
        GSM_FEATURE_VSYNC |
        GSM_FEATURE_GAMMA;

    // SMOOTH may require more powerful hardware
    if (feature == GSM_FEATURE_SMOOTH) {
        // Add hardware capability check here
    }

    return (supportedFeatures & feature) ? 1 : 0;
}

//------------------------------------------------------------------------------
// Configuration Validation
//------------------------------------------------------------------------------

/**
 * Validate GSM configuration
 *
 * @param config Pointer to GSMConfig structure
 * @return GSM_ERROR_NONE if valid, error code otherwise
 */
int GSMValidateConfig(GSMConfig *config)
{
    if (!config) {
        return GSM_ERROR_PARAM;
    }

    // Check mode range
    if (config->mode < 0 || config->mode >= GSM_MODE_COUNT) {
        GSMSetError(config, GSM_ERROR_PARAM, "Invalid display mode");
        return GSM_ERROR_PARAM;
    }

    // Check aspect ratio range
    if (config->aspect < GSM_ASPECT_4_3 || config->aspect > GSM_ASPECT_AUTO) {
        GSMSetError(config, GSM_ERROR_PARAM, "Invalid aspect ratio");
        return GSM_ERROR_PARAM;
    }

    // Check quality range
    if (config->quality < GSM_QUALITY_LOW || config->quality > GSM_QUALITY_ULTRA) {
        GSMSetError(config, GSM_ERROR_PARAM, "Invalid quality setting");
        return GSM_ERROR_PARAM;
    }

    // Check scanline intensity
    if (config->scanlineIntensity < 0 || config->scanlineIntensity > 100) {
        GSMSetError(config, GSM_ERROR_PARAM, "Invalid scanline intensity");
        return GSM_ERROR_PARAM;
    }

    // Check gamma value range
    if (config->gamma < 0.5f || config->gamma > 2.0f) {
        GSMSetError(config, GSM_ERROR_PARAM, "Invalid gamma value");
        return GSM_ERROR_PARAM;
    }

    // Validate feature compatibility
    if ((config->features & GSM_FEATURE_SMOOTH) && config->quality < GSM_QUALITY_HIGH) {
        GSMSetError(config, GSM_ERROR_PARAM, "Smooth feature requires HIGH quality or better");
        return GSM_ERROR_PARAM;
    }

    return GSM_ERROR_NONE;
}

//------------------------------------------------------------------------------
// Internal Helper Functions
//------------------------------------------------------------------------------

/**
 * Validate mode parameters
 *
 * @param mode Display mode
 * @param aspect Aspect ratio
 * @param quality Quality preset
 * @return 1 if valid, 0 otherwise
 */
static int gsmValidateModeParams(int mode, int aspect, int quality)
{
    // Check mode range
    if (mode < 0 || mode >= GSM_MODE_COUNT) {
        return 0;
    }

    // Check aspect ratio range
    if (aspect < GSM_ASPECT_4_3 || aspect > GSM_ASPECT_AUTO) {
        return 0;
    }

    // Check quality range
    if (quality < GSM_QUALITY_LOW || quality > GSM_QUALITY_ULTRA) {
        return 0;
    }

    // Advanced validation:
    // Check if the mode is supported by hardware
    if (gsmModes[mode].supported < 50) {
        return 0;
    }

    return 1;
}

/**
 * Update compatibility information based on game ID
 *
 * @param config Pointer to GSMConfig structure
 */
static void gsmUpdateCompatibility(GSMConfig *config)
{
    if (!config) {
        return;
    }

    // If no game ID is set, keep unknown compatibility
    if (config->gameID == 0) {
        config->compatibility = GSM_COMPAT_UNKNOWN;
        return;
    }

    // Search in compatibility database
    int i = 0;
    while (gsmCompatDB[i].gameID != 0) {
        if (gsmCompatDB[i].gameID == config->gameID) {
            config->compatibility = gsmCompatDB[i].compatLevel;

    // If current mode is default, use
