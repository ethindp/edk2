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
#include <Protocol/PciIo.h>
#include <IndustryStandard/Pci.h>
#include "HdaAudio.h"
//#include "Sine.h"

static EFI_HANDLE HdaFindController(IN EFI_HANDLE*, UINTN);
static HDA_CONTROLLER* HdaInstantiateAndReset(IN EFI_HANDLE);
static VOID HdaCheckForError(const IN EFI_STATUS);
static VOID HdaEnumerateCodecs(IN OUT HDA_CONTROLLER*);
static VOID HdaConfigureWakeEvents(IN OUT HDA_CONTROLLER*);
static VOID HdaAllocateCorb(IN OUT HDA_CONTROLLER*);
static VOID HdaFreeCorb(IN OUT HDA_CONTROLLER*);
static VOID HdaAllocateRirb(IN OUT HDA_CONTROLLER*);
static VOID HdaFreeRirb(IN OUT HDA_CONTROLLER*);
static VOID HdaWriteCommand(IN OUT HDA_CONTROLLER*, IN UINT32, IN UINT32, IN UINT32, IN UINT32, OUT HDA_RIRB_RESPONSE*);

EFI_STATUS EFIAPI UefiMain(IN EFI_HANDLE imageHandle, IN EFI_SYSTEM_TABLE* st) {
   gImageHandle = imageHandle;
  gST = st;
  gBS = ggBS;
  Print(L"Attempting to find PCI IO protocol\n");
  UINTN numHandles = 0;
  EFI_HANDLE* handles = NULL;
  EFI_STATUS status = gBS->LocateHandleBuffer(ByProtocol, &gEfiPciIoProtocolGuid, NULL, &numHandles, &handles);
  HdaCheckForError(status);
  EFI_HANDLE HdaHandle = HdaFindDevice(handles, numHandles);
  if (!HdaHandle) {
    Print(L"No HDA devices found\n");
    gBS->FreePool(handles);
    return EFI_OUT_OF_RESOURCES;
  }
  Print(L"Freeing handle buffer... ");
  status = gBS->FreePool(handles);
  HdaCheckForError(status);
  Print(L"Done\n");
  HDA_CONTROLLER* Controller = HdaInstantiateAndReset(HdaHandle);
  gBS->Stall(600);
  HdaEnumerateCodecs(Controller);
  HdaConfigureWakeEventsAndInterrupts(Controller);
  HdaAllocateCorb(Controller);
  HdaAllocateRirb(Controller);
  
    Print(L"Closing protocol... ");
    status = gBS->CloseProtocol(handles[i], &gEfiPciIoProtocolGuid, imageHandle, NULL);
    if (EFI_ERROR(status))
      goto failed;
    Print(L"Ok\n");
    PciIo = NULL;
    Print(L"Done\n");
  }
  Print(L"Freeing handle buffer... ");
  status = gBS->FreePool(handles);
  if (EFI_ERROR(status))
    goto failed;
  Print(L"Done\n");
  return EFI_SUCCESS;
  failed:
  if (PciIo)
    gBS->CloseProtocol(handles[i], &gEfiPciIoProtocolGuid, imageHandle, NULL);
  PciIo = NULL;
  gBS->FreePool(handles);
  handles = NULL;
  Print(L"%r\n", status);
  return EFI_ABORTED;
}

EFI_HANDLE HdaFindController(IN EFI_HANDLE* Handles, UINTN NumHandles) {
  for (UINTN i = 0; i < NumHandles; ++i) {
    EFI_PCI_IO_PROTOCOL* PciIo = NULL;
    HdaCheckForError(gBS->OpenProtocol(Handles[i], &gEfiPciIoProtocolGuid, (VOID**)&PciIo, gImageHandle, NULL, EFI_OPEN_PROTOCOL_GET_PROTOCOL));
    PCI_TYPE00 Pci = {0};
    HdaCheckForError(PciIo->Pci.Read(PciIo, EfiPciIoWidthUint8, 0x0000, sizeof(Pci), &Pci));
    if (IS_CLASS2(&Pci, 0x04, 0x03) || IS_CLASS3(&Pci, 0x04, 0x03, 0x01))
      for (UINTN j = 0; j < sizeof(HDA_DEVICES)/sizeof(HDA_DEVICES[0]); ++j)
        if (Pci.Hdr.VendorId == HDA_DEVICES[j][0] && Pci.Hdr.DeviceId == HDA_DEVICES[j][1]) {
          HdaCheckForError(gBS->CloseProtocol(Handles[i], &gEfiPciIoProtocolGuid, gImageHandle, NULL));
          return Handles[i];
        }
    else {
      Print(L"No HDA devices found; aborting\n");
      HdaCheckForError(EFI_ABORTED);
    }
  }

///
/// Resets the HDA controller and allocates an HDA_CONTROLLER struct that is returned to the caller
///
static HDA_CONTROLLER* HdaInstantiateAndReset(IN EFI_HANDLE Handle) {
  HDA_CONTROLLER* Controller = NULL;
  HdaCheckForError(gBS->AllocatePool(EfiBootServicesData, sizeof(HDA_CONTROLLER), (VOID**)&Controller));
  ZeroMem(Controller);
  Controller->Handle = Handle;
  HdaCheckForError(gBS->OpenProtocol(Handles[i], &gEfiPciIoProtocolGuid, (VOID**)&Controller->PciIo, gImageHandle, NULL, EFI_OPEN_PROTOCOL_EXCLUSIVE));
  HDA_GLOBAL_CONTROL Gctl = {0};
  Gctl.Bits.ControllerReset = 1;
  HdaCheckForError(Controller->PciIo->Mem.Write(PciIo, EfiPciIoWidthUint32, 0, HdaGloblCtl, 1, (VOID**)&Controller.Raw));
  do
    HdaCheckForError(Controller->PciIo->Mem.Read(PciIo, EfiPciIoWidthUint32, 0, HdaGloblCtl, 1, (VOID**)&Controller.Raw));
  while (!Gctl.Bits.ControllerReset);
  return Controller;
}


