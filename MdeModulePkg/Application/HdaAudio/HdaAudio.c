/* HDAAudio.c

Contains the main code for the HDA audio EFI application.

Copyright (C) 2021 Ethin Probst.

SPDX-License-Identifier: BSD-2-Clause-Patent

*/

#include <Uefi.h>
#include <Library/BaseLib.h>
#include <Library/UefiApplicationEntryPoint.h>
#include <Library/DebugLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/TimerLib.h>
#include <Protocol/PciIo.h>
#include <IndustryStandard/Pci.h>
#include "HdaAudio.h"
//#include "Sine.h"

EFI_STATUS EFIAPI UefiMain(IN EFI_HANDLE imageHandle, IN EFI_SYSTEM_TABLE* st) {
  Print(L"Attempting to find PCI IO protocol\n");
  UINTN numHandles = 0;
  UINTN i = 0;
  EFI_HANDLE* handles = NULL;
  EFI_PCI_IO_PROTOCOL* PciIo = NULL;
  EFI_STATUS status = st->BootServices->LocateHandleBuffer(ByProtocol, &gEfiPciIoProtocolGuid, NULL, &numHandles, &handles);
  if (EFI_ERROR(status)) {
    Print(L"Cannot find any handles for PCI devices, reason: %r\n", status);
    return EFI_ABORTED;
  }
  Print(L"Found %d PCI devices; enumerating\n", numHandles);
  for (; i < numHandles; ++i) {
    Print(L"Trying to open handle %d (%x)... ", i, handles[i]);
    status = st->BootServices->OpenProtocol(handles[i], &gEfiPciIoProtocolGuid, (void**)&PciIo, imageHandle, NULL, EFI_OPEN_PROTOCOL_GET_PROTOCOL);
    if (EFI_ERROR(status)) {
      Print(L"%r, skipping\n", status);
      continue;
    }
    PCI_TYPE00 Pci = {0};
    status = PciIo->Pci.Read(PciIo, EfiPciIoWidthUint8, 0, sizeof (Pci.Hdr), &Pci);
    if (EFI_ERROR(status))
      goto failed;
    if (IS_CLASS2(&Pci, 0x04, 0x03) || (Pci.Hdr.VendorId == 0x8086 && (Pci.Hdr.DeviceId == 0x2668 || Pci.Hdr.DeviceId == 0x27D8)) || (Pci.Hdr.VendorId == 0x1002 && Pci.Hdr.DeviceId == 0x4383)) {
      status = st->BootServices->CloseProtocol(handles[i], &gEfiPciIoProtocolGuid, imageHandle, NULL);
      if (EFI_ERROR(status))
        goto failed;
      status = st->BootServices->OpenProtocol(handles[i], &gEfiPciIoProtocolGuid, (void**)&PciIo, imageHandle, NULL, EFI_OPEN_PROTOCOL_EXCLUSIVE);
      if (EFI_ERROR(status))
        goto failed;
    } else {
      status = st->BootServices->CloseProtocol(handles[i], &gEfiPciIoProtocolGuid, imageHandle, NULL);
      if (EFI_ERROR(status))
        goto failed;
      Print(L"Not an audio device, skipping\n");
      continue;
    }
    Print(L"OK\n");
    Print(L"Resetting controller... ");
    HDA_GLOBAL_CONTROL Control;
    Control.Bits.ControllerReset = 1;
    status = PciIo->Mem.Write(PciIo, EfiPciIoWidthUint32, 0, HdaGloblCtl, sizeof(HDA_GLOBAL_CONTROL), (void*)&Control);
    if (EFI_ERROR(status))
      goto failed;
    while (1) {
      status = PciIo->Mem.Read(PciIo, EfiPciIoWidthUint32, 0, HdaGloblCtl, sizeof(HDA_GLOBAL_CONTROL), (void*)&Control);
      if (EFI_ERROR(status))
        goto failed;
      if (Control.Bits.ControllerReset)
        break;
    }
    Print(L"Ok\n");
    st->BootServices->Stall(600);
    Print(L"Detecting node status changes\n");
    UINT16 Statests;
    status = PciIo->Mem.Read(PciIo, EfiPciIoWidthUint16, 0, HdaStateStatus, 1, (void*)&Statests);
    if (EFI_ERROR(status))
      goto failed;
    for (UINT16 j = 0; j < 15; ++j)
      if (Statests & (1 << j)) {
        Print(L"Detected state change for node %02x\n", j);
        Statests |= (1 << j);
      }
    status = PciIo->Mem.Write(PciIo, EfiPciIoWidthUint16, 0, HdaStateStatus, 1, (void*)&Statests);
    if (EFI_ERROR(status))
      goto failed;
    Print(L"Configuring wake event bits\n");
    UINT16 Wakeen = 0x00;
    status = PciIo->Mem.Write(PciIo, EfiPciIoWidthUint16, 0, HdaWakeEn, 1, (void*)&Wakeen);
    if (EFI_ERROR(status))
      goto failed;
    Print(L"Configuring interrupt control\n");
    UINT32 Intctl = 0x0000;
    status = PciIo->Mem.Write(PciIo, EfiPciIoWidthUint32, 0, HdaIntCtl, 1, (void*)&Intctl);
    if (EFI_ERROR(status))
      goto failed;
    Print(L"Allocating CORB and RIRB\n");
    HDA_CORB_SIZE CorbSize = {0};
    HDA_RIRB_SIZE RirbSize = {0};
    status = PciIo->Mem.Read(PciIo, EfiPciIoWidthUint8, 0, HdaCorbSize, 1, (void*)&CorbSize);
    if (EFI_ERROR(status))
      goto failed;
    status = PciIo->Mem.Read(PciIo, EfiPciIoWidthUint8, 0, HdaRirbSize, 1, (void*)&RirbSize);
    if (EFI_ERROR(status))
      goto failed;
    UINTN CorbBytes = 0;
    UINTN RirbBytes = 0;
    void* CorbMapping = NULL;
    void* RirbMapping = NULL;
    EFI_PHYSICAL_ADDRESS CorbPhysAddress;
    EFI_PHYSICAL_ADDRESS RirbPhysAddress;
    HDA_CORB_ENTRY* Corb = NULL;
    HDA_RIRB_RESPONSE* Rirb = NULL;
    if ((CorbSize.Bits.Cap & (1 << 3)))
      CorbBytes = 1024;
    else if ((CorbSize.Bits.Cap & (1 << 2)))
      CorbBytes = 64;
    else if ((CorbSize.Bits.Cap & (1 << 1)))
      CorbBytes = 8;
    if ((RirbSize.Bits.Cap & (1 << 3)))
      RirbBytes = 2048;
    else if ((RirbSize.Bits.Cap & (1 << 2)))
      RirbBytes = 128;
    else if ((RirbSize.Bits.Cap & (1 << 1)))
      RirbBytes = 16;
    Print(L"Allocating corb\n");
    status = PciIo->AllocateBuffer(PciIo, AllocateAnyPages, EfiBootServicesData, EFI_SIZE_TO_PAGES (CorbBytes), (void**)&Corb, 0);
    if (EFI_ERROR(status))
      goto failed;
    Print(L"Mapping Corb\n");
    status = PciIo->Map(PciIo, EfiPciIoOperationBusMasterCommonBuffer, Corb, &CorbBytes, &CorbPhysAddress, &CorbMapping);
    if (EFI_ERROR(status))
      goto failed;
    Print(L"Allocating Rirb\n");
    status = PciIo->AllocateBuffer(PciIo, AllocateAnyPages, EfiBootServicesData, EFI_SIZE_TO_PAGES (RirbBytes), (void**)&Rirb, 0);
    if (EFI_ERROR(status))
      goto failed;
    Print(L"Mapping Rirb\n");
    status = PciIo->Map(PciIo, EfiPciIoOperationBusMasterCommonBuffer, Rirb, &RirbBytes, &RirbPhysAddress, &RirbMapping);
    if (EFI_ERROR(status))
      goto failed;
    Print(L"Closing protocol... ");
    status = st->BootServices->CloseProtocol(handles[i], &gEfiPciIoProtocolGuid, imageHandle, NULL);
    if (EFI_ERROR(status))
      goto failed;
    Print(L"Ok\n");
    PciIo = NULL;
    Print(L"Done\n");
  }
  Print(L"Freeing handle buffer... ");
  status = st->BootServices->FreePool(handles);
  if (EFI_ERROR(status))
    goto failed;
  Print(L"Done\n");
  return EFI_SUCCESS;
  failed:
  if (PciIo)
    st->BootServices->CloseProtocol(handles[i], &gEfiPciIoProtocolGuid, imageHandle, NULL);
  PciIo = NULL;
  st->BootServices->FreePool(handles);
  handles = NULL;
  Print(L"%r\n", status);
  return EFI_ABORTED;
}

