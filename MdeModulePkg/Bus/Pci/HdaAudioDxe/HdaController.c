/** @file
  HD Audio controller register access, reset, and the CORB/RIRB command
  interface. The driver polls and does not enable interrupts.

  Copyright (c) 2026, Ethin Probst. All rights reserved.<BR>
  SPDX-License-Identifier: BSD-2-Clause-Patent

**/

#include "HdaAudioDxe.h"

//
// Layout of the command page. The CORB holds up to 256 4-byte entries and
// the RIRB up to 256 8-byte entries. Both offsets are 128-byte aligned.
//
#define HDA_CORB_OFFSET  0
#define HDA_RIRB_OFFSET  SIZE_1KB

/**
  Read an 8-bit controller register.

  @param[in] Controller  The controller instance.
  @param[in] Offset      Register offset in BAR 0.

  @return The register value, or 0xFF if the read failed.

**/
UINT8
HdaRead8 (
  IN HDA_CONTROLLER  *Controller,
  IN UINT32          Offset
  )
{
  EFI_STATUS  Status;
  UINT8       Value;

  Status = Controller->PciIo->Mem.Read (
                                    Controller->PciIo,
                                    EfiPciIoWidthUint8,
                                    HDA_BAR_INDEX,
                                    Offset,
                                    1,
                                    &Value
                                    );
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "%a: offset 0x%x: %r\n", __func__, Offset, Status));
    return MAX_UINT8;
  }

  return Value;
}

/**
  Read a 16-bit controller register.

  @param[in] Controller  The controller instance.
  @param[in] Offset      Register offset in BAR 0.

  @return The register value, or 0xFFFF if the read failed.

**/
UINT16
HdaRead16 (
  IN HDA_CONTROLLER  *Controller,
  IN UINT32          Offset
  )
{
  EFI_STATUS  Status;
  UINT16      Value;

  Status = Controller->PciIo->Mem.Read (
                                    Controller->PciIo,
                                    EfiPciIoWidthUint16,
                                    HDA_BAR_INDEX,
                                    Offset,
                                    1,
                                    &Value
                                    );
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "%a: offset 0x%x: %r\n", __func__, Offset, Status));
    return MAX_UINT16;
  }

  return Value;
}

/**
  Read a 32-bit controller register.

  @param[in] Controller  The controller instance.
  @param[in] Offset      Register offset in BAR 0.

  @return The register value, or 0xFFFFFFFF if the read failed.

**/
UINT32
HdaRead32 (
  IN HDA_CONTROLLER  *Controller,
  IN UINT32          Offset
  )
{
  EFI_STATUS  Status;
  UINT32      Value;

  Status = Controller->PciIo->Mem.Read (
                                    Controller->PciIo,
                                    EfiPciIoWidthUint32,
                                    HDA_BAR_INDEX,
                                    Offset,
                                    1,
                                    &Value
                                    );
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "%a: offset 0x%x: %r\n", __func__, Offset, Status));
    return MAX_UINT32;
  }

  return Value;
}

/**
  Write an 8-bit controller register.

  @param[in] Controller  The controller instance.
  @param[in] Offset      Register offset in BAR 0.
  @param[in] Value       The value to write.

**/
VOID
HdaWrite8 (
  IN HDA_CONTROLLER  *Controller,
  IN UINT32          Offset,
  IN UINT8           Value
  )
{
  EFI_STATUS  Status;

  Status = Controller->PciIo->Mem.Write (
                                    Controller->PciIo,
                                    EfiPciIoWidthUint8,
                                    HDA_BAR_INDEX,
                                    Offset,
                                    1,
                                    &Value
                                    );
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "%a: offset 0x%x: %r\n", __func__, Offset, Status));
  }
}

