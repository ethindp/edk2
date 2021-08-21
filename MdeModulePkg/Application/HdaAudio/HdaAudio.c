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
#include "Sine.h"

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
static VOID HdaAllocateStreams(IN OUT HDA_CONTROLLER*);
static VOID HdaFreeStreams(IN OUT HDA_CONTROLLER*);
static UINT64 HdaCalcIssOffset(IN UINT64 StreamIndex, IN UINT64 RegisterOffset);
static UINT64 HdaCalcOssOffset(IN HDA_CONTROLLER* Controller, IN UINT64 StreamIndex, IN UINT64 RegisterOffset);
static UINT64 HdaCalcBssOffset(IN HDA_CONTROLLER* Controller, IN UINT64 StreamIndex, IN UINT64 RegisterOffset);
static VOID HdaCreateNodeTree(IN OUT HDA_CONTROLLER*);

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
  Print(L"Allocating streams\n");
  HdaAllocateStreams(Controller);
  // Enumerate through all codecs and initialize them
  HdaCreateNodeTree(Controller);
  // Initialize BDLs
  Controller->OutputBdlBytes[0] = sizeof(HDA_BUFFER_DESCRIPTOR_LIST_ENTRY)*256;
  Controller->OutputBdlBytes[1] = sizeof(HDA_BUFFER_DESCRIPTOR_LIST_ENTRY)*256;
  HdaCheckForError(Controller->PciIo->Map(Controller->PciIo, EfiPciIoOperationBusMasterCommonBuffer, (VOID*)&Controller->OutputBdl[0], &Controller->OutputBdlBytes[0], &Controller->OutputBdlPhysAddress[0], (VOID**)&Controller->OutputBdlMapping[0]), Controller);
  HdaCheckForError(Controller->PciIo->Map(Controller->PciIo, EfiPciIoOperationBusMasterCommonBuffer, (VOID*)&Controller->OutputBdl[1], &Controller->OutputBdlBytes[1], &Controller->OutputBdlPhysAddress[1], (VOID**)&Controller->OutputBdlMapping[1]), Controller);
  ASSERT(Controller->OutputBdlBytes[0] == sizeof(HDA_BUFFER_DESCRIPTOR_LIST_ENTRY)*256);
  ASSERT(Controller->OutputBdlBytes[1] == sizeof(HDA_BUFFER_DESCRIPTOR_LIST_ENTRY)*256);
  Controller->OutputBdl[0].Address = (UINT64)&SINE_SAMPLES;
  Controller->OutputBdl[0].Length = (sizeof(SINE_SAMPLES)/sizeof(SINE_SAMPLES[0]))/2;
  Controller->OutputBdl[0].Config = 0;
  Controller->OutputBdl[1].Address = Controller->OutputBdl[0].Address + Controller->OutputBdl[0].Length;
  Controller->OutputBdl[1].Length = Controller->OutputBdl[0].Length;
  Controller->OutputBdl[1].Config = 0;
  HdaCheckForError(Controller->PciIo->Unmap(Controller->PciIo, Controller->OutputBdlMapping[0]), Controller);
  HdaCheckForError(Controller->PciIo->Unmap(Controller->PciIo, Controller->OutputBdlMapping[1]), Controller);
  UINT32 LastValidIndex =2;
  HdaCheckForError(Controller->PciIo->Mem.Write(Controller->PciIo, EfiPciIoWidthUint32, 0, HdaStreamLastValidIndex, 1, (VOID*)&LastValidIndex), Controller);
  UINT32 BdlSize = sizeof(SINE_SAMPLES)/sizeof(SINE_SAMPLES[0]);
  HdaCheckForError(Controller->PciIo->Mem.Write(Controller->PciIo, EfiPciIoWidthUint32, 0, HdaCalcOssOffset(Controller, 0, HdaStreamCyclicBufferLength), 1, (VOID*)&BdlSize), Controller);
  HdaWriteCommand(Controller, 0, 0x10, HdaVerbSetStreamChannel, 0x10);
  UINT32 StreamControlData = 0x100000;
  HdaCheckForError(Controller->PciIo->Mem.Write(Controller->PciIo, EfiPciIoWidthUint32, 0, HdaCalcOssOffset(Controller, 0, HdaStreamControl), 1, (VOID*)&StreamControlData), Controller);
  UINT16 FifoSize = 4;
  HdaCheckForError(Controller->PciIo->Mem.Write(Controller->PciIo, EfiPciIoWidthUint16, 0, HdaCalcOssOffset(Controller, 0, HdaStreamFifoSize), 1, (VOID*)&FifoSize), Controller);
  HDA_STREAM_FORMAT_DESCRIPTOR Format = {0};
  Format.Pcm.Channels = 1;
  Format.Pcm.BitsPerSample = 1;
  HdaCheckForError(Controller->PciIo->Mem.Write(Controller->PciIo, EfiPciIoWidthUint16, 0, HdaCalcOssOffset(Controller, 0, HdaStreamFormat), 1, (VOID*)&Format.Raw), Controller);
  // Where do all these magic numbers come from?
  HdaWriteCommand(Controller, 0, 0x10, HdaVerbSetStreamFormat, Format.Raw);
  HdaWriteCommand(Controller, 0, 0x11, HdaVerbSetStreamFormat, Format.Raw);
  StreamControlData = 0x100002;
  HdaCheckForError(Controller->PciIo->Mem.Write(Controller->PciIo, EfiPciIoWidthUint32, 0, HdaCalcOssOffset(Controller, 0, HdaStreamControl), 1, (VOID*)&StreamControlData), Controller);
  Print(L"Freeing streams\n");
  HdaFreeStreams(Controller);
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
  // Set CORB address
  // This depends on the address being 64-bits wide. 128-bit addresses are not supported. 32-bit addresses may work.
  UINT32 CorbLo = (UINT32)BitFieldRead64((UINT64)&Controller->Corb, 0, 31);
  UINT32 CorbHi = (UINT32)BitFieldRead64((UINT64)&Controller->Corb, 32, 63);
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
  // Set RIRB address
  // This depends on the address being 64-bits wide. 128-bit addresses are not supported. 32-bit addresses may work.
  UINT32 RirbLo = (UINT32)BitFieldRead64((UINT64)&Controller->Rirb, 0, 31);
  UINT32 RirbHi = (UINT32)BitFieldRead64((UINT64)&Controller->Rirb, 32, 63);
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
  Print(L"Free rirb: vaddr %08x, size %d, %d pages\n", (UINTN)&Controller->Rirb, Controller->RirbBytes, EFI_SIZE_TO_PAGES(Controller->RirbBytes));
  HdaCheckForError(Controller->PciIo->FreeBuffer(Controller->PciIo, EFI_SIZE_TO_PAGES(Controller->RirbBytes), (VOID*)Controller->Rirb), Controller);
}

