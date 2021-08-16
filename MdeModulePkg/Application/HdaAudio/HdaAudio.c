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
#include <Library/MemoryAllocationLib.h>
#include <Protocol/PciIo.h>
#include <IndustryStandard/Pci.h>
#include "HdaAudio.h"

static EFI_HANDLE HdaFindController(IN EFI_HANDLE*, UINTN);
static HDA_CONTROLLER* HdaInstantiateAndReset(IN EFI_HANDLE);
static VOID HdaCheckForError(IN EFI_STATUS, IN OUT HDA_CONTROLLER*);
static VOID HdaEnumerateNodeIds(IN OUT HDA_CONTROLLER*);
static VOID HdaConfigureWakeEventsAndInterrupts(IN OUT HDA_CONTROLLER*);
static VOID HdaAllocateCorb(IN OUT HDA_CONTROLLER*);
static VOID HdaFreeCorb(IN OUT HDA_CONTROLLER*);
static VOID HdaAllocateRirb(IN OUT HDA_CONTROLLER*);
static VOID HdaFreeRirb(IN OUT HDA_CONTROLLER*);
static UINT32 HdaWriteCommand(IN OUT HDA_CONTROLLER*, IN UINT32, IN UINT32, IN UINT32, IN UINT32);

EFI_STATUS EFIAPI UefiMain(IN EFI_HANDLE imageHandle, IN EFI_SYSTEM_TABLE* st) {
   gImageHandle = imageHandle;
  gST = st;
  gBS = gST->BootServices;
  Print(L"Attempting to find PCI IO protocol\n");
  UINTN numHandles = 0;
  EFI_HANDLE* handles = NULL;
  EFI_STATUS status = gBS->LocateHandleBuffer(ByProtocol, &gEfiPciIoProtocolGuid, NULL, &numHandles, &handles);
  HdaCheckForError(status, NULL);
  // Find an HDA device
  EFI_HANDLE HdaHandle = HdaFindController(handles, numHandles);
  if (!HdaHandle) {
    Print(L"No HDA devices found\n");
    gBS->FreePool(handles);
    return EFI_OUT_OF_RESOURCES;
  }

  Print(L"Freeing handle buffer... ");
  status = gBS->FreePool(handles);
  HdaCheckForError(status, NULL);
  Print(L"Done\n");
  // Reset the controller
  Print(L"Resetting controller\n");
  HDA_CONTROLLER* Controller = HdaInstantiateAndReset(HdaHandle);
  // Wait for codecs to be ready
  gBS->Stall(600);
  // Figure out what codecs exist
  Print(L"Scanning for new nodes\n");
  HdaEnumerateNodeIds(Controller);
  // Disable wake events and interrupts
  Print(L"Disabling wake events and interrupts\n");
  HdaConfigureWakeEventsAndInterrupts(Controller);
  // Allocate and map the CORB and RIRB
  Print(L"Allocating corb\n");
  HdaAllocateCorb(Controller);
  Print(L"Allocating rirb\n");
  HdaAllocateRirb(Controller);
  // Get vendor ID and device ID
  UINT32 VendorAndDeviceId = HdaWriteCommand(Controller, 0, 0, HdaVerbGetParameter, HdaParamVendorDevId);
  // Vendor ID is stored in bits 16 .. 31
  // Device ID is stored in bits 0 .. 15
  Print(L"Vendor ID: %02x\n", BitFieldRead32(VendorAndDeviceId, 16, 31));
  Print(L"Device ID: %02x\n", BitFieldRead32(VendorAndDeviceId, 0, 15));
  Print(L"Freeing corb\n");
  HdaFreeCorb(Controller);
  Print(L"Freeing rirb\n");
  HdaFreeRirb(Controller);
  Print(L"Freeing controller buffer\n");
  HdaCheckForError(gBS->FreePool(Controller), NULL);
  return EFI_SUCCESS;
}

