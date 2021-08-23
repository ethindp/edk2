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
#define HDA_USE_IMM

static EFI_HANDLE HdaFindController(IN EFI_HANDLE*, UINTN);
static HDA_CONTROLLER* HdaInstantiateAndReset(IN EFI_HANDLE);
static VOID HdaCheckForError(IN EFI_STATUS, IN OUT HDA_CONTROLLER*);
static VOID HdaScanCodecs(IN OUT HDA_CONTROLLER*);
static VOID HdaConfigureWakeEventsAndInterrupts(IN OUT HDA_CONTROLLER*);
#ifndef HDA_USE_IMM
static VOID HdaAllocateCorb(IN OUT HDA_CONTROLLER*);
static VOID HdaFreeCorb(IN OUT HDA_CONTROLLER*);
static VOID HdaAllocateRirb(IN OUT HDA_CONTROLLER*);
static VOID HdaFreeRirb(IN OUT HDA_CONTROLLER*);
#endif
static UINT32 HdaWriteCommand(IN OUT HDA_CONTROLLER*, IN UINT32, IN UINT32, IN UINT32, IN UINT32, IN HDA_VERB_TYPE);
static VOID HdaAllocateStreams(IN OUT HDA_CONTROLLER*);
static VOID HdaFreeStreams(IN OUT HDA_CONTROLLER*);
static UINT64 HdaCalcOssOffset(IN HDA_CONTROLLER* Controller, IN UINT64 StreamIndex, IN UINT64 RegisterOffset);
static VOID HdaCreateCodecTree(IN OUT HDA_CONTROLLER*);
static VOID FillSubNodeTree(IN OUT HDA_CONTROLLER*, IN UINT32, IN UINT32, IN UINT32);

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
  Print(L"Scanning for new codecs\n");
  HdaScanCodecs(Controller);
  // Disable wake events and interrupts
  Print(L"Disabling wake events and interrupts\n");
  HdaConfigureWakeEventsAndInterrupts(Controller);
  // Use the immediate interface if available
  // Otherwise, allocate and map the CORB and RIRB
  UINT16 ImmStatus = 0;
  HdaCheckForError(Controller->PciIo->Mem.Read(Controller->PciIo, EfiPciIoWidthUint16, 0, HdaImmCmdStatus, 1, (VOID*)&ImmStatus), Controller);
  #ifndef HDA_USE_IMM
    Print(L"Allocating corb\n");
    HdaAllocateCorb(Controller);
    Print(L"Allocating rirb\n");
    HdaAllocateRirb(Controller);
  #endif
  Print(L"Allocating streams\n");
  HdaAllocateStreams(Controller);
  // Enumerate through all codecs and read their data and bring them online
  HdaCreateCodecTree(Controller);
  // Initialize BDLs
  Controller->OutputBdlBytes[1] = sizeof(HDA_BUFFER_DESCRIPTOR_LIST_ENTRY)*256;
  UINTN SineBytes = 96000;
  VOID* SineMapping = NULL;
  EFI_PHYSICAL_ADDRESS SinePhysAddr;
  HdaCheckForError(Controller->PciIo->Map(Controller->PciIo, EfiPciIoOperationBusMasterCommonBuffer, (VOID*)&Controller->OutputBdl[1], &Controller->OutputBdlBytes[1], &Controller->OutputBdlPhysAddress[1], (VOID**)&Controller->OutputBdlMapping[1]), Controller);
  HdaCheckForError(Controller->PciIo->Map(Controller->PciIo, EfiPciIoOperationBusMasterCommonBuffer, (VOID*)&SINE_SAMPLES, &SineBytes, &SinePhysAddr, (VOID**)&SineMapping), Controller);
  ASSERT(Controller->OutputBdlBytes[1] == sizeof(HDA_BUFFER_DESCRIPTOR_LIST_ENTRY)*256);
  ASSERT(SineBytes == 96000);
  Controller->OutputBdl[1][0].Address = SinePhysAddr;
  Controller->OutputBdl[1][0].Length = 48000/2;
  Controller->OutputBdl[1][0].Config = 0;
  Controller->OutputBdl[1][1].Address = SinePhysAddr + (48000 / 2);
  Controller->OutputBdl[1][1].Length = 48000/2;
  Controller->OutputBdl[1][1].Config = 0;
  UINT32 LastValidIndex =2;
  HdaCheckForError(Controller->PciIo->Mem.Write(Controller->PciIo, EfiPciIoWidthUint32, 0, HdaStreamLastValidIndex, 1, (VOID*)&LastValidIndex), Controller);
  UINT32 BdlSize = 48000;
  HdaCheckForError(Controller->PciIo->Mem.Write(Controller->PciIo, EfiPciIoWidthUint32, 0, HdaCalcOssOffset(Controller, 1, HdaStreamCyclicBufferLength), 1, (VOID*)&BdlSize), Controller);
  UINT32 BdlLo = (UINT32)BitFieldRead64(Controller->OutputBdlPhysAddress[1], 0, 31);
  UINT32 BdlHi = (UINT32)BitFieldRead64(Controller->OutputBdlPhysAddress[1], 32, 63);
  HdaCheckForError(Controller->PciIo->Mem.Write(Controller->PciIo, EfiPciIoWidthUint32, 0, HdaCalcOssOffset(Controller, 1, HdaStreamBdlLo), 1, (VOID*)&BdlLo), Controller);
  HdaCheckForError(Controller->PciIo->Mem.Write(Controller->PciIo, EfiPciIoWidthUint32, 0, HdaCalcOssOffset(Controller, 1, HdaStreamBdlHi), 1, (VOID*)&BdlHi), Controller);
  HdaCheckForError(Controller->PciIo->Map(Controller->PciIo, EfiPciIoOperationBusMasterCommonBuffer, (VOID*)&Controller->StreamPositions, &Controller->StreamPositionsBytes, &Controller->StreamPositionsPhysAddress, (VOID*)&Controller->StreamPositionsMapping), Controller);
  UINT32 DplLo = (UINT32)BitFieldRead64(Controller->StreamPositionsPhysAddress, 0, 31);
  UINT32 DplHi = (UINT32)BitFieldRead64(Controller->StreamPositionsPhysAddress, 32, 63);
  HdaCheckForError(Controller->PciIo->Mem.Write(Controller->PciIo, EfiPciIoWidthUint32, 0, HdaDplLo, 1, (VOID*)&DplLo), Controller);
  HdaCheckForError(Controller->PciIo->Mem.Write(Controller->PciIo, EfiPciIoWidthUint32, 0, HdaDplLo, 1, (VOID*)&DplHi), Controller);
  HDA_STREAM_FORMAT_DESCRIPTOR Format = {0};
  Format.Pcm.Channels = 1;
  Format.Pcm.BitsPerSample = 1;
  HdaCheckForError(Controller->PciIo->Mem.Write(Controller->PciIo, EfiPciIoWidthUint16, 0, HdaCalcOssOffset(Controller, 1, HdaStreamFormat), 1, (VOID*)&Format.Raw), Controller);
  for (UINTN i = 0; i < 16; ++i)
    for (UINTN j = 0; j < 16; ++j) {
      if (!Controller->Codecs[i][j])
        continue;
      if (BitFieldRead32(Controller->Codecs[i][j]->AudioWidgetCaps, 20, 23) == HdaWidgetTypeAudioOut) {
        HdaWriteCommand(Controller, i, j, HdaVerbSetStreamFormat, Format.Raw, HdaCmdShort);
        UINT32 StreamChannelData = 0;
        StreamChannelData = BitFieldWrite32(StreamChannelData, 4, 7, 1);
        HdaWriteCommand(Controller, i, j, HdaVerbSetStreamChannel, StreamChannelData, HdaCmdShort);
      }
    }
  UINT32 StreamControlData = 0;
  StreamControlData = BitFieldWrite32(StreamControlData, 20, 23, 1); // Stream number
  StreamControlData = BitFieldWrite32(StreamControlData, 18, 18, 1); // Traffic priority
  StreamControlData = BitFieldWrite32(StreamControlData, 1, 1, 1); // Stream run
  HdaCheckForError(Controller->PciIo->Mem.Write(Controller->PciIo, EfiPciIoWidthUint32, 0, HdaCalcOssOffset(Controller, 1, HdaStreamControl), 1, (VOID*)&StreamControlData), Controller);
  UINT32 StreamSync = 0;
  HdaCheckForError(Controller->PciIo->Mem.Write(Controller->PciIo, EfiPciIoWidthUint32, 0, HdaStrmSync, 1, (VOID*)&StreamSync), Controller);
  HdaCheckForError(Controller->PciIo->Unmap(Controller->PciIo, Controller->OutputBdlMapping[1]), Controller);
  HdaCheckForError(Controller->PciIo->Unmap(Controller->PciIo, Controller->StreamPositionsMapping), Controller);
  HdaCheckForError(Controller->PciIo->Unmap(Controller->PciIo, SineMapping), Controller);
  Print(L"Freeing codecs\n");
  for (UINTN Address = 0; Address < 16; ++Address)
    for (UINTN Node = 0; Node < 256; ++Node)
      if (Controller->Codecs[Address][Node])
        gBS->FreePool(Controller->Codecs[Address][Node]);

  Print(L"Freeing streams\n");
  HdaFreeStreams(Controller);
  #ifndef HDA_USE_IMM
    Print(L"Freeing corb\n");
    HdaFreeCorb(Controller);
    Print(L"Freeing rirb\n");
    HdaFreeRirb(Controller);
  #endif
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