static UINT32 HdaWriteCommand(IN OUT HDA_CONTROLLER* Controller, IN UINT32 CodecAddress, IN UINT32 NodeId, IN UINT32 Command, IN UINT32 Data) {
  Controller->CorbBytes = 1024;
  HdaCheckForError(Controller->PciIo->Map(Controller->PciIo, EfiPciIoOperationBusMasterCommonBuffer, (VOID*)&Controller->Corb, &Controller->CorbBytes, &Controller->CorbPhysAddress, (VOID**)&Controller->CorbMapping), Controller);
  ASSERT(Controller->CorbBytes == 1024);
  HDA_CORB_ENTRY Entry = {0};
  Entry.Bits.CodecAddress = CodecAddress;
  Entry.Bits.NodeId = NodeId;
  Entry.Bits.Command = Command;
  Entry.Bits.Data = Data;
  Print(L"Manifest corb entry: %04x\n", Entry.Raw);
  Controller->Corb[Controller->CorbWritePtr] = Entry.Raw;
  Print(L"Set corbwp: %x\n", Controller->CorbWritePtr);
  Controller->CorbWritePtr += 1;
  HdaCheckForError(Controller->PciIo->Unmap(Controller->PciIo, Controller->CorbMapping), Controller);
  HDA_CORB_WRPTR WritePointer = {0};
  WritePointer.Bits.WritePtr = Controller->CorbWritePtr;
  HdaCheckForError(Controller->PciIo->Mem.Write(Controller->PciIo, EfiPciIoWidthUint16, 0, HdaCorbWritePtr, 1, (VOID**)&WritePointer.Raw), Controller);
  HDA_CORB_RDPTR ReadPointer = {0};
  Print(L"Waiting\n");
  do {
    HdaCheckForError(Controller->PciIo->Mem.Read(Controller->PciIo, EfiPciIoWidthUint16, 0, HdaCorbWritePtr, 1, (VOID**)&WritePointer.Raw), Controller);
    HdaCheckForError(Controller->PciIo->Mem.Read(Controller->PciIo, EfiPciIoWidthUint16, 0, HdaCorbReadPtr, 1, (VOID**)&ReadPointer.Raw), Controller);
  } while (ReadPointer.Raw == WritePointer.Raw);
  Controller->RirbBytes = 2048;
  HdaCheckForError(Controller->PciIo->Map(Controller->PciIo, EfiPciIoOperationBusMasterCommonBuffer, (VOID*)&Controller->Rirb, &Controller->RirbBytes, &Controller->RirbPhysAddress, (VOID**)&Controller->RirbMapping), Controller);
  ASSERT(Controller->RirbBytes == 2048);
  Print(L"Read resp: %04x\n", Controller->Rirb[Controller->RirbReadPtr]);
  UINT32 Resp = Controller->Rirb[Controller->RirbReadPtr];
    Controller->RirbReadPtr++;
  Print(L"Set rirbrp: %x\n", Controller->RirbReadPtr);
  HdaCheckForError(Controller->PciIo->Unmap(Controller->PciIo, Controller->RirbMapping), Controller);
  return Resp;
}

