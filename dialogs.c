// Diagnostic menu handlers and integration
#include "include/opl.h"
#include "include/dialogs.h"
#include "include/gui.h"
#include "include/system.h"
#include "include/ioman.h"
#include <stdio.h>

// Diagnostic test statuses
typedef enum {
    DIAG_STATUS_NOT_RUN,
    DIAG_STATUS_RUNNING,
    DIAG_STATUS_PASSED,
    DIAG_STATUS_FAILED,
    DIAG_STATUS_WARNING
} DiagnosticStatus;

// Diagnostic test results
typedef struct
{
    DiagnosticStatus status;
    char message[128];
} DiagnosticResult;

// Store diagnostic results
static DiagnosticResult networkResult = {DIAG_STATUS_NOT_RUN, ""};
static DiagnosticResult fileSystemResult = {DIAG_STATUS_NOT_RUN, ""};
static DiagnosticResult memoryResult = {DIAG_STATUS_NOT_RUN, ""};
static DiagnosticResult configResult = {DIAG_STATUS_NOT_RUN, ""};
static DiagnosticResult performanceResult = {DIAG_STATUS_NOT_RUN, ""};

// Test Network Connectivity
static int diagTestNetworkConnectivity(int id)
{
    // Set status to running
    networkResult.status = DIAG_STATUS_RUNNING;
    guiUpdateDiagnosticStatus();

    // Perform network connectivity test
    int result = ethCheckConnection();

    if (result == 1) {
        // Connection successful
        networkResult.status = DIAG_STATUS_PASSED;
        snprintf(networkResult.message, sizeof(networkResult.message),
                 "Network connection established successfully.");
    } else if (result == 0) {
        // No connection but network adapter detected
        networkResult.status = DIAG_STATUS_WARNING;
        snprintf(networkResult.message, sizeof(networkResult.message),
                 "Network adapter detected but no connection established.");
    } else {
        // No network adapter detected
        networkResult.status = DIAG_STATUS_FAILED;
        snprintf(networkResult.message, sizeof(networkResult.message),
                 "Network adapter not detected.");
    }

    guiUpdateDiagnosticStatus();
    return 0;
}

// Test File System
static int diagTestFileSystem(int id)
{
    // Set status to running
    fileSystemResult.status = DIAG_STATUS_RUNNING;
    guiUpdateDiagnosticStatus();

    // Check memory card file system
    int mc0_status = sysCheckMC(0);
    int mc1_status = sysCheckMC(1);

    if (mc0_status == 1 && mc1_status == 1) {
        fileSystemResult.status = DIAG_STATUS_PASSED;
        snprintf(fileSystemResult.message, sizeof(fileSystemResult.message),
                 "Both memory cards are accessible and working properly.");
    } else if (mc0_status == 1 || mc1_status == 1) {
        fileSystemResult.status = DIAG_STATUS_WARNING;
        snprintf(fileSystemResult.message, sizeof(fileSystemResult.message),
                 "Memory card in slot %d is working. Slot %d unavailable.",
                 (mc0_status == 1) ? 1 : 2, (mc0_status != 1) ? 1 : 2);
    } else {
        fileSystemResult.status = DIAG_STATUS_FAILED;
        snprintf(fileSystemResult.message, sizeof(fileSystemResult.message),
                 "No memory cards detected or accessible.");
    }

    guiUpdateDiagnosticStatus();
    return 0;
}

// Test Memory
static int diagTestMemory(int id)
{
    // Set status to running
    memoryResult.status = DIAG_STATUS_RUNNING;
    guiUpdateDiagnosticStatus();

    // Check available memory
    int freeMemory = GetFreeMemory();

    if (freeMemory > 1024 * 1024) { // More than 1MB free
        memoryResult.status = DIAG_STATUS_PASSED;
        snprintf(memoryResult.message, sizeof(memoryResult.message),
                 "Memory test passed. %d KB free.", freeMemory / 1024);
    } else if (freeMemory > 512 * 1024) { // Between 512KB and 1MB
        memoryResult.status = DIAG_STATUS_WARNING;
        snprintf(memoryResult.message, sizeof(memoryResult.message),
                 "Memory is limited. %d KB free.", freeMemory / 1024);
    } else { // Less than 512KB
        memoryResult.status = DIAG_STATUS_FAILED;
        snprintf(memoryResult.message, sizeof(memoryResult.message),
                 "Memory critically low. %d KB free.", freeMemory / 1024);
    }

    guiUpdateDiagnosticStatus();
    return 0;
}