/**
  Write a 16-bit controller register.

  @param[in] Controller  The controller instance.
  @param[in] Offset      Register offset in BAR 0.
  @param[in] Value       The value to write.

**/
VOID
HdaWrite16 (
  IN HDA_CONTROLLER  *Controller,
  IN UINT32          Offset,
  IN UINT16          Value
  )
{
  EFI_STATUS  Status;

  Status = Controller->PciIo->Mem.Write (
                                    Controller->PciIo,
                                    EfiPciIoWidthUint16,
                                    HDA_BAR_INDEX,
                                    Offset,
                                    1,
                                    &Value
                                    );
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "%a: offset 0x%x: %r\n", __func__, Offset, Status));
  }
}

/**
  Write a 32-bit controller register.

  @param[in] Controller  The controller instance.
  @param[in] Offset      Register offset in BAR 0.
  @param[in] Value       The value to write.

**/
VOID
HdaWrite32 (
  IN HDA_CONTROLLER  *Controller,
  IN UINT32          Offset,
  IN UINT32          Value
  )
{
  EFI_STATUS  Status;

  Status = Controller->PciIo->Mem.Write (
                                    Controller->PciIo,
                                    EfiPciIoWidthUint32,
                                    HDA_BAR_INDEX,
                                    Offset,
                                    1,
                                    &Value
                                    );
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "%a: offset 0x%x: %r\n", __func__, Offset, Status));
  }
}

/**
  Poll an 8-bit register until (Register & Mask) == Value.

  @param[in] Controller  The controller instance.
  @param[in] Offset      Register offset in BAR 0.
  @param[in] Mask        Bits to test.
  @param[in] Value       Expected value of the tested bits.
  @param[in] TimeoutUs   Timeout in microseconds.

  @retval EFI_SUCCESS  The bits reached the expected value.
  @retval EFI_TIMEOUT  The timeout expired first.

**/
EFI_STATUS
HdaWaitReg8 (
  IN HDA_CONTROLLER  *Controller,
  IN UINT32          Offset,
  IN UINT8           Mask,
  IN UINT8           Value,
  IN UINTN           TimeoutUs
  )
{
  UINTN  Elapsed;

  for (Elapsed = 0; Elapsed <= TimeoutUs; Elapsed += HDA_POLL_US) {
    if ((HdaRead8 (Controller, Offset) & Mask) == Value) {
      return EFI_SUCCESS;
    }

    gBS->Stall (HDA_POLL_US);
  }

  return EFI_TIMEOUT;
}

/**
  Poll a 16-bit register until (Register & Mask) == Value.

  @param[in] Controller  The controller instance.
  @param[in] Offset      Register offset in BAR 0.
  @param[in] Mask        Bits to test.
  @param[in] Value       Expected value of the tested bits.
  @param[in] TimeoutUs   Timeout in microseconds.

  @retval EFI_SUCCESS  The bits reached the expected value.
  @retval EFI_TIMEOUT  The timeout expired first.

**/
EFI_STATUS
HdaWaitReg16 (
  IN HDA_CONTROLLER  *Controller,
  IN UINT32          Offset,
  IN UINT16          Mask,
  IN UINT16          Value,
  IN UINTN           TimeoutUs
  )
{
  UINTN  Elapsed;

  for (Elapsed = 0; Elapsed <= TimeoutUs; Elapsed += HDA_POLL_US) {
    if ((HdaRead16 (Controller, Offset) & Mask) == Value) {
      return EFI_SUCCESS;
    }

    gBS->Stall (HDA_POLL_US);
  }

  return EFI_TIMEOUT;
}

/**
  Poll a 32-bit register until (Register & Mask) == Value.

  @param[in] Controller  The controller instance.
  @param[in] Offset      Register offset in BAR 0.
  @param[in] Mask        Bits to test.
  @param[in] Value       Expected value of the tested bits.
  @param[in] TimeoutUs   Timeout in microseconds.

  @retval EFI_SUCCESS  The bits reached the expected value.
  @retval EFI_TIMEOUT  The timeout expired first.

**/
EFI_STATUS
HdaWaitReg32 (
  IN HDA_CONTROLLER  *Controller,
  IN UINT32          Offset,
  IN UINT32          Mask,
  IN UINT32          Value,
  IN UINTN           TimeoutUs
  )
{
  UINTN  Elapsed;

  for (Elapsed = 0; Elapsed <= TimeoutUs; Elapsed += HDA_POLL_US) {
    if ((HdaRead32 (Controller, Offset) & Mask) == Value) {
      return EFI_SUCCESS;
    }

    gBS->Stall (HDA_POLL_US);
  }

  return EFI_TIMEOUT;
}