EFI_HANDLE HdaFindController(IN EFI_HANDLE* Handles, UINTN NumHandles) {
  // Loop through all the handles we got and find one that is an HDA controller
  for (UINTN i = 0; i < NumHandles; ++i) {
    Print(L"Checking handle %04x\n", Handles[i]);
    EFI_PCI_IO_PROTOCOL* PciIo = NULL;
    HdaCheckForError(gBS->OpenProtocol(Handles[i], &gEfiPciIoProtocolGuid, (VOID**)&PciIo, gImageHandle, NULL, EFI_OPEN_PROTOCOL_GET_PROTOCOL), NULL);
    PCI_TYPE00 Pci = {0};
    HdaCheckForError(PciIo->Pci.Read(PciIo, EfiPciIoWidthUint8, 0x0000, sizeof(Pci), &Pci), NULL);
    for (UINTN j = 0; j < sizeof(HDA_DEVICES)/sizeof(HDA_DEVICES[0]); ++j)
      if (Pci.Hdr.VendorId == HDA_DEVICES[j][0] && Pci.Hdr.DeviceId == HDA_DEVICES[j][1]) {
        HdaCheckForError(gBS->CloseProtocol(Handles[i], &gEfiPciIoProtocolGuid, gImageHandle, NULL), NULL);
        return Handles[i];
      }

  }
  return NULL;
}

///
/// Resets the HDA controller and allocates an HDA_CONTROLLER struct that is returned to the caller
///
static HDA_CONTROLLER* HdaInstantiateAndReset(IN EFI_HANDLE Handle) {
  Print(L"Allocating controller memory: %d bytes\n", sizeof(HDA_CONTROLLER));
  HDA_CONTROLLER* Controller = AllocateZeroPool(sizeof(HDA_CONTROLLER));
  if (!Controller)
    HdaCheckForError(EFI_OUT_OF_RESOURCES, NULL);

  Controller->Handle = Handle;
  Print(L"Exclusively opening handle %04x\n", Handle);
  HdaCheckForError(gBS->OpenProtocol(Handle, &gEfiPciIoProtocolGuid, (VOID**)&Controller->PciIo, gImageHandle, NULL, EFI_OPEN_PROTOCOL_EXCLUSIVE), NULL);
  HDA_GLOBAL_CONTROL Gctl = {0};
  Gctl.Bits.ControllerReset = 1;
  Print(L"Writing global control: %02x\n", Gctl.Raw);
  HdaCheckForError(Controller->PciIo->Mem.Write(Controller->PciIo, EfiPciIoWidthUint32, 0, HdaGloblCtl, 1, (VOID**)&Gctl.Raw), NULL);
  Print(L"Waiting for reset\n");
  do
    HdaCheckForError(Controller->PciIo->Mem.Read(Controller->PciIo, EfiPciIoWidthUint32, 0, HdaGloblCtl, 1, (VOID**)&Gctl.Raw), NULL);
  while (!Gctl.Bits.ControllerReset);

  return Controller;
}

static VOID HdaEnumerateNodeIds(IN OUT HDA_CONTROLLER* Controller) {
  // Go through each node ID in STATESTS and see if its bit is set.
  // If that bit is set, set it in our struct as well.
  UINT16 StateStatus = 0;
  HdaCheckForError(Controller->PciIo->Mem.Read(Controller->PciIo, EfiPciIoWidthUint16, 0, HdaStateStatus, 1, (VOID**)&StateStatus), Controller);
  for (UINT16 i = 0; i < 15; ++i) {
    Print(L"Checking STATESTS[%d]\n", i);
    if (StateStatus & (1 << i)) {
      Controller->DetectedNodes |= (1 << i);
      StateStatus |= (1 << i);
      Print(L"Bit set; storing in Controller->DetectedNodes\n");
    }
  }
  HdaCheckForError(Controller->PciIo->Mem.Write(Controller->PciIo, EfiPciIoWidthUint16, 0, HdaStateStatus, 1, (VOID**)&StateStatus), Controller);
  }