static VOID HdaCheckForError(IN EFI_STATUS Status, IN OUT HDA_CONTROLLER* Controller) {
  if (EFI_ERROR(Status)) {
    Print(L"Failed operation: %r\n", Status);
    gBS->Exit(gImageHandle, Status, 0, NULL);
    }
}

static VOID HdaAllocateStreams(IN OUT HDA_CONTROLLER* Controller) {
  HDA_GLOBAL_CAPABILITIES Gcap = {0};
  HdaCheckForError(Controller->PciIo->Mem.Read(Controller->PciIo, EfiPciIoWidthUint16, 0, HdaGloblCaps, 1, (VOID**)&Gcap.Raw), Controller);
  Print(L"Read gcaps: %02x\n", Gcap.Raw);
  for (UINT64 InputStream = 0; InputStream < Gcap.Bits.InputStreams; ++InputStream) {
    Print(L"Alloc instrm %d: vaddr %08x, %d bytes, %d pages\n", InputStream, (UINTN)&Controller->InputBdl[InputStream], sizeof(HDA_BUFFER_DESCRIPTOR_LIST_ENTRY)*256, EFI_SIZE_TO_PAGES(sizeof(HDA_BUFFER_DESCRIPTOR_LIST_ENTRY)*256));
    HdaCheckForError(Controller->PciIo->AllocateBuffer(Controller->PciIo, AllocateAnyPages, EfiBootServicesData, EFI_SIZE_TO_PAGES(sizeof(HDA_BUFFER_DESCRIPTOR_LIST_ENTRY)*256), (VOID*)&Controller->InputBdl[InputStream], 0), Controller);
    UINT32 BdlLo = (UINT32)BitFieldRead64((UINT64)&Controller->InputBdl[InputStream], 0, 31);
    UINT32 BdlHi = (UINT32)BitFieldRead64((UINT64)&Controller->InputBdl[InputStream], 32, 63);
    Print(L"Set sd%dbdlbase (%x): %x\n", InputStream, HdaCalcIssOffset(InputStream, HdaStreamBdlLo), BdlLo);
    HdaCheckForError(Controller->PciIo->Mem.Write(Controller->PciIo, EfiPciIoWidthUint32, 0, HdaCalcIssOffset(InputStream, HdaStreamBdlLo), 1, (VOID**)&BdlLo), Controller);
    Print(L"Set sd%dbdlubase (%x): %x\n", InputStream, HdaCalcIssOffset(InputStream, HdaStreamBdlHi), BdlHi);
    HdaCheckForError(Controller->PciIo->Mem.Write(Controller->PciIo, EfiPciIoWidthUint32, 0, HdaCalcIssOffset(InputStream, HdaStreamBdlHi), 1, (VOID**)&BdlHi), Controller);
  }
  for (UINT64 OutputStream = 0; OutputStream < Gcap.Bits.OutputStreams; ++OutputStream) {
    Print(L"Alloc outstrm %d: vaddr %08x, %d bytes, %d pages\n", OutputStream, (UINTN)&Controller->OutputBdl[OutputStream], sizeof(HDA_BUFFER_DESCRIPTOR_LIST_ENTRY)*256, EFI_SIZE_TO_PAGES(sizeof(HDA_BUFFER_DESCRIPTOR_LIST_ENTRY)*256));
    HdaCheckForError(Controller->PciIo->AllocateBuffer(Controller->PciIo, AllocateAnyPages, EfiBootServicesData, EFI_SIZE_TO_PAGES(sizeof(HDA_BUFFER_DESCRIPTOR_LIST_ENTRY)*256), (VOID*)&Controller->OutputBdl[OutputStream], 0), Controller);
    UINT32 BdlLo = (UINT32)BitFieldRead64((UINT64)&Controller->OutputBdl[OutputStream], 0, 31);
    UINT32 BdlHi = (UINT32)BitFieldRead64((UINT64)&Controller->OutputBdl[OutputStream], 32, 63);
    Print(L"Set sd%dbdlbase (%x): %x\n", OutputStream, HdaCalcOssOffset(Controller, OutputStream, HdaStreamBdlLo), BdlLo);
    HdaCheckForError(Controller->PciIo->Mem.Write(Controller->PciIo, EfiPciIoWidthUint32, 0, HdaCalcOssOffset(Controller, OutputStream, HdaStreamBdlLo), 1, (VOID**)&BdlLo), Controller);
    Print(L"Set sd%dbdlubase (%x): %x\n", OutputStream, HdaCalcOssOffset(Controller, OutputStream, HdaStreamBdlHi), BdlHi);
    HdaCheckForError(Controller->PciIo->Mem.Write(Controller->PciIo, EfiPciIoWidthUint32, 0, HdaCalcOssOffset(Controller, OutputStream, HdaStreamBdlHi), 1, (VOID**)&BdlHi), Controller);
  }
  for (UINT64 BidirectionalStream = 0; BidirectionalStream < Gcap.Bits.BidirectionalStreams; ++BidirectionalStream) {
    Print(L"Alloc bistrm %d: vaddr %08x, %d bytes, %d pages\n", BidirectionalStream, (UINTN)&Controller->BidirectionalBdl[BidirectionalStream], sizeof(HDA_BUFFER_DESCRIPTOR_LIST_ENTRY)*256, EFI_SIZE_TO_PAGES(sizeof(HDA_BUFFER_DESCRIPTOR_LIST_ENTRY)*256));
    HdaCheckForError(Controller->PciIo->AllocateBuffer(Controller->PciIo, AllocateAnyPages, EfiBootServicesData, EFI_SIZE_TO_PAGES(sizeof(HDA_BUFFER_DESCRIPTOR_LIST_ENTRY)*256), (VOID*)&Controller->BidirectionalBdl[BidirectionalStream], 0), Controller);
    UINT32 BdlLo = (UINT32)BitFieldRead64((UINT64)&Controller->BidirectionalBdl[BidirectionalStream], 0, 31);
    UINT32 BdlHi = (UINT32)BitFieldRead64((UINT64)&Controller->BidirectionalBdl[BidirectionalStream], 32, 63);
    Print(L"Set sd%dbdlbase (%x): %x\n", BidirectionalStream, HdaCalcBssOffset(Controller, BidirectionalStream, HdaStreamBdlLo), BdlLo);
    HdaCheckForError(Controller->PciIo->Mem.Write(Controller->PciIo, EfiPciIoWidthUint32, 0, HdaCalcBssOffset(Controller, BidirectionalStream, HdaStreamBdlLo), 1, (VOID**)&BdlLo), Controller);
    Print(L"Set sd%dbdlubase (%x): %x\n", BidirectionalStream, HdaCalcBssOffset(Controller, BidirectionalStream, HdaStreamBdlHi), BdlHi);
    HdaCheckForError(Controller->PciIo->Mem.Write(Controller->PciIo, EfiPciIoWidthUint32, 0, HdaCalcBssOffset(Controller, BidirectionalStream, HdaStreamBdlHi), 1, (VOID**)&BdlHi), Controller);
  }
  // Allocate DMA positions
  // Good god, the amount of memory this thing needs... :P
  // Someone should check to see if this app even runs on an embedded system lol
  // Say, a system with 128 MB of RAM?
  // EDK II requires 64M min so... Oof
  // Luckaly, we can allocate this in one pass
  // Todo: see if we can allocate the streams in a single pass too, get rid of those for loops (possible O(3n) excluding complexity of called routines)
  Print(L"Alloc strmpos: vaddr %08x, %d bytes, %d pages\n", (UINTN)&Controller->StreamPositions, sizeof(HDA_STREAM_DMA_POSITION)*(Gcap.Bits.InputStreams + Gcap.Bits.OutputStreams + Gcap.Bits.BidirectionalStreams), EFI_SIZE_TO_PAGES(sizeof(HDA_STREAM_DMA_POSITION)*(Gcap.Bits.InputStreams + Gcap.Bits.OutputStreams + Gcap.Bits.BidirectionalStreams)));
  HdaCheckForError(Controller->PciIo->AllocateBuffer(Controller->PciIo, AllocateAnyPages, EfiBootServicesData, EFI_SIZE_TO_PAGES(sizeof(HDA_STREAM_DMA_POSITION)*(Gcap.Bits.InputStreams + Gcap.Bits.OutputStreams + Gcap.Bits.BidirectionalStreams)), (VOID*)&Controller->StreamPositions, 0), Controller);
}

