/* UsbAudio.c

Contains the main code for the USB audio EFI application.

Copyright (C) 2021 Ethin Probst.

SPDX-License-Identifier: BSD-2-Clause-Patent

*/

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

EFI_STATUS EFIAPI UefiMain(IN EFI_HANDLE imageHandle, IN EFI_SYSTEM_TABLE* st) {
  Print(L"Attempting to find USB IO protocol\n");
  UINTN numHandles = 0;
  UINTN i = 0;
  UINT32 UsbStatus = 0;
  EFI_HANDLE* handles = NULL;
  EFI_USB_IO_PROTOCOL* UsbIo = NULL;
  EFI_STATUS status = st->BootServices->LocateHandleBuffer(ByProtocol, &gEfiUsbIoProtocolGuid, NULL, &numHandles, &handles);
  if (EFI_ERROR(status)) {
    Print(L"Cannot find any handles for USB devices, reason: %r\n", status);
    return EFI_ABORTED;
  }
  Print(L"Found %d USB devices; enumerating\n", numHandles);
  for (; i < numHandles; ++i) {
    Print(L"Trying to open handle %d (%x)... ", i, handles[i]);
    status = st->BootServices->OpenProtocol(handles[i], &gEfiUsbIoProtocolGuid, (void**)&UsbIo, imageHandle, NULL, EFI_OPEN_PROTOCOL_EXCLUSIVE);
    if (EFI_ERROR(status))
      goto failed;

    Print(L"OK\n");
    Print(L"Resetting port... ");
    status = UsbIo->UsbPortReset(UsbIo);
    if (EFI_ERROR(status))
      goto failed;

    Print(L"OK\n");
    Print(L"Reading interface descriptor... ");
    EFI_USB_INTERFACE_DESCRIPTOR interfaceDescriptor = {0};
    status = UsbIo->UsbGetInterfaceDescriptor(UsbIo, &interfaceDescriptor);
    if (EFI_ERROR(status))
      goto failed;

    Print(L"Success\n");
    if (interfaceDescriptor.InterfaceClass != 0x01) {
      Print(L"Not an audio device, skipping\n");
      Print(L"Closing opened protocol... ");
      status = st->BootServices->CloseProtocol(handles[i], &gEfiUsbIoProtocolGuid, imageHandle, NULL);
      if (EFI_ERROR(status))
        goto failed;

      Print(L"OK\n");
    continue;
    }
    Print(L"Finding class-specific AC interface descriptor... ");
    UINT8* Header = 0;
        status = st->BootServices->AllocatePool(EfiBootServicesData, 65536, (void*)&Header);
    if (EFI_ERROR(status))
      goto failed;

    EFI_USB_DEVICE_REQUEST req = {0};
    req.RequestType = (USB_DEV_GET_DESCRIPTOR_REQ_TYPE | USB_REQ_TYPE_CLASS | USB_ENDPOINT_DIR_IN);
    req.Request = USB_REQ_GET_DESCRIPTOR;
    req.Value = (0x24 << 8);
    req.Index = interfaceDescriptor.InterfaceNumber;
    req.Length = 2;
    status = UsbIo->UsbControlTransfer(UsbIo, &req, EfiUsbDataIn, PcdGet32 (PcdUsbTransferTimeoutValue), &Header, req.Length, &UsbStatus);
    if (EFI_ERROR(status)) {
      st->BootServices->FreePool(Header);
      goto failed;
}
  req.Length = (Header[6] << 8) | Header[5];
    status = UsbIo->UsbControlTransfer(UsbIo, &req, EfiUsbDataIn, PcdGet32 (PcdUsbTransferTimeoutValue), &Header, req.Length, &UsbStatus);
    if (EFI_ERROR(status)) {
      st->BootServices->FreePool(Header);
      goto failed;
    }
      Print(L"Length is %d, total length is %d, USB ADC version is %04x, %d interfaces in collection\n", Header[0], req.Length, Header[3], Header[7]);
    Print(L"Closing protocol... ");
    status = st->BootServices->CloseProtocol(handles[i], &gEfiUsbIoProtocolGuid, imageHandle, NULL);
    if (EFI_ERROR(status))
      goto failed;

    Print(L"Freeing header pool... ");
    status = st->BootServices->FreePool(Header);
    if (EFI_ERROR(status))
      goto failed;

    UsbIo = NULL;
    Print(L"Done\n");
  }
  Print(L"Freeing handle buffer... ");
  status = st->BootServices->FreePool(handles);
  if (EFI_ERROR(status))
    goto failed;

  Print(L"Done\n");
  return EFI_SUCCESS;
  failed:
  st->BootServices->FreePool(handles);
  handles = NULL;
  if (UsbIo)
    st->BootServices->CloseProtocol(handles[i], &gEfiUsbIoProtocolGuid, imageHandle, NULL);

  UsbIo = NULL;
  Print(L"%r (%04x)\n", status, UsbStatus);
  return EFI_ABORTED;
}