// Test Configuration
static int diagTestConfiguration(int id)
{
    // Set status to running
    configResult.status = DIAG_STATUS_RUNNING;
    guiUpdateDiagnosticStatus();

    // Check configuration file integrity
    int result = configCheck();

    if (result == 0) {
        configResult.status = DIAG_STATUS_PASSED;
        snprintf(configResult.message, sizeof(configResult.message),
                 "Configuration files are valid.");
    } else if (result == 1) {
        configResult.status = DIAG_STATUS_WARNING;
        snprintf(configResult.message, sizeof(configResult.message),
                 "Minor configuration issues detected but operable.");
    } else {
        configResult.status = DIAG_STATUS_FAILED;
        snprintf(configResult.message, sizeof(configResult.message),
                 "Configuration files corrupted or missing.");
    }

    guiUpdateDiagnosticStatus();
    return 0;
}

// Test Performance
static int diagTestPerformance(int id)
{
    // Set status to running
    performanceResult.status = DIAG_STATUS_RUNNING;
    guiUpdateDiagnosticStatus();

    // Run performance benchmark
    int score = perfBenchmark();

    if (score > 800) { // Good performance
        performanceResult.status = DIAG_STATUS_PASSED;
        snprintf(performanceResult.message, sizeof(performanceResult.message),
                 "Performance score: %d - Good", score);
    } else if (score > 500) { // Average performance
        performanceResult.status = DIAG_STATUS_WARNING;
        snprintf(performanceResult.message, sizeof(performanceResult.message),
                 "Performance score: %d - Average", score);
    } else { // Poor performance
        performanceResult.status = DIAG_STATUS_FAILED;
        snprintf(performanceResult.message, sizeof(performanceResult.message),
                 "Performance score: %d - Poor", score);
    }

    guiUpdateDiagnosticStatus();
    return 0;
}

// Repair Network Settings
static int diagRepairNetwork(int id)
{
    // Reset network settings to defaults
    ethLoadDefaultConfig();

    // Apply and save settings
    ethApplyConfig();
    saveConfig(CONFIG_NETWORK, 1);

    // Update status
    networkResult.status = DIAG_STATUS_NOT_RUN;
    snprintf(networkResult.message, sizeof(networkResult.message),
             "Network settings reset to defaults. Please test again.");

    guiUpdateDiagnosticStatus();
    return 0;
}

// Repair File System
static int diagRepairFileSystem(int id)
{
    // Attempt to repair file system
    int result = fsckMC(id - DIAGNOSTIC_REPAIR_FS_MC0);

    if (result == 0) {
        fileSystemResult.status = DIAG_STATUS_PASSED;
        snprintf(fileSystemResult.message, sizeof(fileSystemResult.message),
                 "File system repair completed successfully.");
    } else {
        fileSystemResult.status = DIAG_STATUS_FAILED;
        snprintf(fileSystemResult.message, sizeof(fileSystemResult.message),
                 "File system repair failed. Try formatting memory card.");
    }

    guiUpdateDiagnosticStatus();
    return 0;
}

// Repair Configuration
static int diagRepairConfig(int id)
{
    // Reset configuration to defaults
    configResetAll();
    saveConfig(CONFIG_ALL, 1);

    // Update status
    configResult.status = DIAG_STATUS_PASSED;
    snprintf(configResult.message, sizeof(configResult.message),
             "Configuration reset to defaults.");

    guiUpdateDiagnosticStatus();
    return 0;
}