static VOID HdaConfigureWakeEventsAndInterrupts(IN OUT HDA_CONTROLLER* Controller) {
  UINT16 WakeEvents = 0;
  UINT32 Interrupts = 0;
  // Disable all wake events
  Print(L"Disabling wakeen\n");
  HdaCheckForError(Controller->PciIo->Mem.Write(Controller->PciIo, EfiPciIoWidthUint16, 0, HdaWakeEn, 1, (VOID**)&WakeEvents), Controller);
  // Disable interrupts
  Print(L"Disabling intctl\n");
  HdaCheckForError(Controller->PciIo->Mem.Write(Controller->PciIo, EfiPciIoWidthUint32, 0, HdaIntCtl, 1, (VOID**)&Interrupts), Controller);
}

static VOID HdaAllocateCorb(IN OUT HDA_CONTROLLER* Controller) {
  // First, reset the read pointer
  HDA_CORB_CONTROL CorbCtl = {0};
  CorbCtl.Bits.MemErrIntEnable = 0;
  CorbCtl.Bits.Run = 0;
  Print(L"Reset corbctl: %x\n", CorbCtl.Raw);
  HdaCheckForError(Controller->PciIo->Mem.Write(Controller->PciIo, EfiPciIoWidthUint8, 0, HdaCorbCtl, 1, (VOID**)&CorbCtl.Raw), Controller);
  HDA_CORB_RDPTR ReadPointer = {0};
  ReadPointer.Bits.Reset = 1;
  Print(L"Reset corbrp: %x\n", ReadPointer.Raw);
  HdaCheckForError(Controller->PciIo->Mem.Write(Controller->PciIo, EfiPciIoWidthUint16, 0, HdaCorbReadPtr, 1, (VOID**)&ReadPointer.Raw), Controller);
  Print(L"Waiting\n");
  do
    HdaCheckForError(Controller->PciIo->Mem.Read(Controller->PciIo, EfiPciIoWidthUint16, 0, HdaCorbReadPtr, 1, (VOID**)&ReadPointer.Raw), Controller);
  while (!ReadPointer.Bits.Reset);

  ReadPointer.Bits.Reset = 0;
  Print(L"Finalize reset corbrp: %x\n", ReadPointer.Raw);
  HdaCheckForError(Controller->PciIo->Mem.Write(Controller->PciIo, EfiPciIoWidthUint16, 0, HdaCorbReadPtr, 1, (VOID**)&ReadPointer.Raw), Controller);
  // Second, allocate our CORB
  Print(L"Alloc corb: Addr %08x, 1024 bytes, %d pages\n", (UINTN)&Controller->Corb, EFI_SIZE_TO_PAGES(1024));
  HdaCheckForError(Controller->PciIo->AllocateBuffer(Controller->PciIo, AllocateAnyPages, EfiBootServicesData, EFI_SIZE_TO_PAGES(1024), (VOID*)&Controller->Corb, 0), Controller);
  // Now map it
  Controller->CorbBytes = 1024;
  HdaCheckForError(Controller->PciIo->Map(Controller->PciIo, EfiPciIoOperationBusMasterCommonBuffer, (VOID*)&Controller->Corb, &Controller->CorbBytes, &Controller->CorbPhysAddress, (VOID**)&Controller->CorbMapping), Controller);
  ASSERT(Controller->CorbBytes == 1024);
  Print(L"Map corb: vaddr %08x, paddr %08x, size %d, %d pages, mapping at %08x\n", (UINTN)&Controller->Corb, Controller->CorbPhysAddress, Controller->CorbBytes, EFI_SIZE_TO_PAGES(Controller->CorbBytes), (UINTN)&Controller->CorbMapping);
  // Set CORB address
  // This depends on the address being 64-bits wide. 128-bit addresses are not supported. 32-bit addresses may work.
  UINT32 CorbLo = (UINT32)((Controller->CorbPhysAddress & 0xFFFFFFFF00000000ULL) >> 32);
  UINT32 CorbHi = (UINT32)(Controller->CorbPhysAddress & 0xFFFFFFFFULL);
  Print(L"Set corblbase: %04x\n", CorbLo);
  HdaCheckForError(Controller->PciIo->Mem.Write(Controller->PciIo, EfiPciIoWidthUint32, 0, HdaCorbLo, 1, (VOID**)&CorbLo), Controller);
  Print(L"Set corbubase: %04x\n", CorbHi);
  HdaCheckForError(Controller->PciIo->Mem.Write(Controller->PciIo, EfiPciIoWidthUint32, 0, HdaCorbHi, 1, (VOID**)&CorbHi), Controller);
  // Set CORB size
  HDA_CORB_SIZE CorbSz = {0};
  HdaCheckForError(Controller->PciIo->Mem.Read(Controller->PciIo, EfiPciIoWidthUint8, 0, HdaCorbSize, 1, (VOID**)&CorbSz.Raw), Controller);
  CorbSz.Bits.Size = 2;
  Print(L"Set corbsize: %x\n", CorbSz.Raw);
  HdaCheckForError(Controller->PciIo->Mem.Write(Controller->PciIo, EfiPciIoWidthUint8, 0, HdaCorbSize, 1, (VOID**)&CorbSz.Raw), Controller);
  // Set the CORB to running
  CorbCtl.Bits.Run = 1;
  Print(L"Set corbctl: %x\n", CorbCtl);
  HdaCheckForError(Controller->PciIo->Mem.Write(Controller->PciIo, EfiPciIoWidthUint8, 0, HdaCorbCtl, 1, (VOID**)&CorbCtl.Raw), Controller);
}

