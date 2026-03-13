/* based on https://github.com/IonAgorria/Arduino-PSRemote */
/* and https://github.com/felis/USB_Host_Shield_2.0 */

#include "types.h"
#include "loadcore.h"
#include "stdio.h"
#include "sifrpc.h"
#include "sysclib.h"
#include "usbd.h"
#include "usbd_macro.h"
#include "thbase.h"
#include "thsemap.h"
#include "sys_utils.h"
#include "pademu.h"
#include <stdint.h>
#include "ds34common.h"
#include "xbox360usb.h"
#include "padmacro.h"

#define MODNAME "XBOX360USB"

#ifdef DEBUG
#define DPRINTF(format, args...) \
    printf(MODNAME ": " format, ##args)
#else
#define DPRINTF(args...)
#endif

#define REQ_USB_OUT (USB_DIR_OUT | USB_TYPE_CLASS | USB_RECIP_INTERFACE)
#define REQ_USB_IN  (USB_DIR_IN | USB_TYPE_CLASS | USB_RECIP_INTERFACE)

#define MAX_PADS 4

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

static UsbDriver usb_driver = {NULL, NULL, "xbox360usb", usb_probe, usb_connect, usb_disconnect};

static void readReport(u8 *data, int pad);
static int LEDRumble(u8 *led, u8 lrum, u8 rrum, int pad);

static int xbox360_get_model(struct pad_funcs *pf, int port);
static int xbox360_get_data(struct pad_funcs *pf, u8 *dst, int size, int port);
static void xbox360_set_rumble(struct pad_funcs *pf, u8 lrum, u8 rrum);
static void xbox360_set_mode(struct pad_funcs *pf, int mode, int lock);

static xbox360usb_device xbox360pad[MAX_PADS];
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

    if ((device->idVendor == XBOX_VID || device->idVendor == MADCATZ_VID || device->idVendor == JOYTECH_VID || device->idVendor == GAMESTOP_VID) &&
        (device->idProduct == XBOX_WIRED_PID || device->idProduct == MADCATZ_WIRED_PID || device->idProduct == GAMESTOP_WIRED_PID ||
         device->idProduct == AFTERGLOW_WIRED_PID || device->idProduct == JOYTECH_WIRED_PID))
        return 1;

    return 0;
}

static int usb_connect(int devId)
{
    int pad, epCount;
    UsbDeviceDescriptor *device;
    UsbConfigDescriptor *config;
    UsbEndpointDescriptor *endpoint;

    DPRINTF("connect: devId=%i\n", devId);

    for (pad = 0; pad < MAX_PADS; pad++) {
        if (xbox360pad[pad].devId == -1 && xbox360pad[pad].enabled)
            break;
    }

    if (pad >= MAX_PADS) {
        DPRINTF("Error - only %d device allowed !\n", MAX_PADS);
        return 1;
    }

    PollSema(xbox360pad[pad].sema);

    xbox360pad[pad].devId = devId;

    xbox360pad[pad].status = XBOX360USB_STATE_AUTHORIZED;

    xbox360pad[pad].controlEndp = sceUsbdOpenPipe(devId, NULL);

    device = (UsbDeviceDescriptor *)sceUsbdScanStaticDescriptor(devId, NULL, USB_DT_DEVICE);
    config = (UsbConfigDescriptor *)sceUsbdScanStaticDescriptor(devId, device, USB_DT_CONFIG);

    xbox360pad[pad].type = DS4;
    epCount = 20; // ds4 v2 returns interface->bNumEndpoints as 0

    endpoint = (UsbEndpointDescriptor *)sceUsbdScanStaticDescriptor(devId, NULL, USB_DT_ENDPOINT);

    do {
        if (endpoint->bmAttributes == USB_ENDPOINT_XFER_INT) {
            if ((endpoint->bEndpointAddress & USB_ENDPOINT_DIR_MASK) == USB_DIR_IN && xbox360pad[pad].interruptEndp < 0) {
                xbox360pad[pad].interruptEndp = sceUsbdOpenPipe(devId, endpoint);
                DPRINTF("register Event endpoint id =%i addr=%02X packetSize=%i\n", xbox360pad[pad].interruptEndp, endpoint->bEndpointAddress, (unsigned short int)endpoint->wMaxPacketSizeHB << 8 | endpoint->wMaxPacketSizeLB);
            }
            if ((endpoint->bEndpointAddress & USB_ENDPOINT_DIR_MASK) == USB_DIR_OUT && xbox360pad[pad].outEndp < 0) {
                xbox360pad[pad].outEndp = sceUsbdOpenPipe(devId, endpoint);
                DPRINTF("register Output endpoint id =%i addr=%02X packetSize=%i\n", xbox360pad[pad].outEndp, endpoint->bEndpointAddress, (unsigned short int)endpoint->wMaxPacketSizeHB << 8 | endpoint->wMaxPacketSizeLB);
            }
        }

        endpoint = (UsbEndpointDescriptor *)((char *)endpoint + endpoint->bLength);

    } while (epCount--);

    if (xbox360pad[pad].interruptEndp < 0 || xbox360pad[pad].outEndp < 0) {
        usb_release(pad);
        return 1;
    }

    xbox360pad[pad].status |= XBOX360USB_STATE_CONNECTED;

    sceUsbdSetConfiguration(xbox360pad[pad].controlEndp, config->bConfigurationValue, usb_config_set, (void *)pad);
    SignalSema(xbox360pad[pad].sema);

    return 0;
}