/**
  Pick the largest ring size the controller supports.

  @param[in]  SizeReg  Value of CORBSIZE or RIRBSIZE.
  @param[out] Entries  Number of entries for the chosen size.

  @return The value for the size field, or MAX_UINT8 if no size is
          advertised.

**/
STATIC
UINT8
HdaPickRingSize (
  IN  UINT8   SizeReg,
  OUT UINT16  *Entries
  )
{
  if ((SizeReg & HDA_RINGSIZE_SZCAP_256) != 0) {
    *Entries = 256;
    return HDA_RINGSIZE_SIZE_256;
  }

  if ((SizeReg & HDA_RINGSIZE_SZCAP_16) != 0) {
    *Entries = 16;
    return HDA_RINGSIZE_SIZE_16;
  }

  if ((SizeReg & HDA_RINGSIZE_SZCAP_2) != 0) {
    *Entries = 2;
    return HDA_RINGSIZE_SIZE_2;
  }

  *Entries = 0;
  return MAX_UINT8;
}

/**
  Program a ring size register.

  @param[in] Controller  The controller instance.
  @param[in] Offset      HDA_REG_CORBSIZE or HDA_REG_RIRBSIZE.
  @param[in] SizeReg     The current register value.
  @param[in] Size        The size field value to program.

  @retval EFI_SUCCESS       The size is in effect.
  @retval EFI_DEVICE_ERROR  The controller did not accept the size.

**/
STATIC
EFI_STATUS
HdaSetRingSize (
  IN HDA_CONTROLLER  *Controller,
  IN UINT32          Offset,
  IN UINT8           SizeReg,
  IN UINT8           Size
  )
{
  if ((Controller->Quirks & HDA_QUIRK_RING_SIZE_READ_ONLY) != 0) {
    return EFI_SUCCESS;
  }

  HdaWrite8 (Controller, Offset, (UINT8)((SizeReg & ~HDA_RINGSIZE_SIZE_MASK) | Size));
  if ((HdaRead8 (Controller, Offset) & HDA_RINGSIZE_SIZE_MASK) != Size) {
    DEBUG ((DEBUG_ERROR, "%a: ring size at 0x%x not accepted\n", __func__, Offset));
    return EFI_DEVICE_ERROR;
  }

  return EFI_SUCCESS;
}

/**
  Stop the CORB and RIRB DMA engines.

  @param[in] Controller  The controller instance.

**/
STATIC
VOID
HdaStopCommandRings (
  IN HDA_CONTROLLER  *Controller
  )
{
  HdaWrite8 (Controller, HDA_REG_CORBCTL, 0);
  HdaWrite8 (Controller, HDA_REG_RIRBCTL, 0);
  HdaWaitReg8 (Controller, HDA_REG_CORBCTL, HDA_CORBCTL_RUN, 0, HDA_RESET_TIMEOUT_US);
  HdaWaitReg8 (Controller, HDA_REG_RIRBCTL, HDA_RIRBCTL_DMAEN, 0, HDA_RESET_TIMEOUT_US);
}

/**
  Stop every stream descriptor the controller implements.

  @param[in] Controller  The controller instance.

**/
STATIC
VOID
HdaStopAllStreams (
  IN HDA_CONTROLLER  *Controller
  )
{
  UINT32  Count;
  UINT32  Index;

  Count = HDA_GCAP_ISS (Controller->Gcap) +
          HDA_GCAP_OSS (Controller->Gcap) +
          HDA_GCAP_BSS (Controller->Gcap);

  for (Index = 0; Index < Count; Index++) {
    HdaWrite8 (Controller, HDA_REG_SD (Index) + HDA_REG_SD_CTL, 0);
  }
}