static VOID HdaScanCodecs(IN OUT HDA_CONTROLLER* Controller) {
  // Go through each codec in STATESTS and see if its bit is set.
  // If that bit is set, set it in our struct as well.
  UINT16 StateStatus = 0;
  HdaCheckForError(Controller->PciIo->Mem.Read(Controller->PciIo, EfiPciIoWidthUint16, 0, HdaStateStatus, 1, (VOID**)&StateStatus), Controller);
  for (UINT16 i = 0; i < 15; ++i) {
    Print(L"Checking STATESTS[%d]\n", i);
    if (StateStatus & (1 << i)) {
      Controller->DetectedCodecs |= (1 << i);
      StateStatus |= (1 << i);
      Print(L"Bit set; storing in Controller->DetectedCodecs\n");
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

#ifndef HDA_USE_IMM
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
#endif

static UINT32 HdaWriteCommand(IN OUT HDA_CONTROLLER* Controller, IN UINT32 CodecAddress, IN UINT32 NodeId, IN UINT32 Command, IN UINT32 Data, HDA_VERB_TYPE VerbType) {
  HDA_CORB_ENTRY Entry = {0};
  if (VerbType == HdaCmdShort) {
    Entry.Short.CodecAddress = CodecAddress % (1 << 4);
    Entry.Short.NodeId = NodeId % (1 << 8);
    Entry.Short.Command = Command % (1 << 12);
    Entry.Short.Payload = Data % (1 << 8);
  } else {
    Entry.Long.CodecAddress = CodecAddress % (1 << 4);
    Entry.Long.NodeId = NodeId % (1 << 8);
    Entry.Long.Command = Command % (1 << 4);
    Entry.Long.Payload = Data % (1 << 16);
  }
  // Disable indirect node addressing if present
  if (Entry.Raw & (1 << 27)) {
    Print(L"Warning: verb %04x uses indirect node addressing; changing to ", Entry.Raw);
    Entry.Raw &= ~(1 << 27);
    Print(L"%04x\n", Entry.Raw);
  }
  #ifdef HDA_USE_IMM
    // Write constructed verb in immediate command output
    HdaCheckForError(Controller->PciIo->Mem.Write(Controller->PciIo, EfiPciIoWidthUint32, 0, HdaImmCmdOut, 1, (VOID**)&Entry.Raw), Controller);
    // Set ICB bit (bit 0 of immediate command status) to allow for command processing
    UINT16 ImmStatus = 0;
    HdaCheckForError(Controller->PciIo->Mem.Read(Controller->PciIo, EfiPciIoWidthUint16, 0, HdaImmCmdStatus, 1, (VOID*)&ImmStatus), Controller);
    ImmStatus |= (1 << 0);
    HdaCheckForError(Controller->PciIo->Mem.Write(Controller->PciIo, EfiPciIoWidthUint16, 0, HdaImmCmdStatus, 1, (VOID*)&ImmStatus), Controller);
    EFI_EVENT Event;
    HdaCheckForError(gBS->CreateEvent(EVT_TIMER, TPL_NOTIFY, NULL, NULL, &Event), Controller);
    HdaCheckForError(gBS->SetTimer(Event, TimerRelative, 10000), Controller);
    do {
      if (gBS->CheckEvent(Event) == EFI_SUCCESS)
        break;
      HdaCheckForError(Controller->PciIo->Mem.Read(Controller->PciIo, EfiPciIoWidthUint16, 0, HdaImmCmdStatus, 1, (VOID*)&ImmStatus), Controller);
    }while (!BitFieldRead16(ImmStatus, 1, 1));
    if (gBS->CheckEvent(Event) == EFI_SUCCESS) {
      HdaCheckForError(gBS->CloseEvent(Event), Controller);
      return MAX_UINT32;
    }
    HdaCheckForError(gBS->CloseEvent(Event), Controller);
    UINT32 Resp = 0;
    HdaCheckForError(Controller->PciIo->Mem.Read(Controller->PciIo, EfiPciIoWidthUint32, 0, HdaImmRespIn, 1, (VOID*)&Resp), Controller);
    HdaCheckForError(Controller->PciIo->Mem.Write(Controller->PciIo, EfiPciIoWidthUint16, 0, HdaImmCmdStatus, 1, (VOID*)&ImmStatus), Controller);
    return Resp;
  #else
    Controller->CorbBytes = 1024;
    HdaCheckForError(Controller->PciIo->Map(Controller->PciIo, EfiPciIoOperationBusMasterCommonBuffer, (VOID*)&Controller->Corb, &Controller->CorbBytes, &Controller->CorbPhysAddress, (VOID**)&Controller->CorbMapping), Controller);
    ASSERT(Controller->CorbBytes == 1024);
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
  #endif
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
  }
  for (UINT64 OutputStream = 0; OutputStream < Gcap.Bits.OutputStreams; ++OutputStream) {
    Print(L"Alloc outstrm %d: vaddr %08x, %d bytes, %d pages\n", OutputStream, (UINTN)&Controller->OutputBdl[OutputStream], sizeof(HDA_BUFFER_DESCRIPTOR_LIST_ENTRY)*256, EFI_SIZE_TO_PAGES(sizeof(HDA_BUFFER_DESCRIPTOR_LIST_ENTRY)*256));
    HdaCheckForError(Controller->PciIo->AllocateBuffer(Controller->PciIo, AllocateAnyPages, EfiBootServicesData, EFI_SIZE_TO_PAGES(sizeof(HDA_BUFFER_DESCRIPTOR_LIST_ENTRY)*256), (VOID*)&Controller->OutputBdl[OutputStream], 0), Controller);
  }
  for (UINT64 BidirectionalStream = 0; BidirectionalStream < Gcap.Bits.BidirectionalStreams; ++BidirectionalStream) {
    Print(L"Alloc bistrm %d: vaddr %08x, %d bytes, %d pages\n", BidirectionalStream, (UINTN)&Controller->BidirectionalBdl[BidirectionalStream], sizeof(HDA_BUFFER_DESCRIPTOR_LIST_ENTRY)*256, EFI_SIZE_TO_PAGES(sizeof(HDA_BUFFER_DESCRIPTOR_LIST_ENTRY)*256));
    HdaCheckForError(Controller->PciIo->AllocateBuffer(Controller->PciIo, AllocateAnyPages, EfiBootServicesData, EFI_SIZE_TO_PAGES(sizeof(HDA_BUFFER_DESCRIPTOR_LIST_ENTRY)*256), (VOID*)&Controller->BidirectionalBdl[BidirectionalStream], 0), Controller);
  }
  // Allocate DMA positions
  // Good god, the amount of memory this thing needs... :P
  // Someone should check to see if this app even runs on an embedded system lol
  // Say, a system with 128 MB of RAM?
  // EDK II requires 64M min so... Oof
  // Luckaly, we can allocate this in one pass
  // Todo: see if we can allocate the streams in a single pass too, get rid of those for loops (possible O(3n) excluding complexity of called routines)
  Print(L"Alloc strmpos: vaddr %08x, %d bytes, %d pages\n", (UINTN)&Controller->StreamPositions, sizeof(HDA_STREAM_DMA_POSITION)*(Gcap.Bits.InputStreams + Gcap.Bits.OutputStreams + Gcap.Bits.BidirectionalStreams), EFI_SIZE_TO_PAGES(sizeof(HDA_STREAM_DMA_POSITION)*(Gcap.Bits.InputStreams + Gcap.Bits.OutputStreams + Gcap.Bits.BidirectionalStreams)));
  Controller->StreamPositionsBytes = sizeof(HDA_STREAM_DMA_POSITION)*(Gcap.Bits.InputStreams + Gcap.Bits.OutputStreams + Gcap.Bits.BidirectionalStreams);
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

static UINT64 HdaCalcOssOffset(IN HDA_CONTROLLER* Controller, IN UINT64 StreamIndex, IN UINT64 RegisterOffset) {
  HDA_GLOBAL_CAPABILITIES Gcap = {0};
  HdaCheckForError(Controller->PciIo->Mem.Read(Controller->PciIo, EfiPciIoWidthUint16, 0, HdaGloblCaps, 1, (VOID**)&Gcap.Raw), Controller);
  UINT16 IssCount = BitFieldRead16(Gcap.Raw, 12, 15);
  return RegisterOffset + (0x80 + (IssCount * 0x20) + (StreamIndex * 0x20));
}

static VOID HdaCreateCodecTree(IN OUT HDA_CONTROLLER* Controller) {
  Print(L"Scanning nodes\n");
  for (UINT32 CodecAddress = 0; CodecAddress < 16; ++CodecAddress) {
    if (Controller->DetectedCodecs & (1 << CodecAddress)) {
      Print(L"Scanning codec %04x\n", CodecAddress);
      HDA_CODEC *Codec = AllocateZeroPool(sizeof(HDA_CODEC));
      Codec->VendorDeviceId = HdaWriteCommand(Controller, CodecAddress, 0, HdaVerbGetParameter, HdaParamVendorDevId, HdaCmdShort);
      Codec->RevisionId = HdaWriteCommand(Controller, CodecAddress, 0, HdaVerbGetParameter, HdaParamRevisionId, HdaCmdShort);
      Codec->NodeCount = HdaWriteCommand(Controller, CodecAddress, 0, HdaVerbGetParameter, HdaParamNodeCount, HdaCmdShort);
      Codec->FunctionGroupType = HdaWriteCommand(Controller, CodecAddress, 0, HdaVerbGetParameter, HdaParamFunctionGroupType, HdaCmdShort);
      Codec->AudioGroupCaps = HdaWriteCommand(Controller, CodecAddress, 0, HdaVerbGetParameter, HdaParamAudioGroupCapabilities, HdaCmdShort);
      Codec->AudioWidgetCaps = HdaWriteCommand(Controller, CodecAddress, 0, HdaVerbGetParameter, HdaParamAudioGroupCapabilities, HdaCmdShort);
      Codec->SupPcmRates = HdaWriteCommand(Controller, CodecAddress, 0, HdaVerbGetParameter, HdaParamSupportedPcmRates, HdaCmdShort);
      Codec->SupFmts = HdaWriteCommand(Controller, CodecAddress, 0, HdaVerbGetParameter, HdaParamSupportedFormats, HdaCmdShort);
      Codec->PinCaps = HdaWriteCommand(Controller, CodecAddress, 0, HdaVerbGetParameter, HdaParamPinCapabilities, HdaCmdShort);
      Codec->InAmpCaps = HdaWriteCommand(Controller, CodecAddress, 0, HdaVerbGetParameter, HdaParamInputAmplifierCapabilities, HdaCmdShort);
      Codec->ConnListLen = HdaWriteCommand(Controller, CodecAddress, 0, HdaVerbGetParameter, HdaParamConnectionListLength, HdaCmdShort);
      Codec->SupPowerStates = HdaWriteCommand(Controller, CodecAddress, 0, HdaVerbGetParameter, HdaParamSupportedPowerStates, HdaCmdShort);
      Codec->ProcCaps = HdaWriteCommand(Controller, CodecAddress, 0, HdaVerbGetParameter, HdaParamProcessingCapabilities, HdaCmdShort);
      Codec->GpioCount = HdaWriteCommand(Controller, CodecAddress, 0, HdaVerbGetParameter, HdaParamGpioCount, HdaCmdShort);
      Codec->OutAmpCaps = HdaWriteCommand(Controller, CodecAddress, 0, HdaVerbGetParameter, HdaParamOutputAmplifierCapabilities, HdaCmdShort);
      Codec->VolCaps = HdaWriteCommand(Controller, CodecAddress, 0, HdaVerbGetParameter, HdaParamVolumeCapabilities, HdaCmdShort);
      Controller->Codecs[CodecAddress][0] = Codec;
      Print(L"Vendor/device ID: %04x\n", Codec->VendorDeviceId);
      Print(L"Revision ID: %04x\n", Codec->RevisionId);
      Print(L"Node count: %04x\n", Codec->NodeCount);
      Print(L"Function group type: %04x\n", Codec->FunctionGroupType);
      Print(L"Audio group caps: %04x\n", Codec->AudioGroupCaps);
      Print(L"Audio widget caps: %04x\n", Codec->AudioWidgetCaps);
      Print(L"Supported PCM rates: %04x\n", Codec->SupPcmRates);
      Print(L"Supported formats: %04x\n", Codec->SupFmts);
      Print(L"Pin widget caps: %04x\n", Codec->PinCaps);
      Print(L"Input amplifier capabilities: %04x\n", Codec->InAmpCaps);
      Print(L"Connection list length: %04x\n", Codec->ConnListLen);
      Print(L"Supported power states: %04x\n", Codec->SupPowerStates);
      Print(L"Processing capabilities: %04x\n", Codec->ProcCaps);
      Print(L"GPIO count: %04x\n", Codec->GpioCount);
      Print(L"Output amplifier capabilities: %04x\n", Codec->OutAmpCaps);
      Print(L"Volume capabilities: %04x\n", Codec->VolCaps);
      Print(L"Root codec has %d nodes starting at node ID %d\n", BitFieldRead32(Codec->NodeCount, 0, 7), BitFieldRead32(Codec->NodeCount, 16, 23));
      for (UINT32 Node = BitFieldRead32(Controller->Codecs[CodecAddress][0]->NodeCount, 16, 23); Node < BitFieldRead32(Controller->Codecs[CodecAddress][0]->NodeCount, 0, 7) + 1; Node++) {
        FillSubNodeTree(Controller, CodecAddress, BitFieldRead32(Codec->NodeCount, 16, 23), BitFieldRead32(Codec->NodeCount, 0, 7));
      }
    }
  }
}

static VOID FillSubNodeTree(IN OUT HDA_CONTROLLER* Controller, IN UINT32 CodecAddress, IN UINT32 StartNode, IN UINT32 NodeCount) {
  for (UINT32 Node = StartNode; Node < NodeCount + 1; Node++) {
    Print(L"Scanning node %d\n", Node);
    // Allocate codec
    HDA_CODEC* Codec = AllocateZeroPool(sizeof(HDA_CODEC));
    // Extract node parameters
    Codec->VendorDeviceId = HdaWriteCommand(Controller, CodecAddress, Node, HdaVerbGetParameter, HdaParamVendorDevId, HdaCmdShort);
    Codec->RevisionId = HdaWriteCommand(Controller, CodecAddress, Node, HdaVerbGetParameter, HdaParamRevisionId, HdaCmdShort);
    Codec->NodeCount = HdaWriteCommand(Controller, CodecAddress, Node, HdaVerbGetParameter, HdaParamNodeCount, HdaCmdShort);
    Codec->FunctionGroupType = HdaWriteCommand(Controller, CodecAddress, Node, HdaVerbGetParameter, HdaParamFunctionGroupType, HdaCmdShort);
    Codec->AudioGroupCaps = HdaWriteCommand(Controller, CodecAddress, Node, HdaVerbGetParameter, HdaParamAudioGroupCapabilities, HdaCmdShort);
    Codec->AudioWidgetCaps = HdaWriteCommand(Controller, CodecAddress, Node, HdaVerbGetParameter, HdaParamAudioGroupCapabilities, HdaCmdShort);
    Codec->SupPcmRates = HdaWriteCommand(Controller, CodecAddress, Node, HdaVerbGetParameter, HdaParamSupportedPcmRates, HdaCmdShort);
    Codec->SupFmts = HdaWriteCommand(Controller, CodecAddress, Node, HdaVerbGetParameter, HdaParamSupportedFormats, HdaCmdShort);
    Codec->PinCaps = HdaWriteCommand(Controller, CodecAddress, Node, HdaVerbGetParameter, HdaParamPinCapabilities, HdaCmdShort);
    Codec->InAmpCaps = HdaWriteCommand(Controller, CodecAddress, Node, HdaVerbGetParameter, HdaParamInputAmplifierCapabilities, HdaCmdShort);
    Codec->ConnListLen = HdaWriteCommand(Controller, CodecAddress, Node, HdaVerbGetParameter, HdaParamConnectionListLength, HdaCmdShort);
    Codec->SupPowerStates = HdaWriteCommand(Controller, CodecAddress, Node, HdaVerbGetParameter, HdaParamSupportedPowerStates, HdaCmdShort);
    Codec->ProcCaps = HdaWriteCommand(Controller, CodecAddress, Node, HdaVerbGetParameter, HdaParamProcessingCapabilities, HdaCmdShort);
    Codec->GpioCount = HdaWriteCommand(Controller, CodecAddress, Node, HdaVerbGetParameter, HdaParamGpioCount, HdaCmdShort);
    Codec->OutAmpCaps = HdaWriteCommand(Controller, CodecAddress, Node, HdaVerbGetParameter, HdaParamOutputAmplifierCapabilities, HdaCmdShort);
    Codec->VolCaps = HdaWriteCommand(Controller, CodecAddress, Node, HdaVerbGetParameter, HdaParamVolumeCapabilities, HdaCmdShort);
    Controller->Codecs[CodecAddress][Node] = Codec;
    Print(L"Vendor/device ID: %04x\n", Codec->VendorDeviceId);
    Print(L"Revision ID: %04x\n", Codec->RevisionId);
    Print(L"Node count: %04x\n", Codec->NodeCount);
    Print(L"Function group type: %04x\n", Codec->FunctionGroupType);
    Print(L"Audio group caps: %04x\n", Codec->AudioGroupCaps);
    Print(L"Audio widget caps: %04x\n", Codec->AudioWidgetCaps);
    Print(L"Supported PCM rates: %04x\n", Codec->SupPcmRates);
    Print(L"Supported formats: %04x\n", Codec->SupFmts);
    Print(L"Pin widget caps: %04x\n", Codec->PinCaps);
    Print(L"Input amplifier capabilities: %04x\n", Codec->InAmpCaps);
    Print(L"Connection list length: %04x\n", Codec->ConnListLen);
    Print(L"Supported power states: %04x\n", Codec->SupPowerStates);
    Print(L"Processing capabilities: %04x\n", Codec->ProcCaps);
    Print(L"GPIO count: %04x\n", Codec->GpioCount);
    Print(L"Output amplifier capabilities: %04x\n", Codec->OutAmpCaps);
    Print(L"Volume capabilities: %04x\n", Codec->VolCaps);
    // Configure codec
    if (BitFieldRead32(Codec->SupPowerStates, 0, 0)) {
      Print(L"Bringing node %04x on codec %04x online\n", Node, CodecAddress);
      HdaWriteCommand(Controller, CodecAddress, Node, HdaVerbSetPowerState, 0x0, HdaCmdShort);
    }
    if (BitFieldRead32(Codec->InAmpCaps, 31, 31)) {
      Print(L"Unmuting input on node %04x on codec %04x\n", Node, CodecAddress);
      UINT32 Payload = (1 << 12) | (1 << 13) | (1 << 14); // all channels, set input
      Payload = BitFieldWrite32(Payload, 0, 6, BitFieldRead32(Codec->InAmpCaps, 0, 6));
      HdaWriteCommand(Controller, CodecAddress, Node, HdaVerbSetAmplifierGain, Payload, HdaCmdLong);
    }
    if (BitFieldRead32(Codec->OutAmpCaps, 31, 31)) {
      Print(L"Unmuting output on node %04x on codec %04x\n", Node, CodecAddress);
      UINT32 Payload = (1 << 12) | (1 << 13) | (1 << 15); // all channels, output
      Payload = BitFieldWrite32(Payload, 0, 6, BitFieldRead32(Codec->OutAmpCaps, 0, 6));
      HdaWriteCommand(Controller, CodecAddress, Node, HdaVerbSetAmplifierGain, Payload, HdaCmdLong);
    }
    if (BitFieldRead32(Codec->NodeCount, 0, 7) > 0)
      FillSubNodeTree(Controller, CodecAddress, BitFieldRead32(Codec->NodeCount, 16, 23), BitFieldRead32(Codec->NodeCount, 0, 7));
  }
}