static VOID HdaAllocateRirb(IN OUT HDA_CONTROLLER* Controller) {
  // First, reset the pointers
  Print(L"Set rirbrp: reset\n");
  Controller->RirbReadPtr = 0;
  HDA_RIRB_CONTROL RirbCtl = {0};
  RirbCtl.Bits.ResponseInterrupt = 0;
  RirbCtl.Bits.DmaRun = 0;
  RirbCtl.Bits.OverrunInterrupt = 0;
  Print(L"Set rirbctl: %x\n", RirbCtl.Raw);
  HdaCheckForError(Controller->PciIo->Mem.Write(Controller->PciIo, EfiPciIoWidthUint8, 0, HdaRirbCtl, 1, (VOID**)&RirbCtl.Raw), Controller);
  HDA_RIRB_WRPTR WritePointer = {0};
  WritePointer.Bits.Reset = 1;
  Print(L"Set rirbwp: %x\n", WritePointer.Raw);
  HdaCheckForError(Controller->PciIo->Mem.Write(Controller->PciIo, EfiPciIoWidthUint16, 0, HdaRirbWritePtr, 1, (VOID**)&WritePointer.Raw), Controller);
  // Next, allocate our RIRB
  Print(L"Alloc rirb: vaddr %08x, %d bytes, %d pages\n", (UINTN)&Controller->Rirb, 2048, EFI_SIZE_TO_PAGES(2048));
  HdaCheckForError(Controller->PciIo->AllocateBuffer(Controller->PciIo, AllocateAnyPages, EfiBootServicesData, EFI_SIZE_TO_PAGES(2048), (VOID*)&Controller->Rirb, 0), Controller);
  // Now map it
  Controller->RirbBytes = 2048;
  HdaCheckForError(Controller->PciIo->Map(Controller->PciIo, EfiPciIoOperationBusMasterCommonBuffer, (VOID*)&Controller->Rirb, &Controller->RirbBytes, &Controller->RirbPhysAddress, (VOID**)&Controller->RirbMapping), Controller);
  ASSERT(Controller->RirbBytes == 2048);
  Print(L"Map rirb: vaddr %08x, paddr %08x, size %d, %d pages, mapping at %08x\n", (UINTN)&Controller->Rirb, Controller->RirbPhysAddress, Controller->RirbBytes, EFI_SIZE_TO_PAGES(Controller->RirbBytes), (UINTN)&Controller->RirbMapping);
  // Set RIRB address
  // This depends on the address being 64-bits wide. 128-bit addresses are not supported. 32-bit addresses may work.
  UINT32 RirbLo = (UINT32)((Controller->RirbPhysAddress & 0xFFFFFFFF00000000ULL) >> 32);
  UINT32 RirbHi = (UINT32)(Controller->RirbPhysAddress & 0xFFFFFFFFULL);
  Print(L"Set rirblbase: %04x\n", RirbLo);
  HdaCheckForError(Controller->PciIo->Mem.Write(Controller->PciIo, EfiPciIoWidthUint32, 0, HdaRirbLo, 1, (VOID**)&RirbLo), Controller);
  Print(L"Set rirbubase: %04x\n", RirbHi);
  HdaCheckForError(Controller->PciIo->Mem.Write(Controller->PciIo, EfiPciIoWidthUint32, 0, HdaRirbHi, 1, (VOID**)&RirbHi), Controller);
  // Set RIRB size
  // Set CORB size
  HDA_RIRB_SIZE RirbSz = {0};
  HdaCheckForError(Controller->PciIo->Mem.Read(Controller->PciIo, EfiPciIoWidthUint8, 0, HdaRirbSize, 1, (VOID**)&RirbSz.Raw), Controller);
  RirbSz.Bits.Size = 2;
  Print(L"Set rirbsize: %x\n", RirbSz.Raw);
  HdaCheckForError(Controller->PciIo->Mem.Write(Controller->PciIo, EfiPciIoWidthUint8, 0, HdaRirbSize, 1, (VOID**)&RirbSz.Raw), Controller);
  // Set the RIRB to running
  RirbCtl.Bits.DmaRun = 1;
  Print(L"Set rirbctl: %x\n", RirbCtl.Raw);
  HdaCheckForError(Controller->PciIo->Mem.Write(Controller->PciIo, EfiPciIoWidthUint8, 0, HdaRirbCtl, 1, (VOID**)&RirbCtl.Raw), Controller);
}

