#include <Uefi.h>
#include <Library/BaseLib.h>
#include <Library/UefiApplicationEntryPoint.h>
#include <Library/DebugLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiLib.h>
#include <Library/UefiUsbLib.h>
#include <Library/PcdLib.h>
#include <Library/BaseMemoryLib.h>
#include <Protocol/UsbIo.h>
#include <IndustryStandard/Usb.h>
#include "Descriptors.h"

STATIC EFI_STATUS ReadDescriptor(IN EFI_USB_IO_PROTOCOL *UsbIo, IN UINT8 DescriptorType, IN UINT8 Index, OUT VOID* Buffer, OUT UINT32* UsbStatus);

EFI_STATUS EFIAPI UefiMain(IN EFI_HANDLE imageHandle, IN EFI_SYSTEM_TABLE* st) {
Print(L"Attempting to find USB IO protocol\n");
UINTN numHandles = 0;
EFI_HANDLE* handles = NULL;
EFI_STATUS status = st->BootServices->LocateHandleBuffer(ByProtocol, &gEfiUsbIoProtocolGuid, NULL, &numHandles, &handles);
if (EFI_ERROR(status)) {
Print(L"Cannot find any handles for USB devices, reason: %r\n", status);
return EFI_ABORTED;
}
Print(L"Found %d USB devices; enumerating\n", numHandles);
for (UINTN i = 0; i < numHandles; ++i) {
EFI_USB_IO_PROTOCOL* UsbIo = NULL;
Print(L"Trying to open handle %d (%x)... ", i, handles[i]);
status = st->BootServices->OpenProtocol(handles[i], &gEfiUsbIoProtocolGuid, (void**)&UsbIo, imageHandle, NULL, EFI_OPEN_PROTOCOL_EXCLUSIVE);
if (EFI_ERROR(status)) {
Print(L"error: %r\n", status);
return EFI_ABORTED;
}
Print(L"OK\n");
Print(L"Resetting port... ");
status = UsbIo->UsbPortReset(UsbIo);
if (EFI_ERROR(status)) {
Print(L"Failed (%r)\n", status);
Print(L"Closing opened protocol... ");
status = st->BootServices->CloseProtocol(handles[i], &gEfiUsbIoProtocolGuid, imageHandle, NULL);
if (EFI_ERROR(status)) {
Print(L"Error: %r\n", status);
st->BootServices->FreePool(handles);
return status;
}
Print(L"OK\n");
continue;
}
Print(L"OK\n");
Print(L"Reading interface descriptor... ");
EFI_USB_INTERFACE_DESCRIPTOR interfaceDescriptor = {0};
status = UsbIo->UsbGetInterfaceDescriptor(UsbIo, &interfaceDescriptor);
if (EFI_ERROR(status)) {
Print(L"Error: %r\n", status);
Print(L"Closing opened protocol... ");
status = st->BootServices->CloseProtocol(handles[i], &gEfiUsbIoProtocolGuid, imageHandle, NULL);
if (EFI_ERROR(status)) {
Print(L"Error: %r\n", status);
st->BootServices->FreePool(handles);
return status;
}
Print(L"OK\n");
continue;
}
Print(L"Success\n");
if (interfaceDescriptor.InterfaceClass != 0x01) {
Print(L"Not an audio device, skipping\n");
Print(L"Closing opened protocol... ");
status = st->BootServices->CloseProtocol(handles[i], &gEfiUsbIoProtocolGuid, imageHandle, NULL);
if (EFI_ERROR(status)) {
Print(L"Error: %r\n", status);
st->BootServices->FreePool(handles);
return status;
}
Print(L"OK\n");
continue;
}
Print(L"Finding class-specific AC interface descriptor... ");
for (UINT8 i = 0; i < 0xFF; ++i) {
UINT8 header[256];
UINT32 usbStatus;
status = ReadDescriptor(UsbIo, 0x24, i, header, &usbStatus);
if (!EFI_ERROR(status) && header[3] == 0x00 && header[4] == 0x01) {
Print(L"Found\n");
Print(L"Total length field is %d with %d interfaces\n", (header[6] << 8) | header[5], header[7]);
Print(L"Descriptor length is %d and descriptor subtype is %x\n", header[0], header[2]);
} else {
continue;
}
}
Print(L"Closing protocol... ");
status = st->BootServices->CloseProtocol(handles[i], &gEfiUsbIoProtocolGuid, imageHandle, NULL);
if (EFI_ERROR(status)) {
Print(L"Error: %r\n", status);
return EFI_ABORTED;
}
Print(L"Done\n");
}
Print(L"Freeing handle buffer... ");
status = st->BootServices->FreePool(handles);
if (EFI_ERROR(status)) {
Print(L"Error: %r\n", status);
return status;
}
Print(L"Done\n");
return EFI_SUCCESS;
}

STATIC EFI_STATUS ReadDescriptor(IN EFI_USB_IO_PROTOCOL *UsbIo, IN UINT8 DescriptorType, IN UINT8 Index, OUT VOID* Buffer, OUT UINT32* UsbStatus) {
UINT8 header[2] = {0};
EFI_USB_DEVICE_REQUEST Request = {0};
Request.RequestType = USB_ENDPOINT_DIR_IN | USB_REQ_TYPE_STANDARD | USB_TARGET_DEVICE;
Request.Request = USB_REQ_GET_DESCRIPTOR;
Request.Index = 0;
Request.Value = DescriptorType << 8 | Index;
Request.Length = sizeof (header);
EFI_STATUS status = UsbIo->UsbControlTransfer (UsbIo, &Request, EfiUsbDataIn, 10000, header, sizeof header, UsbStatus);
if (EFI_ERROR(status)) {
DEBUG ((DEBUG_ERROR, "Failed to read length of descriptor type x%x, index %u (code %r, USB status x%x)\n", DescriptorType, Index, status, *UsbStatus));
return status;
}
CONST UINT16 TotalLength = header[0];
Request.Length = TotalLength;
status = UsbIo->UsbControlTransfer (UsbIo, &Request, EfiUsbDataIn, 10000, Buffer, TotalLength, UsbStatus);
return status;
}