// Run All Diagnostics
static int diagRunAllTests(int id)
{
    // Run all diagnostic tests sequentially
    diagTestNetworkConnectivity(0);
    diagTestFileSystem(0);
    diagTestMemory(0);
    diagTestConfiguration(0);
    diagTestPerformance(0);

    return 0;
}

// Diagnostic menu item IDs
enum {
    DIAGNOSTIC_TEST_NETWORK = 1,
    DIAGNOSTIC_TEST_FILESYSTEM,
    DIAGNOSTIC_TEST_MEMORY,
    DIAGNOSTIC_TEST_CONFIG,
    DIAGNOSTIC_TEST_PERFORMANCE,
    DIAGNOSTIC_TEST_ALL,
    DIAGNOSTIC_REPAIR_NETWORK,
    DIAGNOSTIC_REPAIR_FS_MC0,
    DIAGNOSTIC_REPAIR_FS_MC1,
    DIAGNOSTIC_REPAIR_CONFIG,
};

// Initialize the diagnostic menu
void initDiagnosticMenu(void)
{
    // Reset all diagnostic results
    networkResult.status = DIAG_STATUS_NOT_RUN;
    fileSystemResult.status = DIAG_STATUS_NOT_RUN;
    memoryResult.status = DIAG_STATUS_NOT_RUN;
    configResult.status = DIAG_STATUS_NOT_RUN;
    performanceResult.status = DIAG_STATUS_NOT_RUN;

    // Clear all messages
    networkResult.message[0] = '\0';
    fileSystemResult.message[0] = '\0';
    memoryResult.message[0] = '\0';
    configResult.message[0] = '\0';
    performanceResult.message[0] = '\0';
}

// Update diagnostic result in GUI
void updateDiagnosticResult(DiagnosticStatus status, const char *message)
{
    guiDraw2dText(20, 200, 0, 0, message);

    // Draw status indicator
    switch (status) {
        case DIAG_STATUS_PASSED:
            guiDraw2dText(380, 200, 0, 0, "PASSED");
            break;
        case DIAG_STATUS_FAILED:
            guiDraw2dText(380, 200, 0, 0, "FAILED");
            break;
        case DIAG_STATUS_WARNING:
            guiDraw2dText(380, 200, 0, 0, "WARNING");
            break;
        case DIAG_STATUS_RUNNING:
            guiDraw2dText(380, 200, 0, 0, "RUNNING...");
            break;
        default:
            guiDraw2dText(380, 200, 0, 0, "NOT RUN");
            break;
    }
}

// Get status color based on diagnostic status
int getDiagnosticStatusColor(DiagnosticStatus status)
{
    switch (status) {
        case DIAG_STATUS_PASSED:
            return GS_SETREG_RGBA(0, 255, 0, 128); // Green
        case DIAG_STATUS_FAILED:
            return GS_SETREG_RGBA(255, 0, 0, 128); // Red
        case DIAG_STATUS_WARNING:
            return GS_SETREG_RGBA(255, 255, 0, 128); // Yellow
        case DIAG_STATUS_RUNNING:
            return GS_SETREG_RGBA(0, 128, 255, 128); // Blue
        default:
            return GS_SETREG_RGBA(128, 128, 128, 128); // Gray
    }
}

// Update diagnostic result status
void updateDiagnosticStatus(DiagnosticResult *result, DiagnosticStatus status, const char *message)
{
    result->status = status;
    if (message) {
        snprintf(result->message, sizeof(result->message), "%s", message);
    }
    guiUpdateDiagnosticStatus();
}