/**
  Allocate, map, and start the CORB and RIRB.

  @param[in] Controller  The controller instance.

  @retval EFI_SUCCESS       The rings are running.
  @retval EFI_UNSUPPORTED   The controller advertises no ring size.
  @retval EFI_DEVICE_ERROR  A DMA engine did not start.
  @retval Others            Allocation or mapping failed.

**/
STATIC
EFI_STATUS
HdaStartCommandRings (
  IN HDA_CONTROLLER  *Controller
  )
{
  EFI_STATUS  Status;
  UINTN       Bytes;
  UINT8       CorbSizeReg;
  UINT8       RirbSizeReg;
  UINT8       CorbSize;
  UINT8       RirbSize;
  UINT8       RirbControl;

  CorbSizeReg = HdaRead8 (Controller, HDA_REG_CORBSIZE);
  RirbSizeReg = HdaRead8 (Controller, HDA_REG_RIRBSIZE);
  CorbSize    = HdaPickRingSize (CorbSizeReg, &Controller->CorbEntries);
  RirbSize    = HdaPickRingSize (RirbSizeReg, &Controller->RirbEntries);
  if ((CorbSize == MAX_UINT8) || (RirbSize == MAX_UINT8)) {
    DEBUG ((
      DEBUG_ERROR,
      "%a: no ring size advertised (CORBSIZE 0x%x RIRBSIZE 0x%x)\n",
      __func__,
      CorbSizeReg,
      RirbSizeReg
      ));
    return EFI_UNSUPPORTED;
  }

  Status = Controller->PciIo->AllocateBuffer (
                                Controller->PciIo,
                                AllocateAnyPages,
                                EfiBootServicesData,
                                1,
                                &Controller->CmdBuffer,
                                0
                                );
  if (EFI_ERROR (Status)) {
    return Status;
  }

  ZeroMem (Controller->CmdBuffer, EFI_PAGE_SIZE);

  Bytes  = EFI_PAGE_SIZE;
  Status = Controller->PciIo->Map (
                                Controller->PciIo,
                                EfiPciIoOperationBusMasterCommonBuffer,
                                Controller->CmdBuffer,
                                &Bytes,
                                &Controller->CmdDeviceAddress,
                                &Controller->CmdMapping
                                );
  if (EFI_ERROR (Status) || (Bytes != EFI_PAGE_SIZE)) {
    if (!EFI_ERROR (Status)) {
      Controller->PciIo->Unmap (Controller->PciIo, Controller->CmdMapping);
      Status = EFI_OUT_OF_RESOURCES;
    }

    Controller->PciIo->FreeBuffer (Controller->PciIo, 1, Controller->CmdBuffer);
    Controller->CmdBuffer  = NULL;
    Controller->CmdMapping = NULL;
    return Status;
  }

  Controller->Corb = (volatile UINT32 *)((UINT8 *)Controller->CmdBuffer + HDA_CORB_OFFSET);
  Controller->Rirb = (volatile HDA_RIRB_ENTRY *)((UINT8 *)Controller->CmdBuffer + HDA_RIRB_OFFSET);

  //
  // CORB setup, Section 4.4.1.3.
  //
  Status = HdaSetRingSize (Controller, HDA_REG_CORBSIZE, CorbSizeReg, CorbSize);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  HdaWrite32 (Controller, HDA_REG_CORBLBASE, (UINT32)(Controller->CmdDeviceAddress + HDA_CORB_OFFSET));
  HdaWrite32 (Controller, HDA_REG_CORBUBASE, (UINT32)RShiftU64 (Controller->CmdDeviceAddress + HDA_CORB_OFFSET, 32));
  HdaWrite16 (Controller, HDA_REG_CORBWP, 0);
  Controller->CorbWritePointer = 0;

  HdaWrite16 (Controller, HDA_REG_CORBRP, HDA_CORBRP_RST);
  if (EFI_ERROR (HdaWaitReg16 (Controller, HDA_REG_CORBRP, HDA_CORBRP_RST, HDA_CORBRP_RST, HDA_RESET_TIMEOUT_US))) {
    DEBUG ((DEBUG_ERROR, "%a: CORBRP reset not acknowledged\n", __func__));
    return EFI_DEVICE_ERROR;
  }

  HdaWrite16 (Controller, HDA_REG_CORBRP, 0);
  if (EFI_ERROR (HdaWaitReg16 (Controller, HDA_REG_CORBRP, HDA_CORBRP_RST, 0, HDA_RESET_TIMEOUT_US))) {
    DEBUG ((DEBUG_ERROR, "%a: CORBRP stuck in reset\n", __func__));
    return EFI_DEVICE_ERROR;
  }

  //
  // RIRB setup, Section 4.4.2.3.
  //
  Status = HdaSetRingSize (Controller, HDA_REG_RIRBSIZE, RirbSizeReg, RirbSize);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  HdaWrite32 (Controller, HDA_REG_RIRBLBASE, (UINT32)(Controller->CmdDeviceAddress + HDA_RIRB_OFFSET));
  HdaWrite32 (Controller, HDA_REG_RIRBUBASE, (UINT32)RShiftU64 (Controller->CmdDeviceAddress + HDA_RIRB_OFFSET, 32));
  HdaWrite16 (Controller, HDA_REG_RIRBWP, HDA_RIRBWP_RST);
  Controller->RirbReadPointer = 0;
  HdaWrite8 (Controller, HDA_REG_RIRBSTS, HDA_RIRBSTS_RINTFL | HDA_RIRBSTS_RIRBOIS);

  RirbControl = HDA_RIRBCTL_DMAEN;
  if ((Controller->Quirks & HDA_QUIRK_RIRB_RESPONSE_COUNT) != 0) {
    HdaWrite16 (Controller, HDA_REG_RINTCNT, 0xFF);
    RirbControl |= HDA_RIRBCTL_RINTCTL;
  }

  HdaWrite8 (Controller, HDA_REG_RIRBCTL, RirbControl);
  HdaWrite8 (Controller, HDA_REG_CORBCTL, HDA_CORBCTL_RUN);
  if (EFI_ERROR (HdaWaitReg8 (Controller, HDA_REG_CORBCTL, HDA_CORBCTL_RUN, HDA_CORBCTL_RUN, HDA_RESET_TIMEOUT_US)) ||
      EFI_ERROR (HdaWaitReg8 (Controller, HDA_REG_RIRBCTL, HDA_RIRBCTL_DMAEN, HDA_RIRBCTL_DMAEN, HDA_RESET_TIMEOUT_US)))
  {
    DEBUG ((DEBUG_ERROR, "%a: CORB/RIRB DMA did not start\n", __func__));
    return EFI_DEVICE_ERROR;
  }

  DEBUG ((
    DEBUG_INFO,
    "%a: CORB %u entries, RIRB %u entries at 0x%lx\n",
    __func__,
    Controller->CorbEntries,
    Controller->RirbEntries,
    Controller->CmdDeviceAddress
    ));
  return EFI_SUCCESS;
}