static VOID HdaFreeStreams(IN OUT HDA_CONTROLLER* Controller) {
  HDA_GLOBAL_CAPABILITIES Gcap = {0};
  HdaCheckForError(Controller->PciIo->Mem.Read(Controller->PciIo, EfiPciIoWidthUint16, 0, HdaGloblCaps, 1, (VOID**)&Gcap.Raw), Controller);
  for (UINTN Stream = 0; Stream < Gcap.Bits.InputStreams; ++Stream) {
    HdaCheckForError(Controller->PciIo->FreeBuffer(Controller->PciIo, EFI_SIZE_TO_PAGES(Controller->InputBdlBytes[Stream]), (VOID*)&Controller->InputBdl[Stream]), Controller);
  }
  for (UINTN Stream = 0; Stream < Gcap.Bits.InputStreams; ++Stream) {
    HdaCheckForError(Controller->PciIo->FreeBuffer(Controller->PciIo, EFI_SIZE_TO_PAGES(Controller->OutputBdlBytes[Stream]), (VOID*)&Controller->OutputBdl[Stream]), Controller);
  }
  for (UINTN Stream = 0; Stream < Gcap.Bits.InputStreams; ++Stream) {
    HdaCheckForError(Controller->PciIo->FreeBuffer(Controller->PciIo, EFI_SIZE_TO_PAGES(Controller->BidirectionalBdlBytes[Stream]), (VOID*)&Controller->BidirectionalBdl[Stream]), Controller);
  }
  HdaCheckForError(Controller->PciIo->FreeBuffer(Controller->PciIo, EFI_SIZE_TO_PAGES(Controller->StreamPositionsBytes), (VOID*)&Controller->StreamPositions), Controller);
}