// Create the diagnostic menu
void diaCreateDiagnosticMenu(void)
{
    diaAddMenu(mainMenu, -1, "Diagnostics", NULL, NULL, DIAG_ICON);

    // Add test menu items
    diaAddMenuItem(mainMenu, "Diagnostics", "Test Network", DIAGNOSTIC_TEST_NETWORK,
                   diagTestNetworkConnectivity, NULL, NULL);
    diaAddMenuItem(mainMenu, "Diagnostics", "Test File System", DIAGNOSTIC_TEST_FILESYSTEM,
                   diagTestFileSystem, NULL, NULL);
    diaAddMenuItem(mainMenu, "Diagnostics", "Test Memory", DIAGNOSTIC_TEST_MEMORY,
                   diagTestMemory, NULL, NULL);
    diaAddMenuItem(mainMenu, "Diagnostics", "Test Configuration", DIAGNOSTIC_TEST_CONFIG,
                   diagTestConfiguration, NULL, NULL);
    diaAddMenuItem(mainMenu, "Diagnostics", "Test Performance", DIAGNOSTIC_TEST_PERFORMANCE,
                   diagTestPerformance, NULL, NULL);
    diaAddMenuItem(mainMenu, "Diagnostics", "Run All Tests", DIAGNOSTIC_TEST_ALL,
                   diagRunAllTests, NULL, NULL);

    // Add repair menu items
    diaAddMenuItem(mainMenu, "Diagnostics", "Repair Network Settings", DIAGNOSTIC_REPAIR_NETWORK,
                   diagRepairNetwork, NULL, NULL);
    diaAddMenuItem(mainMenu, "Diagnostics", "Repair MC0 File System", DIAGNOSTIC_REPAIR_FS_MC0,
                   diagRepairFileSystem, NULL, NULL);
    diaAddMenuItem(mainMenu, "Diagnostics", "Repair MC1 File System", DIAGNOSTIC_REPAIR_FS_MC1,
                   diagRepairFileSystem, NULL, NULL);
    diaAddMenuItem(mainMenu, "Diagnostics", "Reset Configuration", DIAGNOSTIC_REPAIR_CONFIG,
                   diagRepairConfig, NULL, NULL);
}

// Helper function to update resolution menu with current settings
void guiUpdateResolutionMenu(void)
{
    // Resolution option text
    static const char *resolutionLabels[] = {
        "640x480 (Progressive)",
        "720x480 (Progressive)",
        "1280x720 (HD)",
        "1920x1080 (Full HD)",
        "Original PS1"};

    // Aspect ratio text
    static const char *aspectLabels[] = {
        "4:3 (Original)",
        "16:9 (Widescreen)",
        "Auto Detect"};

    // Quality text
    static const char *qualityLabels[] = {
        "Low (Fast)",
        "Medium",
        "High (Default)",
        "Ultra (Slow)"};

    // Update menu items with current settings
    char buffer[64];

    // Resolution
    snprintf(buffer, sizeof(buffer), "Resolution: %s", resolutionLabels[popsConfig.resolution]);
    diaSetMenuItemLabel(mainMenu, "POPSTARTER Settings", "Resolution", buffer);

    // Aspect Ratio
    snprintf(buffer, sizeof(buffer), "Aspect Ratio: %s", aspectLabels[popsConfig.aspectRatio]);
    diaSetMenuItemLabel(mainMenu, "POPSTARTER Settings", "Aspect Ratio", buffer);

    // Quality
    snprintf(buffer, sizeof(buffer), "Quality: %s", qualityLabels[popsConfig.quality]);
    diaSetMenuItemLabel(mainMenu, "POPSTARTER Settings", "Quality", buffer);

    // Scanlines
    snprintf(buffer, sizeof(buffer), "Scanlines: %s", popsConfig.scanlines ? "On" : "Off");
    diaSetMenuItemLabel(mainMenu, "POPSTARTER Settings", "Scanlines", buffer);

    // Scanline Intensity
    snprintf(buffer, sizeof(buffer), "Scanline Intensity: %d", popsConfig.scanlineIntensity + 1);
    diaSetMenuItemLabel(mainMenu, "POPSTARTER Settings", "Scanline Intensity", buffer);

    // Bilinear Filter
    snprintf(buffer, sizeof(buffer), "Bilinear Filter: %s", popsConfig.bilinearFilter ? "On" : "Off");
    diaSetMenuItemLabel(mainMenu, "POPSTARTER Settings", "Bilinear Filter", buffer);

    // Enhance Textures
    snprintf(buffer, sizeof(buffer), "Enhance Textures: %s", popsConfig.enhanceTextures ? "On" : "Off");
    diaSetMenuItemLabel(mainMenu, "POPSTARTER Settings", "Enhance Textures", buffer);

    // Smooth Polygons
    snprintf(buffer, sizeof(buffer), "Smooth Polygons: %s", popsConfig.smoothPolygons ? "On" : "Off");
    diaSetMenuItemLabel(mainMenu, "POPSTARTER Settings", "Smooth Polygons", buffer);

    // Enable/disable controls based on main toggle
    guiEnableResolutionMenu(popsConfig.enabled);
}

