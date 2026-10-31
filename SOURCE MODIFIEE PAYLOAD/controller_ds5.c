/* controller_ds5.c — DualSense USB report → ScePadData
 *
 * Wire format matches DS5Dongle USBGetStateData (utils.h), report ID 0x01.
 * No handshake: once the IN endpoint is open, reports stream on their own.
 * The PS5 cryptographic check never sees this device; input is injected
 * through scePadVirtualDeviceInsertData.
 */

#include "controller_ds5.h"
#include "controller_ds4.h" /* VID_SONY */
#include <string.h>

/* Hat 0..8 → dpad bits. Same encoding as DS4, different byte. */
static const uint32_t DS5_HAT_DPAD[9] = {
    /* 0 N  */ SCE_PAD_BUTTON_UP,
    /* 1 NE */ SCE_PAD_BUTTON_UP   | SCE_PAD_BUTTON_RIGHT,
    /* 2 E  */ SCE_PAD_BUTTON_RIGHT,
    /* 3 SE */ SCE_PAD_BUTTON_DOWN | SCE_PAD_BUTTON_RIGHT,
    /* 4 S  */ SCE_PAD_BUTTON_DOWN,
    /* 5 SW */ SCE_PAD_BUTTON_DOWN | SCE_PAD_BUTTON_LEFT,
    /* 6 W  */ SCE_PAD_BUTTON_LEFT,
    /* 7 NW */ SCE_PAD_BUTTON_UP   | SCE_PAD_BUTTON_LEFT,
    /* 8 -- */ 0u,
};

int ds5_is_usb_vidpid(uint16_t vid, uint16_t pid) {
    return (vid == GHOST_DONGLE_VID && pid == GHOST_DONGLE_PID) ||
           (vid == VID_SONY && (pid == PID_DS5 || pid == PID_DS5_EDGE));
}

/* One touch finger: 4 bytes, little-endian bitfield from USBGetStateData.
 * Index:7, NotTouching:1, FingerX:12, FingerY:12. */
static int ds5_parse_finger(const uint8_t *b, ScePadTouch *out) {
    uint8_t not_touching = (uint8_t)((b[0] >> 7) & 0x01u);
    if (not_touching)
        return 0;

    uint16_t x = (uint16_t)(b[1] | ((b[2] & 0x0Fu) << 8));
    uint16_t y = (uint16_t)(((b[2] & 0xF0u) >> 4) | (b[3] << 4));

    out->x = x;
    out->y = y;
    out->finger = (uint8_t)(b[0] & 0x7Fu);
    out->pad[0] = 0;
    out->pad[1] = 0;
    out->pad[2] = 0;
    return 1;
}

int ds5_handle_packet(int fd, struct usb_fs_endpoint *eps,
                      const uint8_t *buf, uint32_t len,
                      ScePadData *out_pad) {
    (void)fd;
    (void)eps;

    /* Report ID + sticks + triggers + button block. Touch is optional. */
    if (len < 11 || buf[0] != 0x01)
        return 0;

    out_pad->leftStick.x      = buf[1];
    out_pad->leftStick.y      = buf[2];
    out_pad->rightStick.x     = buf[3];
    out_pad->rightStick.y     = buf[4];
    out_pad->analogButtons.l2 = buf[5];
    out_pad->analogButtons.r2 = buf[6];

    uint32_t btn = 0;

    uint8_t hat = buf[8] & 0x0Fu;
    if (hat <= 8)
        btn |= DS5_HAT_DPAD[hat];
    if (buf[8] & 0x10u) btn |= SCE_PAD_BUTTON_SQUARE;
    if (buf[8] & 0x20u) btn |= SCE_PAD_BUTTON_CROSS;
    if (buf[8] & 0x40u) btn |= SCE_PAD_BUTTON_CIRCLE;
    if (buf[8] & 0x80u) btn |= SCE_PAD_BUTTON_TRIANGLE;

    if (buf[9] & 0x01u) btn |= SCE_PAD_BUTTON_L1;
    if (buf[9] & 0x02u) btn |= SCE_PAD_BUTTON_R1;
    if (buf[9] & 0x04u) btn |= SCE_PAD_BUTTON_L2;
    if (buf[9] & 0x08u) btn |= SCE_PAD_BUTTON_R2;
    if (buf[9] & 0x10u) btn |= SCE_PAD_BUTTON_SHARE;   /* Create */
    if (buf[9] & 0x20u) btn |= SCE_PAD_BUTTON_OPTIONS;
    if (buf[9] & 0x40u) btn |= SCE_PAD_BUTTON_L3;
    if (buf[9] & 0x80u) btn |= SCE_PAD_BUTTON_R3;

    if (buf[10] & 0x01u) btn |= SCE_PAD_BUTTON_PS;
    if (buf[10] & 0x02u) btn |= SCE_PAD_BUTTON_TOUCH_PAD;

    out_pad->buttons = btn;

    /* TouchData starts at USBGetStateData offset 32, so packet offset 33. */
    out_pad->touchData.fingers = 0;
    if (len >= 41) {
        ScePadTouch finger;
        if (ds5_parse_finger(buf + 33, &finger)) {
            out_pad->touchData.touch[out_pad->touchData.fingers] = finger;
            out_pad->touchData.fingers++;
        }
        if (ds5_parse_finger(buf + 37, &finger)) {
            out_pad->touchData.touch[out_pad->touchData.fingers] = finger;
            out_pad->touchData.fingers++;
        }
    }

    out_pad->connected = 1;
    out_pad->quat.w = 1.0f;
    return 1;
}
