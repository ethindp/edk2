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
#include "sine.h"

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
    if (EFI_ERROR(status)) {
      Print(L"%r, skipping\n", status);
      continue;
    }

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
    switch (interfaceDescriptor.InterfaceSubClass) {
      case 0x01:
        Print(L"Found audio control device\n");
      break;
      case 0x02:
       Print(L"Found audio streaming device\n");
      break;
      case 0x03:
        Print(L"Found midi streaming device\n");
      break;
      default: {
        Print(L"Unsupported device");
        return EFI_ABORTED;
      }
    }
    Print(L"Reading descriptor data\n");
    UINT8* Header = 0;
        status = st->BootServices->AllocatePool(EfiBootServicesData, 65536, (void*)&Header);
    if (EFI_ERROR(status))
      goto failed;

    for (UINTN i = 0; i < 0xFFFF; ++i)
      Header[i] = 0;

    status = UsbGetDescriptor(UsbIo, (USB_DESC_TYPE_DEVICE << 8) | 0, 0, 2, (void*)Header, &UsbStatus);
    if (EFI_ERROR(status)) {
      st->BootServices->FreePool(Header);
      goto failed;
}
    status = UsbGetDescriptor(UsbIo, (USB_DESC_TYPE_DEVICE << 8) | 0, 0, Header[0], (void*)Header, &UsbStatus);
    if (EFI_ERROR(status)) {
      st->BootServices->FreePool(Header);
      goto failed;
    }
    Print(L"Device descriptor: device has %d configurations\n", Header[17]);
    Print(L"Configuring device... ");
    status = UsbSetConfiguration(UsbIo, Header[17] - 1, &UsbStatus);
    if (EFI_ERROR(status))
      goto failed;

    Print(L"Ok\n");
    Print(L"Reading configuration information... ");
    for (UINTN i = 0; i < 0xFFFF; ++i) Header[i] = 0;
    status = UsbGetDescriptor(UsbIo, (USB_DESC_TYPE_CONFIG << 8) | 0, 0, 9, (void*)Header, &UsbStatus);
    if (EFI_ERROR(status)) {
      st->BootServices->FreePool(Header);
      goto failed;
}
    status = UsbGetDescriptor(UsbIo, (USB_DESC_TYPE_CONFIG << 8) | 0, 0, (Header[3] << 8) | Header[2], (void*)Header, &UsbStatus);
    if (EFI_ERROR(status)) {
      st->BootServices->FreePool(Header);
      goto failed;
    }
    Print(L"Ok\n");
    Print(L"Setting interface alternate settings\n");
    for (UINT16 interface = 0; interface < (UINT16)Header[4]; ++interface) {
      status = UsbSetInterface(UsbIo, interface, 1, &UsbStatus);
      if (EFI_ERROR(status))
        Print(L"Skipping interface %d: no alternate settings\n", interface);
    }
    if (interfaceDescriptor.InterfaceSubClass == 0x02) {
      Print(L"Initiating stream... ");
      status = UsbIo->UsbIsochronousTransfer(UsbIo, 0x81, SINE_SAMPLES, 48000, &UsbStatus);
      if (EFI_ERROR(status))
        goto failed;
    }
    Print(L"Closing protocol... ");
    status = st->BootServices->CloseProtocol(handles[i], &gEfiUsbIoProtocolGuid, imageHandle, NULL);
    if (EFI_ERROR(status))
      goto failed;

    Print(L"Ok\n");
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

