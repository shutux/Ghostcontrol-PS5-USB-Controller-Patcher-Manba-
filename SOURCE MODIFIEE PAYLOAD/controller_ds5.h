#pragma once
#include <stdint.h>
#include <dev/usb/usb.h>
#include <dev/usb/usb_ioctl.h>
#include "gc_types.h"

/*
 * DualSense / DualSense Edge over USB, as enumerated by DS5Dongle.
 *
 * In Ghost mode DS5Dongle presents neutral VID:PID CAFE:0D55 so Sony's
 * physical-pad driver does not claim it. Legacy/native mode presents Sony
 * VID 054C and PID 0CE6 (DualSense) or 0DF2 (Edge). Both modes use HID IN
 * 0x84 / OUT 0x03 and stream report ID 0x01. The payload is USBGetStateData
 * from the dongle (63 bytes) with the report ID prepended.
 *
 * This is not the DS4 layout. Sticks sit in the same place, but the
 * triggers move to bytes 5-6 and the button block moves to bytes 8-10.
 *
 *   [0]      report ID 0x01
 *   [1]      LX
 *   [2]      LY   (0 = up, 255 = down, 128 = center — same as ScePad)
 *   [3]      RX
 *   [4]      RY
 *   [5]      L2 analog
 *   [6]      R2 analog
 *   [7]      sequence
 *   [8] lo   hat 0..7 = N,NE,E,SE,S,SW,W,NW; 8 = neutral
 *   [8] hi   0x10 Square  0x20 Cross  0x40 Circle  0x80 Triangle
 *   [9]      0x01 L1  0x02 R1  0x04 L2  0x08 R2
 *            0x10 Create  0x20 Options  0x40 L3  0x80 R3
 *   [10]     0x01 PS  0x02 Touchpad-click  0x04 Mute
 *            0x10/0x20/0x40/0x80 Edge FN / paddles (no ScePad bit)
 *   [33..40] two touch fingers (4 bytes each) + timestamp
 *
 * Retail DualSense uses the same VID/PID but always sets iSerialNumber.
 * DS5Dongle leaves iSerialNumber at 0 unless enable_usb_sn is on.
 * probe uses that to avoid detaching a genuine wired pad.
 */

#define PID_DS5       0x0CE6u
#define PID_DS5_EDGE  0x0DF2u
#define GHOST_DONGLE_VID 0xCAFEu
#define GHOST_DONGLE_PID 0x0D55u

#define DS5_EP_IN     0x84
#define DS5_EP_OUT    0x03

/* 1 if this is a DS5Dongle identity accepted by this payload. */
int ds5_is_usb_vidpid(uint16_t vid, uint16_t pid);

/* Parse one USB input report into ScePadData.
 * Returns 1 if out_pad was filled, 0 if the packet is not report 0x01. */
int ds5_handle_packet(int fd, struct usb_fs_endpoint *eps,
                      const uint8_t *buf, uint32_t len,
                      ScePadData *out_pad);
