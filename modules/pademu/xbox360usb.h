#ifndef _XBOX360USB_H_
#define _XBOX360USB_H_

#include "irx.h"

#define XBOX_VID     0x045E // Microsoft Corporation
#define MADCATZ_VID  0x1BAD // For unofficial Mad Catz controllers
#define JOYTECH_VID  0x162E // For unofficial Joytech controllers
#define GAMESTOP_VID 0x0E6F // Gamestop controller

#define XBOX_WIRED_PID                         0x028E // Microsoft 360 Wired controller
#define XBOX_WIRELESS_PID                      0x028F // Wireless controller only support charging
#define XBOX_WIRELESS_RECEIVER_PID             0x0719 // Microsoft Wireless Gaming Receiver
#define XBOX_WIRELESS_RECEIVER_THIRD_PARTY_PID 0x0291 // Third party Wireless Gaming Receiver
#define MADCATZ_WIRED_PID                      0xF016 // Mad Catz wired controller
#define JOYTECH_WIRED_PID                      0xBEEF // For Joytech wired controller
#define GAMESTOP_WIRED_PID                     0x0401 // Gamestop wired controller
#define AFTERGLOW_WIRED_PID                    0x0213 // Afterglow wired controller - it uses the same VID as a Gamestop controller

#define MAX_BUFFER_SIZE 64 // Size of general purpose data buffer

typedef struct _usb_xbox360
{
    int devId;
    int sema;
    int cmd_sema;
    int controlEndp;
    int interruptEndp;
    int outEndp;
    uint8_t enabled;
    uint8_t status;
    uint8_t type;      // 0 - ds3, 1 - ds4, 2 - guitar hero guitar, 3 - rock band guitar
    uint8_t oldled[4]; // rgb for ds4 and blink
    uint8_t lrum;
    uint8_t rrum;
    uint8_t update_rum;
    union
    {
        struct ds2report ds2;
        uint8_t data[18];
    };
    uint8_t analog_btn;
    uint8_t btn_delay;
} xbox360usb_device;

typedef struct
{
    u8 ReportID;
    u8 Length; // 0x14
    union
    {
        u8 ButtonStateL; // Main buttons Low
        struct
        {
            u8 Up    : 1;
            u8 Down  : 1;
            u8 Left  : 1;
            u8 Right : 1;
            u8 Start : 1;
            u8 Back  : 1;
            u8 LS    : 1;
            u8 RS    : 1;
        };
    };
    union
    {
        u8 ButtonStateH; // Main buttons High
        struct
        {
            u8 LB     : 1;
            u8 RB     : 1;
            u8 XBOX   : 1;
            u8 Dummy1 : 1;
            u8 A      : 1;
            u8 B      : 1;
            u8 X      : 1;
            u8 Y      : 1;
        };
    };
    u8 LeftTrigger;
    u8 RightTrigger;
    union
    {
        u16 LeftStickX;
        struct
        {
            u8 LeftStickXL;
            u8 LeftStickXH;
        };
    };
    union
    {
        u16 LeftStickY;
        struct
        {
            u8 LeftStickYL;
            u8 LeftStickYH;
        };
    };
    union
    {
        u16 RightStickX;
        struct
        {
            u8 RightStickXL;
            u8 RightStickXH;
        };
    };
    union
    {
        u16 RightStickY;
        struct
        {
            u8 RightStickYL;
            u8 RightStickYH;
        };
    };
} __attribute__((packed)) xbox360report_t;

enum eXBOX360USBStatus {
    XBOX360USB_STATE_DISCONNECTED = 0x00,
    XBOX360USB_STATE_AUTHORIZED = 0x01,
    XBOX360USB_STATE_CONFIGURED = 0x02,
    XBOX360USB_STATE_CONNECTED = 0x04,
    XBOX360USB_STATE_RUNNING = 0x08,
};

int xbox360usb_init(u8 pads, u8 options);
void xbox360usb_reset();

#endif