/**
  Reset the controller, discover attached codecs, and start the command
  rings.

  @param[in]  Controller  The controller instance.
  @param[out] CodecMask   Bit N is set if a codec answered at address N.

  @retval EFI_SUCCESS       The controller is ready to accept verbs.
  @retval EFI_NOT_FOUND     No codecs are attached.
  @retval EFI_DEVICE_ERROR  The controller did not leave reset.
  @retval Others            Allocating or mapping the rings failed.

**/
EFI_STATUS
HdaControllerInit (
  IN  HDA_CONTROLLER  *Controller,
  OUT UINT16          *CodecMask
  )
{
  EFI_STATUS  Status;
  UINT32      Gctl;
  UINT16      StateStatus;

  Controller->Gcap = HdaRead16 (Controller, HDA_REG_GCAP);
  DEBUG ((
    DEBUG_INFO,
    "%a: HDA %u.%u, GCAP 0x%04x (ISS %u OSS %u BSS %u 64OK %u)\n",
    __func__,
    HdaRead8 (Controller, HDA_REG_VMAJ),
    HdaRead8 (Controller, HDA_REG_VMIN),
    Controller->Gcap,
    HDA_GCAP_ISS (Controller->Gcap),
    HDA_GCAP_OSS (Controller->Gcap),
    HDA_GCAP_BSS (Controller->Gcap),
    Controller->Gcap & HDA_GCAP_64OK
    ));

  //
  // Stop any DMA that is still running, for example from a previous driver.
  //
  HdaStopAllStreams (Controller);
  HdaStopCommandRings (Controller);

  Gctl = HdaRead32 (Controller, HDA_REG_GCTL);
  HdaWrite32 (Controller, HDA_REG_GCTL, Gctl & ~(UINT32)HDA_GCTL_CRST);
  Status = HdaWaitReg32 (Controller, HDA_REG_GCTL, HDA_GCTL_CRST, 0, HDA_RESET_TIMEOUT_US);
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "%a: controller did not enter reset\n", __func__));
    return EFI_DEVICE_ERROR;
  }

  gBS->Stall (HDA_RESET_HOLD_US);

  Gctl = HdaRead32 (Controller, HDA_REG_GCTL);
  HdaWrite32 (Controller, HDA_REG_GCTL, Gctl | HDA_GCTL_CRST);
  Status = HdaWaitReg32 (Controller, HDA_REG_GCTL, HDA_GCTL_CRST, HDA_GCTL_CRST, HDA_RESET_TIMEOUT_US);
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "%a: controller did not leave reset\n", __func__));
    return EFI_DEVICE_ERROR;
  }

  //
  // Give the codecs time to request a state change, then acknowledge them.
  // Interrupts and wake events stay disabled.
  //
  gBS->Stall (HDA_CODEC_WAKE_US);
  HdaWrite16 (Controller, HDA_REG_WAKEEN, 0);
  HdaWrite32 (Controller, HDA_REG_INTCTL, 0);

  StateStatus = HdaRead16 (Controller, HDA_REG_STATESTS) & (UINT16)(BIT15 - 1);
  HdaWrite16 (Controller, HDA_REG_STATESTS, StateStatus);
  DEBUG ((DEBUG_INFO, "%a: STATESTS 0x%04x\n", __func__, StateStatus));
  if (StateStatus == 0) {
    return EFI_NOT_FOUND;
  }

  Status = HdaStartCommandRings (Controller);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  *CodecMask = StateStatus;
  return EFI_SUCCESS;
}