static UINT64 HdaCalcIssOffset(IN UINT64 StreamIndex, IN UINT64 RegisterOffset) {
  return (0x80 + (StreamIndex * 0x20)) + RegisterOffset;
}

static UINT64 HdaCalcOssOffset(IN HDA_CONTROLLER* Controller, IN UINT64 StreamIndex, IN UINT64 RegisterOffset) {
  HDA_GLOBAL_CAPABILITIES Gcap = {0};
  HdaCheckForError(Controller->PciIo->Mem.Read(Controller->PciIo, EfiPciIoWidthUint16, 0, HdaGloblCaps, 1, (VOID**)&Gcap.Raw), Controller);
  return ((Gcap.Bits.InputStreams * 0x20) + (StreamIndex * 0x20) + RegisterOffset);
}

static UINT64 HdaCalcBssOffset(IN HDA_CONTROLLER* Controller, IN UINT64 StreamIndex, IN UINT64 RegisterOffset) {
  HDA_GLOBAL_CAPABILITIES Gcap = {0};
  HdaCheckForError(Controller->PciIo->Mem.Read(Controller->PciIo, EfiPciIoWidthUint16, 0, HdaGloblCaps, 1, (VOID**)&Gcap.Raw), Controller);
  return ((Gcap.Bits.InputStreams * 0x20) + (Gcap.Bits.OutputStreams * 0x20) + (StreamIndex * 0x20)) + RegisterOffset;
}