// Enable/disable resolution menu items based on main toggle
void guiEnableResolutionMenu(int enabled)
{
    guiEnableMenuItem(mainMenu, "POPSTARTER Settings", "Resolution", enabled);
    guiEnableMenuItem(mainMenu, "POPSTARTER Settings", "Aspect Ratio", enabled);
    guiEnableMenuItem(mainMenu, "POPSTARTER Settings", "Quality", enabled);
    guiEnableMenuItem(mainMenu, "POPSTARTER Settings", "Scanlines", enabled);
    guiEnableMenuItem(mainMenu, "POPSTARTER Settings", "Scanline Intensity", enabled && popsConfig.scanlines);
    guiEnableMenuItem(mainMenu, "POPSTARTER Settings", "Bilinear Filter", enabled);
    guiEnableMenuItem(mainMenu, "POPSTARTER Settings", "Enhance Textures", enabled);
    guiEnableMenuItem(mainMenu, "POPSTARTER Settings", "Smooth Polygons", enabled);
    guiEnableMenuItem(mainMenu, "POPSTARTER Settings", "Apply Settings", enabled);
    guiEnableMenuItem(mainMenu, "POPSTARTER Settings", "Reset to Defaults", enabled);
}

// Create the POPSTARTER resolution settings menu
void diaCreateResolutionMenu(void)
{
    // Add the parent menu item
    diaAddMenu(mainMenu, -1, "POPSTARTER Settings", NULL, NULL, POPS_ICON);

    // Add resolution setting menu items
    diaAddMenuItem(mainMenu, "POPSTARTER Settings", "Enable Enhanced Resolution", POPS_SETTING_ENABLE,
                   popsToggleEnable, NULL, NULL);

    // Separator
    diaAddMenuSeparator(mainMenu, "POPSTARTER Settings");

    // Resolution options
    diaAddMenuItem(mainMenu, "POPSTARTER Settings", "Resolution", POPS_SETTING_RESOLUTION,
                   popsChangeResolution, NULL, NULL);
    diaAddMenuItem(mainMenu, "POPSTARTER Settings", "Aspect Ratio", POPS_SETTING_ASPECT_RATIO,
                   popsChangeAspectRatio, NULL, NULL);
    diaAddMenuItem(mainMenu, "POPSTARTER Settings", "Quality", POPS_SETTING_QUALITY,
                   popsChangeQuality, NULL, NULL);

    // Separator
    diaAddMenuSeparator(mainMenu, "POPSTARTER Settings");

    // Visual enhancements
    diaAddMenuItem(mainMenu, "POPSTARTER Settings", "Scanlines", POPS_SETTING_SCANLINES_ENABLE,
                   popsToggleScanlines, NULL, NULL);
    diaAddMenuItem(mainMenu, "POPSTARTER Settings", "Scanline Intensity", POPS_SETTING_SCANLINES_INTENSITY,
                   popsChangeScanlineIntensity, NULL, NULL);
    diaAddMenuItem(mainMenu, "POPSTARTER Settings", "Bilinear Filter", POPS_SETTING_BILINEAR_FILTER,
                   popsToggleBilinearFilter, NULL, NULL);
    diaAddMenuItem(mainMenu, "POPSTARTER Settings", "Enhance Textures", POPS_SETTING_ENHANCE_TEXTURES,
                   popsToggleEnhanceTextures, NULL, NULL);
    diaAddMenuItem(mainMenu, "POPSTARTER Settings", "Smooth Polygons", POPS_SETTING_SMOOTH_POLYGONS,
                   popsToggleSmoothPolygons, NULL, NULL);

    // Separator
    diaAddMenuSeparator(mainMenu, "POPSTARTER Settings");

    // Apply/Reset
    diaAddMenuItem(mainMenu, "POPSTARTER Settings", "Apply Settings", POPS_SETTING_APPLY,
                   popsApplySettings, NULL, NULL);
    diaAddMenuItem(mainMenu, "POPSTARTER Settings", "Reset to Defaults", POPS_SETTING_DEFAULT,
                   popsResetDefaults, NULL, NULL);

    // Set initial values
    guiUpdateResolutionMenu();
}

