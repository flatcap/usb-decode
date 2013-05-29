#include <stdint.h>

/**
 * usb_device_descriptor - 18 bytes
 */
typedef struct usb_dd {
	uint8_t  bLength;            //  0 Descriptor size in bytes (12h)
	uint8_t  bDescriptorType;    //  1 The constant DEVICE (01h)
	uint16_t bcdUSB;             //  2 USB specification release number (BCD). For USB 2.0, byte 2 = 00h and byte 3 = 02h.
	uint8_t  bDeviceClass;       //  4 Class code. For mass storage, set to 00h (the class is specified in the interface descriptor).
	uint8_t  bDeviceSubclass;    //  5 Subclass code. For mass storage, set to 00h.
	uint8_t  bDeviceProtocol;    //  6 Protocol Code. For mass storage, set to 00h.
	uint8_t  bMaxPacketSize0;    //  7 Maximum packet size for endpoint zero.
	uint16_t idVendor;           //  8 Vendor ID. Obtained from USB-IF.
	uint16_t idProduct;          // 10 Product ID. Assigned by the product vendor.
	uint16_t bcdDevice;          // 12 Device release number (BCD). Assigned by the product vendor.
	uint8_t  iManufacturer;      // 14 Index of string descriptor for the manufacturer. Set to 00h if there is no string descriptor.
	uint8_t  iProduct;           // 15 Index of string descriptor for the product. Set to 00h if there is no string descriptor.
	uint8_t  iSerialNumber;      // 16 Index of string descriptor containing the serial number
	uint8_t  bNumConfigurations; // 17 Number of possible configurations. Typically 01h.
} usb_device_descriptor;


#if 0

/**
 * Section 5.1: Command Block Wrapper (CBW)
 */
struct command_block_wrapper {
	uint8_t dCBWSignature[4];	// 0x00
	uint32_t dCBWTag;		// 0x04
	uint32_t dCBWDataTransferLength;// 0x08
	uint8_t bmCBWFlags;		// 0x0C
	uint8_t bCBWLUN;		// 0x0D
	uint8_t bCBWCBLength;		// 0x0E
	uint8_t CBWCB[16];		// 0x0F
}  __attribute__((__packed__));

/**
 * Section 5.2: Command Status Wrapper (CSW)
 */
struct command_status_wrapper {
	uint8_t dCSWSignature[4];	// 0x00
	uint32_t dCSWTag;		// 0x04
	uint32_t dCSWDataResidue;	// 0x08
	uint8_t bCSWStatus;		// 0x0C
}  __attribute__((__packed__));

#endif

