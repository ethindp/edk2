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
    Print(L"Finding class-specific AC interface descriptor... ");
    UINT8* Header = 0;
        status = st->BootServices->AllocatePool(EfiBootServicesData, 65536, (void*)&Header);
    if (EFI_ERROR(status))
      goto failed;

    for (UINTN i = 0; i < 0xFFFF; ++i)
      Header[i] = 0;

    status = UsbGetDescriptor(UsbIo, (USB_DESC_TYPE_CONFIG << 8) | 0, 0, 9, (void*)Header, &UsbStatus);
    if (EFI_ERROR(status)) {
      st->BootServices->FreePool(Header);
      goto failed;
}
    status = UsbGetDescriptor(UsbIo, (USB_DESC_TYPE_CONFIG << 8) | 0, 0, Header[2] | Header[3] << 8, (void*)Header, &UsbStatus);
    if (EFI_ERROR(status)) {
      st->BootServices->FreePool(Header);
      goto failed;
    }
    UINTN DescriptorPosition = 0;
    for (UINTN i = 0; i < 0xFFFF; ++i) {
      if (Header[i] == 0x24 && Header[i + 1] == 0x01) {
        DescriptorPosition = i - 1;
        break;
      }
    }
    if (DescriptorPosition == 0) {
      st->BootServices->FreePool(Header);
      status = EFI_NOT_FOUND;
      goto failed;
    }
    Print(L"\n");
    Print(L"Length is %d, total length is %d, total length of interface descriptor is %d, USB ADC version is %04x, %d interfaces in collection\n", Header[DescriptorPosition], (Header[3] << 8) | Header[2], (Header[DescriptorPosition + 6] << 8) | Header[DescriptorPosition + 5], (Header[DescriptorPosition + 4] << 8) | Header[DescriptorPosition + 3], Header[DescriptorPosition + 7]);
    for (UINTN i = 0; i < Header[DescriptorPosition + 7]; ++i)
      Print(L"Interface %d: No. %d\n", i, Header[DescriptorPosition + 8 + i]);
    for (UINTN i = 0; i < Header[DescriptorPosition + 7]; ++i) {
      status = UsbSetInterface(UsbIo, Header[DescriptorPosition + 8 + i], 0, &UsbStatus);
      if (EFI_ERROR(status))
        goto failed;
    }
    for (UINTN i = 0; i < Header[DescriptorPosition + 7]; ++i) {
      status = UsbSetInterface(UsbIo, Header[DescriptorPosition + 8 + i], 1, &UsbStatus);
      if (EFI_ERROR(status))
        goto failed;
    }
    if (interfaceDescriptor.InterfaceSubClass == 0x02) {
      status = UsbIo->UsbIsochronousTransfer(UsbIo, 0x00|0x80, SINE_SAMPLES, 48000, &UsbStatus);
      if (EFI_ERROR(status))
        goto failed;
    }
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