static int usb_disconnect(int devId)
{
    u8 pad;

    DPRINTF("disconnect: devId=%i\n", devId);

    for (pad = 0; pad < MAX_PADS; pad++) {
        if (xbox360pad[pad].devId == devId)
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
    PollSema(xbox360pad[pad].sema);

    if (xbox360pad[pad].interruptEndp >= 0)
        sceUsbdClosePipe(xbox360pad[pad].interruptEndp);

    if (xbox360pad[pad].outEndp >= 0)
        sceUsbdClosePipe(xbox360pad[pad].outEndp);

    xbox360pad[pad].controlEndp = -1;
    xbox360pad[pad].interruptEndp = -1;
    xbox360pad[pad].outEndp = -1;
    xbox360pad[pad].devId = -1;
    xbox360pad[pad].status = XBOX360USB_STATE_DISCONNECTED;

    SignalSema(xbox360pad[pad].sema);
}

static int usb_resulCode;

static void usb_data_cb(int resultCode, int bytes, void *arg)
{
    int pad = (int)arg;

    DPRINTF("usb_data_cb: res %d, bytes %d, arg %p \n", resultCode, bytes, arg);

    usb_resulCode = resultCode;

    SignalSema(xbox360pad[pad].sema);
}

static void usb_cmd_cb(int resultCode, int bytes, void *arg)
{
    int pad = (int)arg;

    DPRINTF("usb_cmd_cb: res %d, bytes %d, arg %p \n", resultCode, bytes, arg);

    SignalSema(xbox360pad[pad].cmd_sema);
}

static void usb_config_set(int result, int count, void *arg)
{
    int pad = (int)arg;
    u8 led[4];

    PollSema(xbox360pad[pad].sema);

    xbox360pad[pad].status |= XBOX360USB_STATE_CONFIGURED;

    led[0] = rgbled_patterns[pad][1][0];
    led[1] = rgbled_patterns[pad][1][1];
    led[2] = rgbled_patterns[pad][1][2];
    led[3] = 0;

    LEDRumble(led, 0, 0, pad);

    xbox360pad[pad].status |= XBOX360USB_STATE_RUNNING;

    SignalSema(xbox360pad[pad].sema);

    pademu_connect(&padf[pad]);
}

#define MAX_DELAY 10

static void readReport(u8 *data, int pad_idx)
{
#ifdef WIP /* Read xbox 360 guitar */
    xbox360usb_device *pad = &ds34pad[pad_idx];
    if (pad->type == GUITAR_GH || pad->type == GUITAR_RB) {
        struct ds3guitarreport *report;

        report = (struct ds3guitarreport *)data;

        translate_pad_guitar(report, &pad->ds2, pad->type == GUITAR_GH);
        padMacroPerform(&pad->ds2, report->PSButton);
    }
    if (data[0]) {
        struct ds4report *report;
        report = (struct ds4report *)data;
        translate_pad_ds4(report, &pad->ds2, 1);
        padMacroPerform(&pad->ds2, report->PSButton);

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

        if (pad->btn_delay > 0) {
            pad->update_rum = 1;
        }
    }
#else
    xbox360report_t *report = (xbox360report_t *)data;
    if (report->ReportID == 0x00 && report->Length == 0x14) {
        xbox360pad[pad_idx].data[0] = ~(report->Back | report->LS << 1 | report->RS << 2 | report->Start << 3 | report->Up << 4 | report->Right << 5 | report->Down << 6 | report->Left << 7);
        xbox360pad[pad_idx].data[1] = ~((report->LeftTrigger != 0) | (report->RightTrigger != 0) << 1 | report->LB << 2 | report->RB << 3 | report->Y << 4 | report->B << 5 | report->A << 6 | report->X << 7);

        xbox360pad[pad_idx].data[2] = report->RightStickXH + 128;    // rx
        xbox360pad[pad_idx].data[3] = ~(report->RightStickYH + 128); // ry
        xbox360pad[pad_idx].data[4] = report->LeftStickXH + 128;     // lx
        xbox360pad[pad_idx].data[5] = ~(report->LeftStickYH + 128);  // ly

        xbox360pad[pad_idx].data[6] = report->Right * 255; // right
        xbox360pad[pad_idx].data[7] = report->Left * 255;  // left
        xbox360pad[pad_idx].data[8] = report->Up * 255;    // up
        xbox360pad[pad_idx].data[9] = report->Down * 255;  // down

        xbox360pad[pad_idx].data[10] = report->Y * 255; // triangle
        xbox360pad[pad_idx].data[11] = report->B * 255; // circle
        xbox360pad[pad_idx].data[12] = report->A * 255; // cross
        xbox360pad[pad_idx].data[13] = report->X * 255; // square

        xbox360pad[pad_idx].data[14] = report->LB * 255;     // L1
        xbox360pad[pad_idx].data[15] = report->RB * 255;     // R1
        xbox360pad[pad_idx].data[16] = report->LeftTrigger;  // L2
        xbox360pad[pad_idx].data[17] = report->RightTrigger; // R2

        if (report->XBOX) {                                             // display battery level
            if (report->Back && (xbox360pad->btn_delay == MAX_DELAY)) { // XBOX + BACK
                if (xbox360pad[pad_idx].analog_btn < 2)                 // unlocked mode
                    xbox360pad[pad_idx].analog_btn = !xbox360pad[pad_idx].analog_btn;

                // xbox360dev[pad].oldled[0] = led_patterns[pad][(xbox360dev[pad].analog_btn & 1)];
                xbox360pad[pad_idx].btn_delay = 1;
            } else {
                // if (report->Power != 0xEE)
                //     xbox360dev[pad].oldled[0] = power_level[report->Power];

                if (xbox360pad[pad_idx].btn_delay < MAX_DELAY)
                    xbox360pad[pad_idx].btn_delay++;
            }
        } else {
            // xbox360dev[pad].oldled[0] = led_patterns[pad][(xbox360dev[pad].analog_btn & 1)];

            if (xbox360pad[pad_idx].btn_delay > 0)
                xbox360pad[pad_idx].btn_delay--;
        }
    }
#endif
}

static int LEDRumble(u8 *led, u8 lrum, u8 rrum, int pad)
{
    int ret = 0;

    PollSema(xbox360pad[pad].cmd_sema);

    memset(usb_buf, 0, sizeof(usb_buf));

    usb_buf[0] = 0x05;
    usb_buf[1] = 0xFF;
    usb_buf[3] = lrum; // big weight
    usb_buf[4] = rrum; // small weight
    usb_buf[5] = lrum;

    usb_buf[6] = led[0]; // r
    usb_buf[7] = led[1]; // g
    usb_buf[8] = led[2]; // b

    if (led[3]) // means charging, so blink
    {
        usb_buf[9] = 0x80;  // Time to flash bright (255 = 2.5 seconds)
        usb_buf[10] = 0x80; // Time to flash dark (255 = 2.5 seconds)
    }

    ret = sceUsbdInterruptTransfer(xbox360pad[pad].outEndp, usb_buf, 32, usb_cmd_cb, (void *)pad);

    if (ret == USB_RC_OK) {
        usb_buf[0] = 0x01;
        usb_buf[1] = 0x03;
        usb_buf[2] = led[0];
        ret = UsbControlTransfer(xbox360pad[pad].controlEndp, REQ_USB_OUT, USB_REQ_SET_REPORT, (HID_USB_SET_REPORT_OUTPUT << 8) | 0x01, 0, 3, usb_buf, usb_cmd_cb, (void *)pad);
    }

    xbox360pad[pad].oldled[0] = led[0];
    xbox360pad[pad].oldled[1] = led[1];

    return ret;
}

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

static void xbox360_set_rumble(struct pad_funcs *pf, u8 lrum, u8 rrum)
{
    xbox360usb_device *pad = pf->priv;
    WaitSema(pad->sema);

    if ((pad->lrum != lrum) || (pad->rrum != rrum)) {
        pad->lrum = lrum;
        pad->rrum = rrum;
        pad->update_rum = 1;
    }

    SignalSema(pad->sema);
}

static int xbox360_get_data(struct pad_funcs *pf, u8 *dst, int size, int port)
{
    xbox360usb_device *pad = pf->priv;
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
        DPRINTF("ds4usb_get_data usb transfer error %d\n", ret);
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

static void xbox360_set_mode(struct pad_funcs *pf, int mode, int lock)
{
    xbox360usb_device *pad = pf->priv;
    WaitSema(pad->sema);
    if (lock == 3)
        pad->analog_btn = 3;
    else
        pad->analog_btn = mode;
    SignalSema(pad->sema);
}

void xbox360usb_reset()
{
    int pad;

    for (pad = 0; pad < MAX_PADS; pad++)
        usb_release(pad);
}

static int xbox360_get_model(struct pad_funcs *pf, int port)
{
#ifdef WIP /* TODO: Add xbox 360 guitars. */
    xbox360usb_device *pad = pf->priv;
    int ret;

    WaitSema(pad->sema);
    if (pad->type == GUITAR_GH || pad->type == GUITAR_RB) {
        ret = MODEL_GUITAR;
    } else {
        ret = MODEL_PS2;
    }
    SignalSema(pad->sema);

    return ret;
#else
    (void)pf;
    return 0;
#endif
}

int xbox360usb_init(u8 pads, u8 options)
{
    int pad;

    for (pad = 0; pad < MAX_PADS; pad++) {
        xbox360pad[pad].status = 0;
        xbox360pad[pad].devId = -1;
        xbox360pad[pad].oldled[0] = 0;
        xbox360pad[pad].oldled[1] = 0;
        xbox360pad[pad].oldled[2] = 0;
        xbox360pad[pad].oldled[3] = 0;
        xbox360pad[pad].lrum = 0;
        xbox360pad[pad].rrum = 0;
        xbox360pad[pad].update_rum = 1;
        xbox360pad[pad].sema = -1;
        xbox360pad[pad].cmd_sema = -1;
        xbox360pad[pad].controlEndp = -1;
        xbox360pad[pad].interruptEndp = -1;
        xbox360pad[pad].enabled = (pads >> pad) & 1;
        xbox360pad[pad].type = 0;

        xbox360pad[pad].data[0] = 0xFF;
        xbox360pad[pad].data[1] = 0xFF;
        xbox360pad[pad].analog_btn = 0;

        memset(&xbox360pad[pad].data[2], 0x7F, 4);
        memset(&xbox360pad[pad].data[6], 0x00, 12);

        xbox360pad[pad].sema = CreateMutex(IOP_MUTEX_UNLOCKED);
        xbox360pad[pad].cmd_sema = CreateMutex(IOP_MUTEX_UNLOCKED);

        if (xbox360pad[pad].sema < 0 || xbox360pad[pad].cmd_sema < 0) {
            DPRINTF("Failed to allocate I/O semaphore.\n");
            return 0;
        }
        padf[pad].priv = &xbox360pad[pad];
        padf[pad].get_model = xbox360_get_model;
        padf[pad].get_data = xbox360_get_data;
        padf[pad].set_rumble = xbox360_set_rumble;
        padf[pad].set_mode = xbox360_set_mode;
    }

    if (sceUsbdRegisterLdd(&usb_driver) != USB_RC_OK) {
        DPRINTF("Error registering USB devices\n");
        return 0;
    }

    return 1;
}
