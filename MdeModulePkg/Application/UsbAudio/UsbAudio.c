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
    status = st->BootServices->OpenProtocol(handles[i], &gEfiUsbIoProtocolGuid, (void**)&UsbIo, imageHandle, NULL, EFI_OPEN_PROTOCOL_GET_PROTOCOL);
    if (EFI_ERROR(status)) {
      Print(L"%r, skipping\n", status);
      continue;
    }
    EFI_USB_INTERFACE_DESCRIPTOR InterfaceDescriptor = {0};
    status = UsbIo->UsbGetInterfaceDescriptor(UsbIo, &InterfaceDescriptor);
    if (EFI_ERROR(status))
      goto failed;

    if (InterfaceDescriptor.InterfaceClass == 0x01 && (InterfaceDescriptor.InterfaceSubClass == 0x01 || InterfaceDescriptor.InterfaceSubClass == 0x02 || InterfaceDescriptor.InterfaceSubClass == 0x03)) {
      st->BootServices->CloseProtocol(handles[i], &gEfiUsbIoProtocolGuid, imageHandle, NULL);
      st->BootServices->OpenProtocol(handles[i], &gEfiUsbIoProtocolGuid, (void**)&UsbIo, imageHandle, NULL, EFI_OPEN_PROTOCOL_EXCLUSIVE);
    } else {
      st->BootServices->CloseProtocol(handles[i], &gEfiUsbIoProtocolGuid, imageHandle, NULL);
      Print(L"Not an audio device, skipping\n");
      continue;
    }
    Print(L"OK\n");
    Print(L"Resetting port... ");
    status = UsbIo->UsbPortReset(UsbIo);
    if (EFI_ERROR(status))
      goto failed;

    Print(L"OK\n");
    Print(L"Reading interface descriptor... ");
    status = UsbIo->UsbGetInterfaceDescriptor(UsbIo, &InterfaceDescriptor);
    if (EFI_ERROR(status))
      goto failed;

    Print(L"Success\n");
    switch (InterfaceDescriptor.InterfaceSubClass) {
      case 0x01:
        Print(L"Found audio control device\n");
      break;
      case 0x02:
       Print(L"Found audio streaming device\n");
      break;
      case 0x03:
        Print(L"Found midi streaming device\n");
      break;
    }
    CHAR16* Manufacturer;
    CHAR16* Product;
    CHAR16* SerialNumber;
    CHAR16* InterfaceName;
    EFI_USB_DEVICE_DESCRIPTOR DevDesc = {0};
    status = UsbIo->UsbGetDeviceDescriptor (UsbIo, &DevDesc);
    if (EFI_ERROR(status))
      goto failed;
    if (InterfaceDescriptor.InterfaceSubClass == 0x02)
      UsbSetInterface (UsbIo, 1, 1, &UsbStatus);

    status = UsbIo->UsbGetInterfaceDescriptor(UsbIo, &InterfaceDescriptor);
    if (EFI_ERROR(status))
      goto failed;

    Print(L"Device has %d endpoints\n", InterfaceDescriptor.NumEndpoints);
    status = UsbIo->UsbGetStringDescriptor (UsbIo, 0x0409, InterfaceDescriptor.Interface, &InterfaceName);
    if (EFI_ERROR(status))
      InterfaceName = L"Unknown";
    status = UsbIo->UsbGetStringDescriptor (UsbIo, 0x0409, DevDesc.StrManufacturer, &Manufacturer);
    if (EFI_ERROR(status))
      Manufacturer = L"Unknown";
    status = UsbIo->UsbGetStringDescriptor(UsbIo, 0x0409, DevDesc.StrProduct, &Product);
    if (EFI_ERROR(status))
      Product = L"Unknown";
    status = UsbIo->UsbGetStringDescriptor(UsbIo, 0x0409, DevDesc.StrSerialNumber, &SerialNumber);
    if (EFI_ERROR(status))
      SerialNumber = L"Unknown";
    Print(L"Device found: %02x:%02x:%02x: %s %s (%s)\n", InterfaceDescriptor.InterfaceClass, InterfaceDescriptor.InterfaceSubClass, InterfaceDescriptor.InterfaceProtocol, Manufacturer, Product, SerialNumber);
    Print(L"Interface found: %s\n", InterfaceName);
    Print(L"Reading descriptor data\n");
    UINT8* Header = 0;
        status = st->BootServices->AllocatePool(EfiBootServicesData, 0x10000, (void*)&Header);
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
    Print(L"Configuring device... ");
    status = UsbSetConfiguration(UsbIo, Header[17] - 1, &UsbStatus);
    if (EFI_ERROR(status))
      goto failed;

    Print(L"Ok\n");
    if (InterfaceDescriptor.InterfaceSubClass == 0x02) {
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
  if (UsbIo)
    st->BootServices->CloseProtocol(handles[i], &gEfiUsbIoProtocolGuid, imageHandle, NULL);

  UsbIo = NULL;
  st->BootServices->FreePool(handles);
  handles = NULL;
  Print(L"%r (%04x)\n", status, UsbStatus);
  return EFI_ABORTED;
}

