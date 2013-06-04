/* Copyright (c) 2013 Richard Russon (FlatCap)
 *
 * This program is free software; you can redistribute it and/or modify it under
 * the terms of the GNU General Public License as published by the Free Software
 * Foundation; either version 2 of the License, or (at your option) any later
 * version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE.  See the GNU Library General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc., 59 Temple
 * Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#ifndef _USB_H_
#define _USB_H_

#include "usbtypes.h"

/**
 * struct usbmon - 64 bytes
 */
typedef struct _usbmon
{
	u64  id;		/*  0: URB ID - from submission to callback */
	u8   type;		/*  8: Same as text; extensible. */
	u8   xfer_type;		/*  9: ISO (0), Intr, Control, Bulk (3) */
	u8   epnum;		/* 10: Endpoint number and transfer direction */
	u8   devnum;		/* 11: Device address */
	u16  busnum;		/* 12: Bus number */
	char flag_setup;	/* 14: Same as text */
	char flag_data;		/* 15: Same as text; Binary zero is OK. */
	s64  ts_sec;		/* 16: gettimeofday */
	s32  ts_usec;		/* 24: gettimeofday */
	int  status;		/* 28: */
	u32  length;		/* 32: Length of data (submitted or actual) */
	u32  len_cap;		/* 36: Delivered length */
	u8   setup[8];		/* 40: Only for Control S-type */
} __attribute__((__packed__)) usbmon;

/**
 * struct usbmon_setup - 8 bytes
 */
typedef struct _usbmon_setup
{
	u8  bmRequestType;
	u8  bRequest;
	u16 wValue;
	u16 wIndex;
	u16 wLength;
} __attribute__((__packed__)) usbmon_setup;

/**
 * struct usb_device_descriptor - 18 bytes
 */
typedef struct _usb_device_descriptor {
	u8  bLength;            //  0 Descriptor size in bytes (12h)
	u8  bDescriptorType;    //  1 The constant DEVICE (01h)
	u16 bcdUSB;             //  2 USB specification release number (BCD). For USB 2.0, byte 2 = 00h and byte 3 = 02h.
	u8  bDeviceClass;       //  4 Class code. For mass storage, set to 00h (the class is specified in the interface descriptor).
	u8  bDeviceSubclass;    //  5 Subclass code. For mass storage, set to 00h.
	u8  bDeviceProtocol;    //  6 Protocol Code. For mass storage, set to 00h.
	u8  bMaxPacketSize0;    //  7 Maximum packet size for endpoint zero.
	u16 idVendor;           //  8 Vendor ID. Obtained from USB-IF.
	u16 idProduct;          // 10 Product ID. Assigned by the product vendor.
	u16 bcdDevice;          // 12 Device release number (BCD). Assigned by the product vendor.
	u8  iManufacturer;      // 14 Index of string descriptor for the manufacturer. Set to 00h if there is no string descriptor.
	u8  iProduct;           // 15 Index of string descriptor for the product. Set to 00h if there is no string descriptor.
	u8  iSerialNumber;      // 16 Index of string descriptor containing the serial number
	u8  bNumConfigurations; // 17 Number of possible configurations. Typically 01h.
} __attribute__((__packed__)) usb_device_descriptor;

/**
 * struct command_block_wrapper - 31 bytes
 */
typedef struct _command_block_wrapper {
	char dCBWSignature[4];		// 0x00
	u32  dCBWTag;			// 0x04
	u32  dCBWDataTransferLength;	// 0x08
	u8   bmCBWFlags;		// 0x0C
	u8   bCBWLUN;			// 0x0D
	u8   bCBWCBLength;		// 0x0E
	u8   CBWCB[16];			// 0x0F
} __attribute__((__packed__)) command_block_wrapper;

/**
 * struct command_status_wrapper - 13 bytes
 */
typedef struct _command_status_wrapper {
	char dCSWSignature[4];	// 0x00
	u32  dCSWTag;		// 0x04
	u32  dCSWDataResidue;	// 0x08
	u8   bCSWStatus;	// 0x0C
} __attribute__((__packed__)) command_status_wrapper;


#endif // _USB_H_