static VOID HdaCreateNodeTree(IN OUT HDA_CONTROLLER* Controller) {
  for (UINT32 NodeId = 0; NodeId < 16; ++NodeId) {
    if (Controller->DetectedNodes & (1 << NodeId)) {
      for (UINT32 CodecAddress = 0; CodecAddress < (1 << 4); ++CodecAddress) {
        UINT32 Resp = HdaWriteCommand(Controller, CodecAddress, NodeId, HdaVerbGetParameter, HdaParamVendorDevId);
        // This is our check to see if the codec even exists.
        // If it has no vendor/dev id, we bail out.
        if (Resp == 0x0000)
          continue;
        Controller->Nodes[NodeId][CodecAddress].VendorDeviceId = Resp;
        Controller->Nodes[NodeId][CodecAddress].RevisionId = HdaWriteCommand(Controller, CodecAddress, NodeId, HdaVerbGetParameter, HdaParamRevId);
        Controller->Nodes[NodeId][CodecAddress].NodeCount = HdaWriteCommand(Controller, CodecAddress, NodeId, HdaVerbGetParameter, HdaParamNodeCount);
        Controller->Nodes[NodeId][CodecAddress].FunctionGroupType = HdaWriteCommand(Controller, CodecAddress, NodeId, HdaVerbGetParameter, HdaParamFunctionGroupType);
        Controller->Nodes[NodeId][CodecAddress].AudioGroupCaps = HdaWriteCommand(Controller, CodecAddress, NodeId, HdaVerbGetParameter, HdaParamAudioGroupCapabilities);
        Controller->Nodes[NodeId][CodecAddress].AudioWidgetCaps = HdaWriteCommand(Controller, CodecAddress, NodeId, HdaVerbGetParameter, HdaParamAudioWidgetCapabilities);
        Controller->Nodes[NodeId][CodecAddress].SupPcmRates = HdaWriteCommand(Controller, CodecAddress, NodeId, HdaVerbGetParameter, HdaParamSupportedPcmRates);
        Controller->Nodes[NodeId][CodecAddress].SupFmts = HdaWriteCommand(Controller, CodecAddress, NodeId, HdaVerbGetParameter, HdaParamSupportedFormats);
        Controller->Nodes[NodeId][CodecAddress].PinCaps = HdaWriteCommand(Controller, CodecAddress, NodeId, HdaVerbGetParameter, HdaParamPinCapabilities);
        Controller->Nodes[NodeId][CodecAddress].InAmpCaps = HdaWriteCommand(Controller, CodecAddress, NodeId, HdaVerbGetParameter, HdaParamInputAmplifierCapabilities);
        Controller->Nodes[NodeId][CodecAddress].ConnListLen = HdaWriteCommand(Controller, CodecAddress, NodeId, HdaVerbGetParameter, HdaParamConnectionListLength);
        Controller->Nodes[NodeId][CodecAddress].SupPowerStates = HdaWriteCommand(Controller, CodecAddress, NodeId, HdaVerbGetParameter, HdaParamSupportedPowerStates);
        Controller->Nodes[NodeId][CodecAddress].ProcCaps = HdaWriteCommand(Controller, CodecAddress, NodeId, HdaVerbGetParameter, HdaParamProcessingCapabilities);
        Controller->Nodes[NodeId][CodecAddress].GpioCount = HdaWriteCommand(Controller, CodecAddress, NodeId, HdaVerbGetParameter, HdaParamGpioCount);
        Controller->Nodes[NodeId][CodecAddress].OutAmpCaps = HdaWriteCommand(Controller, CodecAddress, NodeId, HdaVerbGetParameter, HdaParamOutputAmplifierCapabilities);
        Controller->Nodes[NodeId][CodecAddress].VolCaps = HdaWriteCommand(Controller, CodecAddress, NodeId, HdaVerbGetParameter, HdaParamVolumeCapabilities);
        Controller->Nodes[NodeId][CodecAddress].NodeCount &= 0xFF;
        Controller->Nodes[NodeId][CodecAddress].FunctionGroupType &= 0xFF;
        HdaWriteCommand(Controller, CodecAddress, NodeId, HdaVerbSetPowerState, 0x0);
        // Todo: This is a widget-dependent verb, so figure out how to identify it at runtime
        HdaWriteCommand(Controller, CodecAddress, NodeId, 0x3B0, 0x0);
      }
    } else
      continue;
  }
}

