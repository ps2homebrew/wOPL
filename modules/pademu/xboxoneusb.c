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
#include "xboxoneusb.h"
#include "padmacro.h"

#define MODNAME "XBOXONEUSB"

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

static UsbDriver usb_driver = {NULL, NULL, "xboxoneusb", usb_probe, usb_connect, usb_disconnect};

static void readReport(u8 *data, int pad);
static int LEDRumble(u8 *led, u8 lrum, u8 rrum, int pad);

static int xboxone_get_model(struct pad_funcs *pf, int port);
static int xboxone_get_data(struct pad_funcs *pf, u8 *dst, int size, int port);
static void xboxone_set_rumble(struct pad_funcs *pf, u8 lrum, u8 rrum);
static void xboxone_set_mode(struct pad_funcs *pf, int mode, int lock);

static xboxoneusb_device xboxonepad[MAX_PADS];
static struct pad_funcs padf[MAX_PADS];
static u8 cmdcnt = 0;

static int usb_probe(int devId)
{
    UsbDeviceDescriptor *device = NULL;

    DPRINTF("probe: devId=%i\n", devId);

    device = (UsbDeviceDescriptor *)sceUsbdScanStaticDescriptor(devId, NULL, USB_DT_DEVICE);
    if (device == NULL) {
        DPRINTF("Error - Couldn't get device descriptor\n");
        return 0;
    }

    if ((device->idVendor == XBOX_VID || device->idVendor == XBOX_VID2 || device->idVendor == XBOX_VID3 || device->idVendor == XBOX_VID4 || device->idVendor == XBOX_VID5 || device->idVendor == XBOX_VID6) &&
        (device->idProduct == XBOX_ONE_PID1 || device->idProduct == XBOX_ONE_PID2 || device->idProduct == XBOX_ONE_PID3 || device->idProduct == XBOX_ONE_PID4 ||
         device->idProduct == XBOX_ONE_PID5 || device->idProduct == XBOX_ONE_PID6 || device->idProduct == XBOX_ONE_PID7 || device->idProduct == XBOX_ONE_PID8 ||
         device->idProduct == XBOX_ONE_PID9 || device->idProduct == XBOX_ONE_PID10 || device->idProduct == XBOX_ONE_PID11 || device->idProduct == XBOX_ONE_PID12 || device->idProduct == XBOX_ONE_PID13))
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
        if (xboxonepad[pad].devId == -1 && xboxonepad[pad].enabled)
            break;
    }

    if (pad >= MAX_PADS) {
        DPRINTF("Error - only %d device allowed !\n", MAX_PADS);
        return 1;
    }

    PollSema(xboxonepad[pad].sema);

    xboxonepad[pad].devId = devId;

    xboxonepad[pad].status = XBOXONEUSB_STATE_AUTHORIZED;

    xboxonepad[pad].controlEndp = sceUsbdOpenPipe(devId, NULL);

    device = (UsbDeviceDescriptor *)sceUsbdScanStaticDescriptor(devId, NULL, USB_DT_DEVICE);
    config = (UsbConfigDescriptor *)sceUsbdScanStaticDescriptor(devId, device, USB_DT_CONFIG);

    xboxonepad[pad].type = DS4;
    epCount = 20; // ds4 v2 returns interface->bNumEndpoints as 0

    endpoint = (UsbEndpointDescriptor *)sceUsbdScanStaticDescriptor(devId, NULL, USB_DT_ENDPOINT);

    do {
        if (endpoint->bmAttributes == USB_ENDPOINT_XFER_INT) {
            if ((endpoint->bEndpointAddress & USB_ENDPOINT_DIR_MASK) == USB_DIR_IN && xboxonepad[pad].interruptEndp < 0) {
                xboxonepad[pad].interruptEndp = sceUsbdOpenPipe(devId, endpoint);
                DPRINTF("register Event endpoint id =%i addr=%02X packetSize=%i\n", xboxonepad[pad].interruptEndp, endpoint->bEndpointAddress, (unsigned short int)endpoint->wMaxPacketSizeHB << 8 | endpoint->wMaxPacketSizeLB);
            }
            if ((endpoint->bEndpointAddress & USB_ENDPOINT_DIR_MASK) == USB_DIR_OUT && xboxonepad[pad].outEndp < 0) {
                xboxonepad[pad].outEndp = sceUsbdOpenPipe(devId, endpoint);
                DPRINTF("register Output endpoint id =%i addr=%02X packetSize=%i\n", xboxonepad[pad].outEndp, endpoint->bEndpointAddress, (unsigned short int)endpoint->wMaxPacketSizeHB << 8 | endpoint->wMaxPacketSizeLB);
            }
        }

        endpoint = (UsbEndpointDescriptor *)((char *)endpoint + endpoint->bLength);

    } while (epCount--);

    if (xboxonepad[pad].interruptEndp < 0 || xboxonepad[pad].outEndp < 0) {
        usb_release(pad);
        return 1;
    }

    xboxonepad[pad].status |= XBOXONEUSB_STATE_CONNECTED;

    sceUsbdSetConfiguration(xboxonepad[pad].controlEndp, config->bConfigurationValue, usb_config_set, (void *)pad);
    SignalSema(xboxonepad[pad].sema);

    return 0;
}