/**
  Stop all DMA without touching memory allocations. Safe to call from an
  ExitBootServices() notification.

  @param[in] Controller  The controller instance.

**/
VOID
HdaControllerHalt (
  IN HDA_CONTROLLER  *Controller
  )
{
  HdaStopAllStreams (Controller);
  HdaWrite8 (Controller, HDA_REG_CORBCTL, 0);
  HdaWrite8 (Controller, HDA_REG_RIRBCTL, 0);
}

/**
  Stop all DMA engines and put the controller into reset. Frees and unmaps
  the command rings.

  @param[in] Controller  The controller instance.

**/
VOID
HdaControllerShutdown (
  IN HDA_CONTROLLER  *Controller
  )
{
  UINT32  Gctl;

  HdaStopAllStreams (Controller);
  HdaStopCommandRings (Controller);

  Gctl = HdaRead32 (Controller, HDA_REG_GCTL);
  HdaWrite32 (Controller, HDA_REG_GCTL, Gctl & ~(UINT32)HDA_GCTL_CRST);

  if (Controller->CmdMapping != NULL) {
    Controller->PciIo->Unmap (Controller->PciIo, Controller->CmdMapping);
    Controller->CmdMapping = NULL;
  }

  if (Controller->CmdBuffer != NULL) {
    Controller->PciIo->FreeBuffer (Controller->PciIo, 1, Controller->CmdBuffer);
    Controller->CmdBuffer = NULL;
  }

  Controller->Corb = NULL;
  Controller->Rirb = NULL;
}