static VOID HdaFreeCorb(IN OUT HDA_CONTROLLER* Controller) {
  HDA_CORB_CONTROL CorbCtl = {0};
  CorbCtl.Bits.Run = 0;
  Print(L"Set corbctl: %x\n", CorbCtl.Raw);
  HdaCheckForError(Controller->PciIo->Mem.Write(Controller->PciIo, EfiPciIoWidthUint8, 0, HdaCorbCtl, 1, (VOID**)&CorbCtl.Raw), Controller);
  HDA_CORB_RDPTR ReadPointer = {0};
  ReadPointer.Bits.Reset = 1;
  Print(L"Set corbrp: %x\n", ReadPointer.Raw);
  HdaCheckForError(Controller->PciIo->Mem.Write(Controller->PciIo, EfiPciIoWidthUint16, 0, HdaCorbReadPtr, 1, (VOID**)&ReadPointer.Raw), Controller);
  Print(L"Waiting\n");
  do
    HdaCheckForError(Controller->PciIo->Mem.Read(Controller->PciIo, EfiPciIoWidthUint16, 0, HdaCorbReadPtr, 1, (VOID**)&ReadPointer.Raw), Controller);
  while (!ReadPointer.Bits.Reset);

  ReadPointer.Bits.Reset = 0;
  Print(L"Set corbrp: %x\n", ReadPointer.Raw);
  HdaCheckForError(Controller->PciIo->Mem.Write(Controller->PciIo, EfiPciIoWidthUint16, 0, HdaCorbReadPtr, 1, (VOID**)&ReadPointer.Raw), Controller);
  Print(L"Unmap corb: vaddr %08x\n", (UINTN)&Controller->CorbMapping);
  HdaCheckForError(Controller->PciIo->Unmap(Controller->PciIo, Controller->CorbMapping), Controller);
  Print(L"Free corb: vaddr %08x, size %d, %d pages\n", (UINTN)&Controller->Corb, Controller->CorbBytes, EFI_SIZE_TO_PAGES(Controller->CorbBytes));
  HdaCheckForError(Controller->PciIo->FreeBuffer(Controller->PciIo, EFI_SIZE_TO_PAGES(Controller->CorbBytes), (VOID*)Controller->Corb), Controller);
}

