#ifndef _DS34USB_H_
#define _DS34USB_H_

#include "irx.h"


enum eDS34USBStatus {
    DS34USB_STATE_DISCONNECTED = 0x00,
    DS34USB_STATE_AUTHORIZED = 0x01,
    DS34USB_STATE_CONFIGURED = 0x02,
    DS34USB_STATE_CONNECTED = 0x04,
    DS34USB_STATE_RUNNING = 0x08,
};

int ds34usb_init(u8 pads, u8 options);
void ds34usb_reset();

#endif