/**
  Send one verb to a codec and wait for its response.

  @param[in]  Controller  The controller instance.
  @param[in]  Verb        The fully encoded verb (HDA_VERB_12 or HDA_VERB_4).
  @param[out] Response    The response of the codec. Optional.

  @retval EFI_SUCCESS  The codec responded.
  @retval EFI_TIMEOUT  No response arrived in time.

**/
EFI_STATUS
HdaSendVerb (
  IN  HDA_CONTROLLER  *Controller,
  IN  UINT32          Verb,
  OUT UINT32          *Response OPTIONAL
  )
{
  EFI_TPL         OldTpl;
  EFI_STATUS      Status;
  UINT8           Codec;
  UINT8           RirbStatus;
  UINT16          HwWritePointer;
  UINTN           Elapsed;
  HDA_RIRB_ENTRY  Entry;

  Codec  = (UINT8)(Verb >> 28);
  Status = EFI_TIMEOUT;

  //
  // The command rings are shared by all callers.
  //
  OldTpl = gBS->RaiseTPL (TPL_NOTIFY);

  Controller->CorbWritePointer                   = (UINT16)((Controller->CorbWritePointer + 1) % Controller->CorbEntries);
  Controller->Corb[Controller->CorbWritePointer] = Verb;
  MemoryFence ();
  HdaWrite16 (Controller, HDA_REG_CORBWP, Controller->CorbWritePointer);

  for (Elapsed = 0; Elapsed <= HDA_VERB_TIMEOUT_US; Elapsed += HDA_POLL_US) {
    //
    // Acknowledge any RIRB status. With HDA_QUIRK_RIRB_RESPONSE_COUNT, the
    // controller stops fetching verbs unless we clear RINTFL here.
    //
    RirbStatus = HdaRead8 (Controller, HDA_REG_RIRBSTS);
    if (RirbStatus != 0) {
      HdaWrite8 (Controller, HDA_REG_RIRBSTS, RirbStatus);
    }

    HwWritePointer = (UINT16)((HdaRead16 (Controller, HDA_REG_RIRBWP) & 0xFF) % Controller->RirbEntries);
    while (Controller->RirbReadPointer != HwWritePointer) {
      Controller->RirbReadPointer = (UINT16)((Controller->RirbReadPointer + 1) % Controller->RirbEntries);
      MemoryFence ();
      Entry.Response   = Controller->Rirb[Controller->RirbReadPointer].Response;
      Entry.ResponseEx = Controller->Rirb[Controller->RirbReadPointer].ResponseEx;

      if ((Entry.ResponseEx & HDA_RIRB_RESPONSE_EX_UNSOL) != 0) {
        continue;
      }

      if (HDA_RIRB_RESPONSE_EX_CODEC (Entry.ResponseEx) != Codec) {
        DEBUG ((
          DEBUG_WARN,
          "%a: stray response 0x%08x from codec %u\n",
          __func__,
          Entry.Response,
          HDA_RIRB_RESPONSE_EX_CODEC (Entry.ResponseEx)
          ));
        continue;
      }

      if (Response != NULL) {
        *Response = Entry.Response;
      }

      Status = EFI_SUCCESS;
      break;
    }

    if (!EFI_ERROR (Status)) {
      break;
    }

    gBS->Stall (HDA_POLL_US);
  }

  gBS->RestoreTPL (OldTpl);

  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "%a: verb 0x%08x timed out\n", __func__, Verb));
  }

  return Status;
}