// Draw current resolution settings in preview window
void drawResolutionPreview(int x, int y, int width, int height)
{
    // Only draw if enabled
    if (!popsConfig.enabled)
        return;

    // Draw background
    guiDrawFilledBox(x, y, width, height, GS_SETREG_RGBA(0, 0, 0, 128));

    // Draw border
    guiDrawOutlinedBox(x, y, width, height, GS_SETREG_RGBA(255, 255, 255, 128));

    // Draw title
    guiDraw2dText(x + 10, y + 10, 12, GS_SETREG_RGBA(255, 255, 255, 255), "Resolution Preview");

    // Draw resolution preview representation
    int previewX = x + 20;
    int previewY = y + 40;
    int previewWidth, previewHeight;

    // Set preview dimensions based on resolution and aspect ratio
    switch (popsConfig.aspectRatio) {
        case POPS_ASPECT_4_3:
            previewWidth = 120;
            previewHeight = 90;
            break;
        case POPS_ASPECT_16_9:
            previewWidth = 160;
            previewHeight = 90;
            break;
        default:
            previewWidth = 120;
            previewHeight = 90;
    }

    // Draw the preview box
    guiDrawFilledBox(previewX, previewY, previewWidth, previewHeight,
                     GS_SETREG_RGBA(64, 64, 192, 255));

    // Draw scan lines if enabled
    if (popsConfig.scanlines) {
        int intensity = 100 + (popsConfig.scanlineIntensity * 30);
        int spacing = 2 + popsConfig.scanlineIntensity;

        for (int i = 0; i < previewHeight; i += spacing) {
            guiDrawLine(previewX, previewY + i, previewX + previewWidth, previewY + i,
                        GS_SETREG_RGBA(0, 0, 0, intensity));
        }
    }

    // Draw resolution text
    char resText[32];
    static const char *resLabels[] = {
        "640x480p",
        "720x480p",
        "1280x720p",
        "1920x1080p",
        "Original"};

    snprintf(resText, sizeof(resText), "Mode: %s", resLabels[popsConfig.resolution]);
    guiDraw2dText(previewX, previewY + previewHeight + 15, 10,
                  GS_SETREG_RGBA(255, 255, 255, 255), resText);

    // Draw aspect ratio
    char aspectText[32];
    static const char *aspectLabels[] = {
        "4:3",
        "16:9",
        "Auto"};

    snprintf(aspectText, sizeof(aspectText), "Aspect: %s", aspectLabels[popsConfig.aspectRatio]);
    guiDraw2dText(previewX, previewY + previewHeight + 30, 10,
                  GS_SETREG_RGBA(255, 255, 255, 255), aspectText);

    // Draw quality
    char qualityText[32];
    static const char *qualityLabels[] = {
        "Low",
        "Medium",
        "High",
        "Ultra"};

    snprintf(qualityText, sizeof(qualityText), "Quality: %s", qualityLabels[popsConfig.quality]);
    guiDraw2dText(previewX, previewY + previewHeight + 45, 10,
                  GS_SETREG_RGBA(255, 255, 255, 255), qualityText);

    // List enabled enhancements
    int enhY = previewY + previewHeight + 65;
    guiDraw2dText(previewX, enhY, 10, GS_SETREG_RGBA(255, 255, 0, 255), "Enhancements:");
    enhY += 15;

    if (popsConfig.bilinearFilter)
        enum {
            POPS_SETTING_ENABLE = 500,
            POPS_SETTING_RESOLUTION,
            POPS_SETTING_ASPECT_RATIO,
            POPS_SETTING_QUALITY,
            POPS_SETTING_SCANLINES_ENABLE,
            POPS_SETTING_SCANLINES_INTENSITY,
            POPS_SETTING_BILINEAR_FILTER,
            POPS_SETTING_ENHANCE_TEXTURES,
            POPS_SETTING_SMOOTH_POLYGONS,
            POPS_SETTING_APPLY,
            POPS_SETTING_DEFAULT
        };

    // Resolution menu option handlers
    static int popsToggleEnable(int id)
    {
        popsConfig.enabled = !popsConfig.enabled;

        // Enable/disable all resolution settings based on main toggle
        guiEnableResolutionMenu(popsConfig.enabled);

        return 0;
    }

    static int popsChangeResolution(int id)
    {
        popsConfig.resolution = (popsConfig.resolution + 1) % 5; // Cycle through 5 options
        return 0;
    }

    static int popsChangeAspectRatio(int id)
    {
        popsConfig.aspectRatio = (popsConfig.aspectRatio + 1) % 3; // Cycle through 3 options
        return 0;
    }

    static int popsChangeQuality(int id)
    {
        popsConfig.quality = (popsConfig.quality + 1) % 4; // Cycle through 4 quality options
        return 0;
    }

    static int popsToggleScanlines(int id)
    {
        popsConfig.scanlines = !popsConfig.scanlines;

        // Enable/disable scanline intensity option based on scanlines toggle
        guiEnableMenuItem(mainMenu, "POPSTARTER Settings", "Scanline Intensity", popsConfig.scanlines);

        return 0;
    }

    static int popsChangeScanlineIntensity(int id)
    {
        // 5 levels of intensity (0-4)
        popsConfig.scanlineIntensity = (popsConfig.scanlineIntensity + 1) % 5;
        return 0;
    }

    static int popsToggleBilinearFilter(int id)
    {
        popsConfig.bilinearFilter = !popsConfig.bilinearFilter;
        return 0;
    }

    static int popsToggleEnhanceTextures(int id)
    {
        popsConfig.enhanceTextures = !popsConfig.enhanceTextures;
        return 0;
    }

    static int popsToggleSmoothPolygons(int id)
    {
        popsConfig.smoothPolygons = !popsConfig.smoothPolygons;
        return 0;
    }

    static int popsApplySettings(int id)
    {
        // Apply settings to GSM
        int result = gsmSetResolution(popsConfig.resolution,
                                      popsConfig.aspectRatio,
                                      popsConfig.quality);

        // Apply enhanced settings
        gsmSetEnhancements(popsConfig.scanlines,
                           popsConfig.scanlineIntensity,
                           popsConfig.bilinearFilter,
                           popsConfig.enhanceTextures,
                           popsConfig.smoothPolygons);

        // Save configuration
        saveConfig(CONFIG_POPS, 1);

        // Show result feedback
        const char *resultMsg = (result == 0) ?
                                    "Resolution settings applied successfully." :
                                    "Error applying resolution settings!";

        // Display message dialog
        diaShowMessage("Apply Settings", resultMsg, 0);

        return 0;
    }

    static int popsResetDefaults(int id)
    {
        // Reset to default values
        popsConfig.resolution = POPS_RES_640_480P;
        popsConfig.aspectRatio = POPS_ASPECT_4_3;
        popsConfig.quality = POPS_QUALITY_HIGH;
        popsConfig.scanlines = false;
        popsConfig.scanlineIntensity = 2;
        popsConfig.bilinearFilter = true;
        popsConfig.enhanceTextures = true;
        popsConfig.smoothPolygons = true;

        // Update UI
        guiUpdateResolutionMenu();

        // Show confirmation
        diaShowMessage("Default Settings", "Resolution settings have been reset to defaults.", 0);

        return 0;
    }
