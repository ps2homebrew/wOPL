#include "types.h"
#include "loadcore.h"
#include "stdio.h"
#include "sifrpc.h"
#include "sysclib.h"
#include "usbd.h"
#include "usbd_macro.h"
#include "thbase.h"
#include "thsemap.h"
#include "ds34usb.h"
#include "sys_utils.h"
#include "padmacro.h"
#include "pademu.h"
#include "ds34common.h"

#define MODNAME "DS34USB"

#ifdef DEBUG
#define DPRINTF(format, args...) \
    printf(MODNAME ": " format, ##args)
#else
#define DPRINTF(args...)
#endif

#define REQ_USB_OUT (USB_DIR_OUT | USB_TYPE_CLASS | USB_RECIP_INTERFACE)
#define REQ_USB_IN  (USB_DIR_IN | USB_TYPE_CLASS | USB_RECIP_INTERFACE)

#define MAX_PADS 4

static u8 output_01_report[] =
    {
        0x00,
        0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
        0x02,
        0xff, 0x27, 0x10, 0x00, 0x32,
        0xff, 0x27, 0x10, 0x00, 0x32,
        0xff, 0x27, 0x10, 0x00, 0x32,
        0xff, 0x27, 0x10, 0x00, 0x32,
        0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00};

static u8 led_patterns[][2] =
    {
        {0x1C, 0x02},
        {0x1A, 0x04},
        {0x16, 0x08},
        {0x0E, 0x10},
};

static u8 power_level[] =
    {
        0x00, 0x00, 0x02, 0x06, 0x0E, 0x1E};

static u8 rgbled_patterns[][2][3] =
    {
        {{0x00, 0x00, 0x10}, {0x00, 0x00, 0x7F}}, // light blue/blue
        {{0x00, 0x10, 0x00}, {0x00, 0x7F, 0x00}}, // light green/green
        {{0x10, 0x10, 0x00}, {0x7F, 0x7F, 0x00}}, // light yellow/yellow
        {{0x00, 0x10, 0x10}, {0x00, 0x7F, 0x7F}}, // light cyan/cyan
};

static u8 usb_buf[MAX_BUFFER_SIZE + 32] __attribute((aligned(4))) = {0};

static int usb_probe(int devId);
static int usb_connect(int devId);
static int usb_disconnect(int devId);

static void usb_release(int pad);
static void usb_config_set(int result, int count, void *arg);

static UsbDriver usb_driver = {NULL, NULL, "ds34usb", usb_probe, usb_connect, usb_disconnect};

static void DS3USB_init(int pad);
static void readReport(u8 *data, int pad);
static int LEDRumble(u8 *led, u8 lrum, u8 rrum, int pad);
static void TransferWait(int sema);

static int ds34usb_get_model(struct pad_funcs *pf, int port);
static int ds34usb_get_data(struct pad_funcs *pf, u8 *dst, int size, int port);
static void ds34usb_set_rumble(struct pad_funcs *pf, u8 lrum, u8 rrum);
static void ds34usb_set_mode(struct pad_funcs *pf, int mode, int lock);

static ds34usb_device ds34pad[MAX_PADS];
static struct pad_funcs padf[MAX_PADS];

static int usb_probe(int devId)
{
    UsbDeviceDescriptor *device = NULL;

    DPRINTF("probe: devId=%i\n", devId);

    device = (UsbDeviceDescriptor *)sceUsbdScanStaticDescriptor(devId, NULL, USB_DT_DEVICE);
    if (device == NULL) {
        DPRINTF("Error - Couldn't get device descriptor\n");
        return 0;
    }

    if (device->idVendor == SONY_VID && (device->idProduct == GUITAR_HERO_PS3_PID || device->idProduct == ROCK_BAND_PS3_PID)) {
        return 1;
    }

    if (device->idVendor == DS34_VID && (device->idProduct == DS3_PID || device->idProduct == DS4_PID || device->idProduct == DS4_PID_SLIM || device->idProduct == DS5_PID))
        return 1;

    if (device->idVendor == LG_VID && device->idProduct == LGDFX_PID)
        return 1;

    return 0;
}

static int usb_connect(int devId)
{
    int pad, epCount;
    UsbDeviceDescriptor *device;
    UsbConfigDescriptor *config;
    UsbInterfaceDescriptor *interface;
    UsbEndpointDescriptor *endpoint;

    DPRINTF("connect: devId=%i\n", devId);

    for (pad = 0; pad < MAX_PADS; pad++) {
        if (ds34pad[pad].devId == -1 && ds34pad[pad].enabled)
            break;
    }

    if (pad >= MAX_PADS) {
        DPRINTF("Error - only %d device allowed !\n", MAX_PADS);
        return 1;
    }

    PollSema(ds34pad[pad].sema);

    ds34pad[pad].devId = devId;

    ds34pad[pad].status = DS34USB_STATE_AUTHORIZED;

    ds34pad[pad].controlEndp = UsbOpenEndpoint(devId, NULL);

    device = (UsbDeviceDescriptor *)sceUsbdScanStaticDescriptor(devId, NULL, USB_DT_DEVICE);
    config = (UsbConfigDescriptor *)sceUsbdScanStaticDescriptor(devId, device, USB_DT_CONFIG);
    interface = (UsbInterfaceDescriptor *)((char *)config + config->bLength);

    if (device->idProduct == DS3_PID) {
        ds34pad[pad].type = DS3;
        epCount = interface->bNumEndpoints - 1;
    } else if (device->idProduct == DS4_PID || device->idProduct == DS4_PID_SLIM) {
        ds34pad[pad].type = DS4;
        epCount = 20; // ds4 v2 returns interface->bNumEndpoints as 0
    } else if (device->idProduct == DS5_PID) {
        ds34pad[pad].type = DS5;
        epCount = 20; // ds5 v2 returns interface->bNumEndpoints as 0
    } else if (device->idProduct == LGDFX_PID) {
        ds34pad[pad].type = LGDFX;
        epCount = interface->bNumEndpoints - 1;
    }

    endpoint = (UsbEndpointDescriptor *)sceUsbdScanStaticDescriptor(devId, NULL, USB_DT_ENDPOINT);

    do {
        if (endpoint->bmAttributes == USB_ENDPOINT_XFER_INT) {
            if ((endpoint->bEndpointAddress & USB_ENDPOINT_DIR_MASK) == USB_DIR_IN && ds34pad[pad].interruptEndp < 0) {
                ds34pad[pad].interruptEndp = sceUsbdOpenPipe(devId, endpoint);
                DPRINTF("register Event endpoint id =%i addr=%02X packetSize=%i\n", ds34pad[pad].interruptEndp, endpoint->bEndpointAddress, (unsigned short int)endpoint->wMaxPacketSizeHB << 8 | endpoint->wMaxPacketSizeLB);
            }
            if ((endpoint->bEndpointAddress & USB_ENDPOINT_DIR_MASK) == USB_DIR_OUT && ds34pad[pad].outEndp < 0) {
                ds34pad[pad].outEndp = sceUsbdOpenPipe(devId, endpoint);
                DPRINTF("register Output endpoint id =%i addr=%02X packetSize=%i\n", ds34pad[pad].outEndp, endpoint->bEndpointAddress, (unsigned short int)endpoint->wMaxPacketSizeHB << 8 | endpoint->wMaxPacketSizeLB);
            }
        }

        endpoint = (UsbEndpointDescriptor *)((char *)endpoint + endpoint->bLength);

    } while (epCount--);

    if (ds34pad[pad].interruptEndp < 0 || ds34pad[pad].outEndp < 0) {
        usb_release(pad);
        return 1;
    }

    ds34pad[pad].status |= DS34USB_STATE_CONNECTED;

    sceUsbdSetConfiguration(ds34pad[pad].controlEndp, config->bConfigurationValue, usb_config_set, (void *)pad);
    SignalSema(ds34pad[pad].sema);

    return 0;
}

static int usb_disconnect(int devId)
{
    u8 pad;

    DPRINTF("disconnect: devId=%i\n", devId);

    for (pad = 0; pad < MAX_PADS; pad++) {
        if (ds34pad[pad].devId == devId)
            break;
    }

    if (pad < MAX_PADS) {
        usb_release(pad);
        pademu_disconnect(&padf[pad]);
    }

    return 0;
}

static void usb_release(int pad)
{
    PollSema(ds34pad[pad].sema);

    if (ds34pad[pad].interruptEndp >= 0)
        sceUsbdClosePipe(ds34pad[pad].interruptEndp);

    if (ds34pad[pad].outEndp >= 0)
        sceUsbdClosePipe(ds34pad[pad].outEndp);

    ds34pad[pad].controlEndp = -1;
    ds34pad[pad].interruptEndp = -1;
    ds34pad[pad].outEndp = -1;
    ds34pad[pad].devId = -1;
    ds34pad[pad].status = DS34USB_STATE_DISCONNECTED;

    SignalSema(ds34pad[pad].sema);
}

static int usb_resulCode;

static void usb_data_cb(int resultCode, int bytes, void *arg)
{
    int pad = (int)arg;

    DPRINTF("usb_data_cb: res %d, bytes %d, arg %p \n", resultCode, bytes, arg);

    usb_resulCode = resultCode;

    SignalSema(ds34pad[pad].sema);
}

static void usb_cmd_cb(int resultCode, int bytes, void *arg)
{
    int pad = (int)arg;

    DPRINTF("usb_cmd_cb: res %d, bytes %d, arg %p \n", resultCode, bytes, arg);

    SignalSema(ds34pad[pad].cmd_sema);
}

static void usb_config_set(int result, int count, void *arg)
{
    int pad = (int)arg;
    u8 led[4];

    PollSema(ds34pad[pad].sema);

    ds34pad[pad].status |= DS34USB_STATE_CONFIGURED;

    if (ds34pad[pad].type == DS3 || ds34pad[pad].type == DS4 || ds34pad[pad].type == DS5) {
        if (ds34pad[pad].type == DS3) {
            DS3USB_init(pad);
            DelayThread(10000);
            led[0] = led_patterns[pad][1];
            led[3] = 0;
        } else if (ds34pad[pad].type == DS4 || ds34pad[pad].type == DS5) {
            led[0] = rgbled_patterns[pad][1][0];
            led[1] = rgbled_patterns[pad][1][1];
            led[2] = rgbled_patterns[pad][1][2];
            led[3] = 0;
        }

        LEDRumble(led, 0, 0, pad);
    } else if (ds34pad[pad].type == LGDFX) {
        led[0] = 0x01;
        led[1] = 0x03;
        led[2] = 0x00;
        sceUsbdInterruptTransfer(ds34pad[pad].outEndp, led, 3, NULL, NULL);
        DelayThread(10000);
    }

    ds34pad[pad].status |= DS34USB_STATE_RUNNING;

    SignalSema(ds34pad[pad].sema);

    pademu_connect(&padf[pad]);
}

static void DS3USB_init(int pad)
{
    usb_buf[0] = 0x42;
    usb_buf[1] = 0x0c;
    usb_buf[2] = 0x00;
    usb_buf[3] = 0x00;

    sceUsbdControlTransfer(ds34pad[pad].controlEndp, REQ_USB_OUT, USB_REQ_SET_REPORT, (HID_USB_GET_REPORT_FEATURE << 8) | 0xF4, 0, 4, usb_buf, NULL, NULL);
}

#define MAX_DELAY 10

static void readReport(u8 *data, int pad_idx)
{
    ds34usb_device *pad = &ds34pad[pad_idx];

    if (pad->type == LGDFX) {
        struct xbox360report *report;

        report = (struct xbox360report *)data;

        if (report->ReportID == 0x00 && report->Length == 0x14) {

            /*
            // some ffb testing keys
            if (report->Back) {
                if (pad->lrum_margin < 60) { // default 20
                    pad->lrum_margin += 20;
                } else {
                    pad->lrum_margin = 20;
                }
            }
            if (report->XBOX) {
                if (pad->lrum_treshold < 180) { // default 140
                    pad->lrum_treshold += 40;
                } else {
                    pad->lrum_treshold = 100;
                }
            }
            */

            pad->data[0] = ~(report->Start << 3 | report->Up << 4 | report->Right << 5 | report->Down << 6 | report->Left << 7);
            pad->data[1] = ~((report->LeftTrigger != 0) | (report->RightTrigger != 0) << 1 | report->LB << 2 | report->RB << 3 | report->Y << 4 | report->B << 5 | report->A << 6 | report->X << 7);

            pad->data[2] = report->LeftStickXH + 128; // rx -nc wheel
            pad->data[3] = report->RightTrigger;      // ry -nc gas
            pad->data[4] = report->LeftTrigger;       // lx -nc brake
            pad->data[5] = report->LB * 255;          // ly -nc l
        }
    }

    if (data[0]) {

        if (pad->type == DS3) {
            struct ds3report *report;

            report = (struct ds3report *)&data[2];

            if (report->RightStickX == 0 && report->RightStickY == 0) // ledrumble cmd causes null report sometime
                return;

            pad->data[0] = ~report->ButtonStateL;
            pad->data[1] = ~report->ButtonStateH;

            translate_pad_ds3(report, &pad->ds2, 0);
            // padMacroPerform(&pad->ds2, report->PSButton);

            /*
            if (report->PSButton) {                                    // display battery level
                if (report->Select && (pad->btn_delay == MAX_DELAY)) { // PS + SELECT
                    if (pad->analog_btn < 2)                           // unlocked mode
                        pad->analog_btn = !pad->analog_btn;

                    pad->oldled[0] = led_patterns[pad_idx][(pad->analog_btn & 1)];
                    pad->btn_delay = 1;
                } else {
                    if (report->Power <= 0x05)
                        pad->oldled[0] = power_level[report->Power];

                    if (pad->btn_delay < MAX_DELAY)
                        pad->btn_delay++;
                }
            } else {
                pad->oldled[0] = led_patterns[pad_idx][(pad->analog_btn & 1)];

                if (pad->btn_delay > 0)
                    pad->btn_delay--;
            }

            if (report->Power == 0xEE) // charging
                pad->oldled[3] = 1;
            else
                pad->oldled[3] = 0;
            */
        } else if (pad->type == DS4) {
            struct ds4report *report;
            report = (struct ds4report *)data;
            translate_pad_ds4(report, &pad->ds2, 1);
            // padMacroPerform(&pad->ds2, report->PSButton);

            /*
            if (report->PSButton) {                                   // display battery level
                if (report->Share && (pad->btn_delay == MAX_DELAY)) { // PS + Share
                    if (pad->analog_btn < 2)                          // unlocked mode
                        pad->analog_btn = !pad->analog_btn;

                    pad->oldled[0] = rgbled_patterns[pad_idx][(pad->analog_btn & 1)][0];
                    pad->oldled[1] = rgbled_patterns[pad_idx][(pad->analog_btn & 1)][1];
                    pad->oldled[2] = rgbled_patterns[pad_idx][(pad->analog_btn & 1)][2];
                    pad->btn_delay = 1;
                } else {
                    pad->oldled[0] = report->Battery;
                    pad->oldled[1] = 0;
                    pad->oldled[2] = 0;

                    if (pad->btn_delay < MAX_DELAY)
                        pad->btn_delay++;
                }
            } else {
                pad->oldled[0] = rgbled_patterns[pad_idx][(pad->analog_btn & 1)][0];
                pad->oldled[1] = rgbled_patterns[pad_idx][(pad->analog_btn & 1)][1];
                pad->oldled[2] = rgbled_patterns[pad_idx][(pad->analog_btn & 1)][2];

                if (pad->btn_delay > 0)
                    pad->btn_delay--;
            }

            if (report->Power != 0xB && report->Usb_plugged) // charging
                pad->oldled[3] = 1;
            else
                pad->oldled[3] = 0;
            */
        } else if (pad->type == DS5) {
            struct ds5report *report;
            report = (struct ds5report *)data;
            translate_pad_ds5(report, &pad->ds2, 1);
            // padMacroPerform(&pad->ds2, report->PSButton);

            /*
            if (report->PSButton) {                                    // display battery level
                //if (report->Create && (pad->btn_delay == MAX_DELAY)) { // PS + Create
                    //if (pad->analog_btn < 2)                           // unlocked mode
                        //pad->analog_btn = !pad->analog_btn;
               if (pad->analog_btn < 2) {
                    pad->analog_btn = !pad->analog_btn;

                    pad->oldled[0] = rgbled_patterns[pad_idx][(pad->analog_btn & 1)][0];
                    pad->oldled[1] = rgbled_patterns[pad_idx][(pad->analog_btn & 1)][1];
                    pad->oldled[2] = rgbled_patterns[pad_idx][(pad->analog_btn & 1)][2];
                    //pad->btn_delay = 1;
               } else {

                    pad->oldled[0] = (report->Battery * 255) / 15;
                    pad->oldled[1] = 0;
                    pad->oldled[2] = 0;

                    //if (pad->btn_delay < MAX_DELAY)
                        //pad->btn_delay++;
               }
            } else {
                pad->oldled[0] = rgbled_patterns[pad_idx][(pad->analog_btn & 1)][0];
                pad->oldled[1] = rgbled_patterns[pad_idx][(pad->analog_btn & 1)][1];
                pad->oldled[2] = rgbled_patterns[pad_idx][(pad->analog_btn & 1)][2];

                //if (pad->btn_delay > 0)
                    //pad->btn_delay--;
            }
        */
        }
        // if (pad->btn_delay > 0) {
        // pad->update_rum = 1;
        //}
    }
}

static u8 processVibrationValue(u8 rum, u8 *rum_wait, u8 rum_wait_max)
{
    u8 val = 0;

    if (rum > 0) {
        if (*rum_wait > 0) {
            (*rum_wait)++;
            if (*rum_wait == rum_wait_max)
                *rum_wait = 0;
        } else {
            (*rum_wait)++;
            val = rum;
        }
    } else {
        *rum_wait = 0;
    }

    return val;
}

static int LEDRumble(u8 *led, u8 lrum, u8 rrum, int pad)
{
    int ret = 0;
    u8 new_lrum = 0;

    PollSema(ds34pad[pad].cmd_sema);

    memset(usb_buf, 0, sizeof(usb_buf));

    if (ds34pad[pad].type == DS3 || ds34pad[pad].type == DS4 || ds34pad[pad].type == DS5) {

        if (ds34pad[pad].type == DS3) {
            memcpy(usb_buf, output_01_report, sizeof(output_01_report));

            usb_buf[1] = 0xFE; // rt
            usb_buf[2] = rrum; // rp
            usb_buf[3] = 0xFE; // lt
            usb_buf[4] = lrum; // lp

            usb_buf[9] = led[0] & 0x7F; // LED Conf

            if (led[3]) // means charging, so blink
            {
                usb_buf[13] = 0x32;
                usb_buf[18] = 0x32;
                usb_buf[23] = 0x32;
                usb_buf[28] = 0x32;
            }

            ret = sceUsbdControlTransfer(ds34pad[pad].controlEndp, REQ_USB_OUT, USB_REQ_SET_REPORT, (HID_USB_SET_REPORT_OUTPUT << 8) | 0x01, 0, sizeof(output_01_report), usb_buf, usb_cmd_cb, (void *)pad);
        } else if (ds34pad[pad].type == DS4) {
            usb_buf[0] = 0x05;
            usb_buf[1] = 0xFF;

            usb_buf[4] = rrum * 255; // ds4 has full control
            usb_buf[5] = lrum;

            usb_buf[6] = led[0]; // r
            usb_buf[7] = led[1]; // g
            usb_buf[8] = led[2]; // b

            if (led[3]) // means charging, so blink
            {
                usb_buf[9] = 0x80;  // Time to flash bright (255 = 2.5 seconds)
                usb_buf[10] = 0x80; // Time to flash dark (255 = 2.5 seconds)
            }

            ret = sceUsbdInterruptTransfer(ds34pad[pad].outEndp, usb_buf, 32, usb_cmd_cb, (void *)pad);
        } else if (ds34pad[pad].type == DS5) {
            usb_buf[0] = 0x02; // ReportID
            usb_buf[1] = 0x03; // EnableRumbleEmulation & RumbleUseRumbleNotHaptics
            usb_buf[2] = 0x17;

            usb_buf[3] = rrum * 255;
            ;                  // light weight !!!ds5 has full control!!!
            usb_buf[4] = lrum; // heavy weight

            usb_buf[39] = 0x07; // AllowLightBrightnessChange & AllowColorLightFadeAnimation & EnableImprovedRumbleEmulation
            usb_buf[42] = 0x80; // LightFadeAnimation
            usb_buf[43] = 0xFF; // LightBrightness
            usb_buf[44] = 0x04; // PlayerLight - - X - -

            usb_buf[45] = led[0]; // r
            usb_buf[46] = led[1]; // g
            usb_buf[47] = led[2]; // b

            ret = sceUsbdInterruptTransfer(ds34pad[pad].outEndp, usb_buf, 48, usb_cmd_cb, (void *)pad);
        }

        ds34pad[pad].oldled[0] = led[0];
        ds34pad[pad].oldled[1] = led[1];
        ds34pad[pad].oldled[2] = led[2];
        ds34pad[pad].oldled[3] = led[3];

    } else if (ds34pad[pad].type == LGDFX) {

        new_lrum = processVibrationValue(lrum, &ds34pad[pad].lrum_wait, ds34pad[pad].lrum_wait_max);

        if (new_lrum < ds34pad[pad].lrum_treshold) { // treshold
            new_lrum = 0;
        } else {
            new_lrum = lrum - (ds34pad[pad].lrum_treshold - ds34pad[pad].lrum_margin);
        }

        /*
        if (new_lrum > (ds34pad[pad].lrum_clamp * 2)) // clamp
             new_lrum = (ds34pad[pad].lrum_clamp * 2);
        */

        usb_buf[0] = 0x00;
        usb_buf[1] = 0x08;
        usb_buf[2] = 0x00;
        usb_buf[3] = new_lrum;                                                                               // big weight
        usb_buf[4] = processVibrationValue(rrum * 255, &ds34pad[pad].rrum_wait, ds34pad[pad].rrum_wait_max); // small weight
        usb_buf[5] = 0x00;
        usb_buf[6] = 0x00;
        usb_buf[7] = 0x00;

        ret = sceUsbdInterruptTransfer(ds34pad[pad].outEndp, usb_buf, 8, usb_cmd_cb, (void *)pad);

        /*
        if (ret == USB_RC_OK) {
            TransferWait(ds34pad[pad].cmd_sema); // if You ever impl xbox 360 pad then need this wait otherwise no rumble, and good to impl check if old led val diff from new led val, then send otherwise usb cmd so no spaming with led cmds (use this ds34pad[pad] struct like this ds34pad[pad].last_led or smth (all the same to xbox one pad))
            usb_buf[0] = 0x01;
            usb_buf[1] = 0x03;
            usb_buf[2] = 0x00; // led[0];
            ret = UsbControlTransfer(ds34pad[pad].controlEndp, REQ_USB_OUT, USB_REQ_SET_REPORT, (HID_USB_SET_REPORT_OUTPUT << 8) | 0x01, 0, 3, usb_buf, usb_cmd_cb, (void *)pad);
        }

        ds34pad[pad].oldled[0] = led[0];
        ds34pad[pad].oldled[1] = led[1];
        */
    }

    return ret;
}

/*

// led patterns
0x00	 All off
0x01	 All blinking
0x02	 1 flashes, then on
0x03	 2 flashes, then on
0x04	 3 flashes, then on
0x05	 4 flashes, then on
0x06	 1 on
0x07	 2 on
0x08	 3 on
0x09	 4 on
0x0A	 Rotating (e.g. 1-2-4-3)
0x0B	 Blinking*
0x0C	 Slow blinking*
0x0D	 Alternating (e.g. 1+4-2+3), then back to previous*


*/

static unsigned int timeout(void *arg)
{
    int sema = (int)arg;
    iSignalSema(sema);
    return 0;
}

static void TransferWait(int sema)
{
    iop_sys_clock_t cmd_timeout;

    cmd_timeout.lo = 200000;
    cmd_timeout.hi = 0;

    if (SetAlarm(&cmd_timeout, timeout, (void *)sema) == 0) {
        WaitSema(sema);
        CancelAlarm(timeout, NULL);
    }
}

static void ds34usb_set_rumble(struct pad_funcs *pf, u8 lrum, u8 rrum)
{
    ds34usb_device *pad = pf->priv;
    WaitSema(pad->sema);

    if ((pad->lrum != lrum) || (pad->rrum != rrum)) {
        pad->lrum = lrum;
        pad->rrum = rrum;
        pad->update_rum = 1;
    }

    SignalSema(pad->sema);
}

static int ds34usb_get_data(struct pad_funcs *pf, u8 *dst, int size, int port)
{
    ds34usb_device *pad = pf->priv;
    int ret = 0;

    WaitSema(pad->sema);

    PollSema(pad->sema);

    ret = sceUsbdInterruptTransfer(pad->interruptEndp, usb_buf, MAX_BUFFER_SIZE, usb_data_cb, (void *)port);

    if (ret == USB_RC_OK) {
        TransferWait(pad->sema);
        if (!usb_resulCode)
            readReport(usb_buf, port);

        usb_resulCode = 1;
    } else {
        DPRINTF("ds34usb_get_data usb transfer error %d\n", ret);
    }

    memcpy(dst, pad->data, size);
    ret = pad->analog_btn & 1;

    if (pad->update_rum) {
        ret = LEDRumble(pad->oldled, pad->lrum, pad->rrum, port);
        if (ret == USB_RC_OK)
            TransferWait(pad->cmd_sema);
        else
            DPRINTF("LEDRumble usb transfer error %d\n", ret);

        pad->update_rum = 0;
    }

    SignalSema(pad->sema);

    return ret;
}

static void ds34usb_set_mode(struct pad_funcs *pf, int mode, int lock)
{
    ds34usb_device *pad = pf->priv;
    WaitSema(pad->sema);
    if (lock == 3)
        pad->analog_btn = 3;
    else
        pad->analog_btn = mode;
    SignalSema(pad->sema);
}

void ds34usb_reset()
{
    int pad;

    for (pad = 0; pad < MAX_PADS; pad++)
        usb_release(pad);
}

static int ds34usb_get_model(struct pad_funcs *pf, int port)
{
    (void *)port;
    ds34usb_device *pad = pf->priv;
    int ret;

    WaitSema(pad->sema);

    if (pad->type == GUITAR_GH || pad->type == GUITAR_RB) {
        ret = MODEL_GUITAR;
    } else {
        ret = MODEL_PS2;
    }

    SignalSema(pad->sema);

    return ret;
}

int ds34usb_init(u8 pads, u8 options)
{
    int pad;

    for (pad = 0; pad < MAX_PADS; pad++) {
        ds34pad[pad].status = 0;
        ds34pad[pad].devId = -1;
        ds34pad[pad].oldled[0] = 0;
        ds34pad[pad].oldled[1] = 0;
        ds34pad[pad].oldled[2] = 0;
        ds34pad[pad].oldled[3] = 0;
        ds34pad[pad].lrum = 0;
        ds34pad[pad].rrum = 0;
        ds34pad[pad].update_rum = 1;
        ds34pad[pad].lrum_wait = 0;
        ds34pad[pad].rrum_wait = 0;
        ds34pad[pad].lrum_wait_max = 5;
        ds34pad[pad].rrum_wait_max = 3;
        ds34pad[pad].lrum_treshold = 140;
        ds34pad[pad].lrum_margin = 20;
        ds34pad[pad].sema = -1;
        ds34pad[pad].cmd_sema = -1;
        ds34pad[pad].controlEndp = -1;
        ds34pad[pad].interruptEndp = -1;
        ds34pad[pad].enabled = (pads >> pad) & 1;
        ds34pad[pad].type = 0;

        ds34pad[pad].data[0] = 0xFF;
        ds34pad[pad].data[1] = 0xFF;
        ds34pad[pad].analog_btn = 0;

        memset(&ds34pad[pad].data[2], 0x7F, 4);
        memset(&ds34pad[pad].data[6], 0x00, 12);

        ds34pad[pad].sema = CreateMutex(IOP_MUTEX_UNLOCKED);
        ds34pad[pad].cmd_sema = CreateMutex(IOP_MUTEX_UNLOCKED);

        if (ds34pad[pad].sema < 0 || ds34pad[pad].cmd_sema < 0) {
            DPRINTF("Failed to allocate I/O semaphore.\n");
            return 0;
        }
        padf[pad].priv = &ds34pad[pad];
        padf[pad].get_model = ds34usb_get_model;
        padf[pad].get_data = ds34usb_get_data;
        padf[pad].set_rumble = ds34usb_set_rumble;
        padf[pad].set_mode = ds34usb_set_mode;
    }

    if (sceUsbdRegisterLdd(&usb_driver) != USB_RC_OK) {
        DPRINTF("Error registering USB devices\n");
        return 0;
    }

    return 1;
}