static VOID HdaFreeRirb(IN OUT HDA_CONTROLLER* Controller) {
  Print(L"Set rirbrp: %x\n", 0);
  Controller->RirbReadPtr = 0;
  HDA_RIRB_CONTROL RirbCtl = {0};
  RirbCtl.Bits.DmaRun = 0;
  Print(L"Set rirbctl: %x\n", RirbCtl.Raw);
  HdaCheckForError(Controller->PciIo->Mem.Write(Controller->PciIo, EfiPciIoWidthUint8, 0, HdaRirbCtl, 1, (VOID**)&RirbCtl.Raw), Controller);
  HDA_RIRB_WRPTR WritePointer = {0};
  WritePointer.Bits.Reset = 1;
  Print(L"Set rirbwp: %x\n", WritePointer.Raw);
  HdaCheckForError(Controller->PciIo->Mem.Write(Controller->PciIo, EfiPciIoWidthUint16, 0, HdaRirbWritePtr, 1, (VOID**)&WritePointer.Raw), Controller);
  Print(L"Unmap rirb: vaddr %08x\n", (UINTN)&Controller->RirbMapping);
  HdaCheckForError(Controller->PciIo->Unmap(Controller->PciIo, Controller->RirbMapping), Controller);
  Print(L"Free rirb: vaddr %08x, size %d, %d pages\n", (UINTN)&Controller->Rirb, Controller->RirbBytes, EFI_SIZE_TO_PAGES(Controller->RirbBytes));
  HdaCheckForError(Controller->PciIo->FreeBuffer(Controller->PciIo, EFI_SIZE_TO_PAGES(Controller->RirbBytes), (VOID*)Controller->Rirb), Controller);
}

static UINT32 HdaWriteCommand(IN OUT HDA_CONTROLLER* Controller, IN UINT32 CodecAddress, IN UINT32 NodeId, IN UINT32 Command, IN UINT32 Data) {
  HDA_CORB_ENTRY Entry = {0};
  Entry.Bits.CodecAddress = CodecAddress;
  Entry.Bits.NodeId = NodeId;
  Entry.Bits.Command = Command;
  Entry.Bits.Data = Data;
  Print(L"Manifest corb entry: %04x\n", Entry.Raw);
  Controller->Corb[Controller->CorbWritePtr] = Entry.Raw;
      Print(L"Set corbwp: %x\n", Controller->CorbWritePtr);
    Controller->CorbWritePtr += 1;
  HDA_CORB_WRPTR WritePointer = {0};
  WritePointer.Bits.WritePtr = Controller->CorbWritePtr;
  HdaCheckForError(Controller->PciIo->Mem.Write(Controller->PciIo, EfiPciIoWidthUint16, 0, HdaCorbWritePtr, 1, (VOID**)&WritePointer.Raw), Controller);
  HDA_CORB_RDPTR ReadPointer = {0};
  Print(L"Waiting\n");
  do {
    HdaCheckForError(Controller->PciIo->Mem.Read(Controller->PciIo, EfiPciIoWidthUint16, 0, HdaCorbWritePtr, 1, (VOID**)&WritePointer.Raw), Controller);
    HdaCheckForError(Controller->PciIo->Mem.Read(Controller->PciIo, EfiPciIoWidthUint16, 0, HdaCorbReadPtr, 1, (VOID**)&ReadPointer.Raw), Controller);
  } while (ReadPointer.Raw == WritePointer.Raw);
  Print(L"Read resp: %04x\n", Controller->Rirb[Controller->RirbReadPtr]);
  UINT32 Resp = Controller->Rirb[Controller->RirbReadPtr];
    Controller->RirbReadPtr++;
  Print(L"Set rirbrp: %x\n", Controller->RirbReadPtr);
  return Resp;
}

static VOID HdaCheckForError(IN EFI_STATUS Status, IN OUT HDA_CONTROLLER* Controller) {
  if (EFI_ERROR(Status)) {
    Print(L"Failed operation: %r\n", Status);
    gBS->Exit(gImageHandle, Status, 0, NULL);
    }
}


