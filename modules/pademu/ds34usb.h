#ifndef _DS34USB_H_
#define _DS34USB_H_

#include "irx.h"

#include <ds34common.h>


#define GUITAR_GH 2
#define GUITAR_RB 3

#define MODEL_GUITAR 1
#define MODEL_PS2    3

typedef struct _usb_ds34
{
    int devId;
    int sema;
    int cmd_sema;
    int controlEndp;
    int interruptEndp;
    int outEndp;
    u8 enabled;
    u8 status;
    u8 type;      // 0 - ds3, 1 - ds4, 2 - guitar hero guitar, 3 - rock band guitar
    u8 oldled[4]; // rgb for ds4 and blink
    u8 lrum;
    u8 rrum;
    u8 update_rum;
    union
    {
        struct ds2report ds2;
        u8 data[18];
    };
    u8 analog_btn;
    u8 btn_delay;
} ds34usb_device;

enum eDS34USBStatus {
    DS34USB_STATE_DISCONNECTED = 0x00,
    DS34USB_STATE_AUTHORIZED = 0x01,
    DS34USB_STATE_CONFIGURED = 0x02,
    DS34USB_STATE_CONNECTED = 0x04,
    DS34USB_STATE_RUNNING = 0x08,
};

int ds34usb_init(u8 pads, u8 options);
int ds34usb_get_status(int port);
int ds34usb_get_model(int port);
void ds34usb_reset();
int ds34usb_get_data(u8 *dst, int size, int port);
void ds34usb_set_rumble(u8 lrum, u8 rrum, int port);
void ds34usb_set_mode(int mode, int lock, int port);

#endif