static int usb_disconnect(int devId)
{
    u8 pad;

    DPRINTF("disconnect: devId=%i\n", devId);

    for (pad = 0; pad < MAX_PADS; pad++) {
        if (xboxonepad[pad].devId == devId)
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
    PollSema(xboxonepad[pad].sema);

    if (xboxonepad[pad].interruptEndp >= 0)
        sceUsbdClosePipe(xboxonepad[pad].interruptEndp);

    if (xboxonepad[pad].outEndp >= 0)
        sceUsbdClosePipe(xboxonepad[pad].outEndp);

    xboxonepad[pad].controlEndp = -1;
    xboxonepad[pad].interruptEndp = -1;
    xboxonepad[pad].outEndp = -1;
    xboxonepad[pad].devId = -1;
    xboxonepad[pad].status = XBOXONEUSB_STATE_DISCONNECTED;

    SignalSema(xboxonepad[pad].sema);
}

static int usb_resulCode;

static void usb_data_cb(int resultCode, int bytes, void *arg)
{
    int pad = (int)arg;

    DPRINTF("usb_data_cb: res %d, bytes %d, arg %p \n", resultCode, bytes, arg);

    usb_resulCode = resultCode;

    SignalSema(xboxonepad[pad].sema);
}

static void usb_cmd_cb(int resultCode, int bytes, void *arg)
{
    int pad = (int)arg;

    DPRINTF("usb_cmd_cb: res %d, bytes %d, arg %p \n", resultCode, bytes, arg);

    SignalSema(xboxonepad[pad].cmd_sema);
}

static void usb_config_set(int result, int count, void *arg)
{
    int pad = (int)arg;
    u8 led[4];

    PollSema(xboxonepad[pad].sema);

    cmdcnt = 0;
    usb_buf[0] = 0x05;
    usb_buf[1] = 0x20;
    usb_buf[2] = cmdcnt++;
    usb_buf[3] = 0x01;
    usb_buf[4] = 0x00;
    UsbInterruptTransfer(xboxonepad[pad].outEndp, usb_buf, 5, NULL, NULL);
    DelayThread(10000);

    SignalSema(xboxonepad[pad].sema);

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
    xboxonereport_t *report = (xboxonereport_t *)data;
    if (report->ReportID == 0x20) {
        xboxonepad[pad_idx].data[0] = ~(report->Back | report->LS << 1 | report->RS << 2 | report->Start << 3 | report->Up << 4 | report->Right << 5 | report->Down << 6 | report->Left << 7);
        xboxonepad[pad_idx].data[1] = ~((report->LeftTriggerH != 0) | (report->RightTriggerH != 0) << 1 | report->LB << 2 | report->RB << 3 | report->Y << 4 | report->B << 5 | report->A << 6 | report->X << 7);

        xboxonepad[pad_idx].data[2] = report->RightStickXH + 128;    // rx
        xboxonepad[pad_idx].data[3] = ~(report->RightStickYH + 128); // ry
        xboxonepad[pad_idx].data[4] = report->LeftStickXH + 128;     // lx
        xboxonepad[pad_idx].data[5] = ~(report->LeftStickYH + 128);  // ly

        xboxonepad[pad_idx].data[6] = report->Right * 255; // right
        xboxonepad[pad_idx].data[7] = report->Left * 255;  // left
        xboxonepad[pad_idx].data[8] = report->Up * 255;    // up
        xboxonepad[pad_idx].data[9] = report->Down * 255;  // down

        xboxonepad[pad_idx].data[10] = report->Y * 255; // triangle
        xboxonepad[pad_idx].data[11] = report->B * 255; // circle
        xboxonepad[pad_idx].data[12] = report->A * 255; // cross
        xboxonepad[pad_idx].data[13] = report->X * 255; // square

        xboxonepad[pad_idx].data[14] = report->LB * 255;      // L1
        xboxonepad[pad_idx].data[15] = report->RB * 255;      // R1
        xboxonepad[pad_idx].data[16] = report->LeftTriggerH;  // L2
        xboxonepad[pad_idx].data[17] = report->RightTriggerH; // R2
    }
#endif
}

static int Rumble(u8 *led, u8 lrum, u8 rrum, int pad)
{
    PollSema(xboxonepad[pad].cmd_sema);

    usb_buf[0] = 0x09;
    usb_buf[1] = 0x00;
    usb_buf[2] = cmdcnt++;
    usb_buf[3] = 0x09;  // Substructure (what substructure rest of this packet has)
    usb_buf[4] = 0x00;  // Mode
    usb_buf[5] = 0x0F;  // Rumble mask (what motors are activated) (0000 lT rT L R)
    usb_buf[6] = 0x00;  // lT force
    usb_buf[7] = 0x00;  // rT force
    usb_buf[8] = lrum;  // L force
    usb_buf[9] = rrum;  // R force
    usb_buf[10] = 0x80; // Length of pulse
    usb_buf[11] = 0x00; // Off period
    usb_buf[12] = 0x00; // Repeat count

    return sceUsbdInterruptTransfer(xboxonepad[pad].outEndp, usb_buf, 13, usb_cmd_cb, (void *)pad);
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

static void xboxone_set_rumble(struct pad_funcs *pf, u8 lrum, u8 rrum)
{
    xboxoneusb_device *pad = pf->priv;
    WaitSema(pad->sema);

    if ((pad->lrum != lrum) || (pad->rrum != rrum)) {
        pad->lrum = lrum;
        pad->rrum = rrum;
        pad->update_rum = 1;
    }

    SignalSema(pad->sema);
}

static int xboxone_get_data(struct pad_funcs *pf, u8 *dst, int size, int port)
{
    xboxoneusb_device *pad = pf->priv;
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
        ret = Rumble(pad->oldled, pad->lrum, pad->rrum, port);
        if (ret == USB_RC_OK)
            TransferWait(pad->cmd_sema);
        else
            DPRINTF("LEDRumble usb transfer error %d\n", ret);

        pad->update_rum = 0;
    }

    SignalSema(pad->sema);

    return ret;
}

static void xboxone_set_mode(struct pad_funcs *pf, int mode, int lock)
{
    xboxoneusb_device *pad = pf->priv;
    WaitSema(pad->sema);
    if (lock == 3)
        pad->analog_btn = 3;
    else
        pad->analog_btn = mode;
    SignalSema(pad->sema);
}

void xboxoneusb_reset()
{
    int pad;

    for (pad = 0; pad < MAX_PADS; pad++)
        usb_release(pad);
}

static int xboxone_get_model(struct pad_funcs *pf, int port)
{
#ifdef WIP /* TODO: Add xbox 360 guitars. */
    xboxoneusb_device *pad = pf->priv;
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

int xboxoneusb_init(u8 pads, u8 options)
{
    int pad;

    for (pad = 0; pad < MAX_PADS; pad++) {
        xboxonepad[pad].status = 0;
        xboxonepad[pad].devId = -1;
        xboxonepad[pad].oldled[0] = 0;
        xboxonepad[pad].oldled[1] = 0;
        xboxonepad[pad].oldled[2] = 0;
        xboxonepad[pad].oldled[3] = 0;
        xboxonepad[pad].lrum = 0;
        xboxonepad[pad].rrum = 0;
        xboxonepad[pad].update_rum = 1;
        xboxonepad[pad].sema = -1;
        xboxonepad[pad].cmd_sema = -1;
        xboxonepad[pad].controlEndp = -1;
        xboxonepad[pad].interruptEndp = -1;
        xboxonepad[pad].enabled = (pads >> pad) & 1;
        xboxonepad[pad].type = 0;

        xboxonepad[pad].data[0] = 0xFF;
        xboxonepad[pad].data[1] = 0xFF;
        xboxonepad[pad].analog_btn = 0;

        memset(&xboxonepad[pad].data[2], 0x7F, 4);
        memset(&xboxonepad[pad].data[6], 0x00, 12);

        xboxonepad[pad].sema = CreateMutex(IOP_MUTEX_UNLOCKED);
        xboxonepad[pad].cmd_sema = CreateMutex(IOP_MUTEX_UNLOCKED);

        if (xboxonepad[pad].sema < 0 || xboxonepad[pad].cmd_sema < 0) {
            DPRINTF("Failed to allocate I/O semaphore.\n");
            return 0;
        }
        padf[pad].priv = &xboxonepad[pad];
        padf[pad].get_model = xboxone_get_model;
        padf[pad].get_data = xboxone_get_data;
        padf[pad].set_rumble = xboxone_set_rumble;
        padf[pad].set_mode = xboxone_set_mode;
    }

    if (sceUsbdRegisterLdd(&usb_driver) != USB_RC_OK) {
        DPRINTF("Error registering USB devices\n");
        return 0;
    }

    return 1;
}
