/** @file
  HD Audio output stream. A periodic timer refills a cyclic ring from the
  caller's buffer, converting mono to stereo and applying software gain.

  Copyright (c) 2026, Ethin Probst. All rights reserved.<BR>
  SPDX-License-Identifier: BSD-2-Clause-Patent

**/

#include "HdaAudioDxe.h"

/**
  Locate the output stream descriptor and allocate the DMA ring and BDL.

  @param[in] Controller  The controller instance.

  @retval EFI_SUCCESS      The stream is ready.
  @retval EFI_UNSUPPORTED  The controller has no output-capable stream.
  @retval Others           Allocating or mapping the buffer failed.

**/
EFI_STATUS
HdaStreamInit (
  IN HDA_CONTROLLER  *Controller
  )
{
  EFI_STATUS  Status;
  UINT32      Index;
  UINTN       Bytes;
  UINT32      Period;

  if (HDA_GCAP_OSS (Controller->Gcap) > 0) {
    Index                             = HDA_GCAP_ISS (Controller->Gcap);
    Controller->StreamIsBidirectional = FALSE;
  } else if (HDA_GCAP_BSS (Controller->Gcap) > 0) {
    Index                             = HDA_GCAP_ISS (Controller->Gcap);
    Controller->StreamIsBidirectional = TRUE;
  } else {
    DEBUG ((DEBUG_ERROR, "%a: no output stream descriptors\n", __func__));
    return EFI_UNSUPPORTED;
  }

  Controller->StreamBase  = HDA_REG_SD (Index);
  Controller->StreamPages = EFI_SIZE_TO_PAGES (EFI_PAGE_SIZE + HDA_RING_BYTES);

  Status = Controller->PciIo->AllocateBuffer (
                                Controller->PciIo,
                                AllocateAnyPages,
                                EfiBootServicesData,
                                Controller->StreamPages,
                                &Controller->StreamBuffer,
                                0
                                );
  if (EFI_ERROR (Status)) {
    return Status;
  }

  ZeroMem (Controller->StreamBuffer, EFI_PAGES_TO_SIZE (Controller->StreamPages));

  Bytes  = EFI_PAGES_TO_SIZE (Controller->StreamPages);
  Status = Controller->PciIo->Map (
                                Controller->PciIo,
                                EfiPciIoOperationBusMasterCommonBuffer,
                                Controller->StreamBuffer,
                                &Bytes,
                                &Controller->StreamDeviceAddress,
                                &Controller->StreamMapping
                                );
  if (EFI_ERROR (Status) || (Bytes != EFI_PAGES_TO_SIZE (Controller->StreamPages))) {
    if (!EFI_ERROR (Status)) {
      Controller->PciIo->Unmap (Controller->PciIo, Controller->StreamMapping);
      Status = EFI_OUT_OF_RESOURCES;
    }

    Controller->PciIo->FreeBuffer (Controller->PciIo, Controller->StreamPages, Controller->StreamBuffer);
    Controller->StreamBuffer  = NULL;
    Controller->StreamMapping = NULL;
    return Status;
  }

  Controller->Bdl  = (HDA_BDL_ENTRY *)Controller->StreamBuffer;
  Controller->Ring = (UINT8 *)Controller->StreamBuffer + EFI_PAGE_SIZE;

  for (Period = 0; Period < HDA_RING_PERIODS; Period++) {
    Controller->Bdl[Period].Address = Controller->StreamDeviceAddress + EFI_PAGE_SIZE +
                                      (UINT64)Period * HDA_RING_PERIOD_BYTES;
    Controller->Bdl[Period].Length = HDA_RING_PERIOD_BYTES;
    Controller->Bdl[Period].Flags  = 0;
  }

  DEBUG ((
    DEBUG_INFO,
    "%a: stream descriptor %u%a at 0x%x, ring 0x%lx\n",
    __func__,
    Index,
    Controller->StreamIsBidirectional ? " (bidirectional)" : "",
    Controller->StreamBase,
    Controller->StreamDeviceAddress + EFI_PAGE_SIZE
    ));
  return EFI_SUCCESS;
}

/**
  Free the DMA ring and BDL. The stream must be stopped.

  @param[in] Controller  The controller instance.

**/
VOID
HdaStreamFree (
  IN HDA_CONTROLLER  *Controller
  )
{
  if (Controller->StreamMapping != NULL) {
    Controller->PciIo->Unmap (Controller->PciIo, Controller->StreamMapping);
    Controller->StreamMapping = NULL;
  }

  if (Controller->StreamBuffer != NULL) {
    Controller->PciIo->FreeBuffer (Controller->PciIo, Controller->StreamPages, Controller->StreamBuffer);
    Controller->StreamBuffer = NULL;
  }

  Controller->Bdl  = NULL;
  Controller->Ring = NULL;
}

/**
  Stop and reset the stream descriptor.

  @param[in] Controller  The controller instance.

  @retval EFI_SUCCESS       The descriptor is reset and idle.
  @retval EFI_DEVICE_ERROR  The descriptor did not leave reset.

**/
STATIC
EFI_STATUS
HdaStreamReset (
  IN HDA_CONTROLLER  *Controller
  )
{
  UINT32  Ctl;

  Ctl = Controller->StreamBase + HDA_REG_SD_CTL;

  HdaWrite8 (Controller, Ctl, 0);
  HdaWaitReg8 (Controller, Ctl, HDA_SD_CTL_RUN, 0, HDA_RESET_TIMEOUT_US);

  //
  // Section 3.3.35: SRST reads back 1 once the descriptor is in reset, and
  // 0 once it has left reset.
  //
  HdaWrite8 (Controller, Ctl, HDA_SD_CTL_SRST);
  if (EFI_ERROR (HdaWaitReg8 (Controller, Ctl, HDA_SD_CTL_SRST, HDA_SD_CTL_SRST, HDA_RESET_TIMEOUT_US))) {
    DEBUG ((DEBUG_ERROR, "%a: stream did not enter reset\n", __func__));
    return EFI_DEVICE_ERROR;
  }

  HdaWrite8 (Controller, Ctl, 0);
  if (EFI_ERROR (HdaWaitReg8 (Controller, Ctl, HDA_SD_CTL_SRST, 0, HDA_RESET_TIMEOUT_US))) {
    DEBUG ((DEBUG_ERROR, "%a: stream stuck in reset\n", __func__));
    return EFI_DEVICE_ERROR;
  }

  HdaWrite8 (
    Controller,
    Controller->StreamBase + HDA_REG_SD_STS,
    HDA_SD_STS_BCIS | HDA_SD_STS_FIFOE | HDA_SD_STS_DESE
    );
  return EFI_SUCCESS;
}

/**
  Convert source frames into hardware frames. Once the source buffer runs
  out, write silence.

  @param[in] Controller   The controller instance.
  @param[in] Destination  Where to write, inside the ring.
  @param[in] Bytes        Number of bytes to write. Must be a multiple of
                          HDA_HW_FRAME_BYTES.

**/
STATIC
VOID
HdaStreamCopy (
  IN HDA_CONTROLLER  *Controller,
  IN UINT8           *Destination,
  IN UINT32          Bytes
  )
{
  UINT16  *Out;
  UINT32  Frames;
  UINT32  Frame;
  UINTN   FrameBytes;
  INT32   Left;
  INT32   Right;
  INT32   Gain;

  Out        = (UINT16 *)Destination;
  Frames     = Bytes / HDA_HW_FRAME_BYTES;
  FrameBytes = (UINTN)Controller->SourceChannels * sizeof (INT16);
  Gain       = (Controller->PlayingOutput != NULL) ?
               (INT32)Controller->PlayingOutput->SoftwareGain : HDA_SOFTWARE_GAIN_UNITY;

  for (Frame = 0; Frame < Frames; Frame++) {
    if ((Controller->Source != NULL) &&
        (Controller->SourceSize - Controller->SourceOffset >= FrameBytes))
    {
      Left  = (INT16)ReadUnaligned16 ((CONST UINT16 *)(Controller->Source + Controller->SourceOffset));
      Right = (Controller->SourceChannels == 2) ?
              (INT16)ReadUnaligned16 ((CONST UINT16 *)(Controller->Source + Controller->SourceOffset + sizeof (INT16))) :
              Left;
      Controller->SourceOffset += FrameBytes;

      if (Gain != HDA_SOFTWARE_GAIN_UNITY) {
        Left  = (Left * Gain) / HDA_SOFTWARE_GAIN_UNITY;
        Right = (Right * Gain) / HDA_SOFTWARE_GAIN_UNITY;
      }
    } else {
      Left  = 0;
      Right = 0;
    }

    Out[2 * Frame]     = (UINT16)(INT16)Left;
    Out[2 * Frame + 1] = (UINT16)(INT16)Right;
  }
}

/**
  Fill the ring up to the guard distance behind the hardware position.

  @param[in] Controller  The controller instance.

**/
STATIC
VOID
HdaStreamFill (
  IN HDA_CONTROLLER  *Controller
  )
{
  UINT64  Limit;
  UINT32  Offset;
  UINT32  Chunk;

  Limit = Controller->RingBytesPlayed + HDA_RING_BYTES - HDA_RING_GUARD_BYTES;
  while (Controller->RingBytesWritten < Limit) {
    Offset = (UINT32)ModU64x32 (Controller->RingBytesWritten, HDA_RING_BYTES);
    Chunk  = (UINT32)MIN (Limit - Controller->RingBytesWritten, (UINT64)(HDA_RING_BYTES - Offset));
    HdaStreamCopy (Controller, Controller->Ring + Offset, Chunk);
    Controller->RingBytesWritten += Chunk;
  }

  MemoryFence ();
}

/**
  Start playing a buffer through an output. The output must already be
  routed. Called at TPL_NOTIFY.

  @param[in] Output      The output.
  @param[in] Buffer      PCM samples in the output's current mode format.
  @param[in] BufferSize  Size of Buffer in bytes.
  @param[in] Token       Optional completion token.

  @retval EFI_SUCCESS       The stream is running.
  @retval EFI_DEVICE_ERROR  The stream descriptor could not be reset.

**/
EFI_STATUS
HdaStreamStart (
  IN HDA_OUTPUT                *Output,
  IN CONST VOID                *Buffer,
  IN UINTN                     BufferSize,
  IN EDKII_AUDIO_OUTPUT_TOKEN  *Token OPTIONAL
  )
{
  EFI_STATUS      Status;
  UINT8           StreamControl;
  HDA_CONTROLLER  *Controller;

  Controller = Output->Controller;
  Status     = HdaStreamReset (Controller);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Controller->PlayingOutput  = Output;
  Controller->Source         = (CONST UINT8 *)Buffer;
  Controller->SourceSize     = BufferSize;
  Controller->SourceOffset   = 0;
  Controller->SourceChannels = Output->Mode.Format.Channels;
  Controller->RingBytesTotal = MultU64x32 (BufferSize / (Controller->SourceChannels * sizeof (INT16)), HDA_HW_FRAME_BYTES) +
                               HDA_TAIL_BYTES;
  Controller->RingBytesWritten = 0;
  Controller->RingBytesPlayed  = 0;
  Controller->LastLpib         = 0;
  Controller->Token            = Token;
  Controller->PlaybackStatus   = EFI_SUCCESS;

  ZeroMem (Controller->Ring, HDA_RING_BYTES);
  HdaStreamFill (Controller);

  StreamControl = (UINT8)(HDA_STREAM_TAG << HDA_SD_CTL_STRM_SHIFT);
  if (Controller->StreamIsBidirectional) {
    StreamControl |= HDA_SD_CTL_DIR;
  }

  HdaWrite8 (Controller, Controller->StreamBase + HDA_REG_SD_CTL_STRM, StreamControl);
  HdaWrite32 (Controller, Controller->StreamBase + HDA_REG_SD_BDPL, (UINT32)Controller->StreamDeviceAddress);
  HdaWrite32 (Controller, Controller->StreamBase + HDA_REG_SD_BDPU, (UINT32)RShiftU64 (Controller->StreamDeviceAddress, 32));
  HdaWrite32 (Controller, Controller->StreamBase + HDA_REG_SD_CBL, HDA_RING_BYTES);
  HdaWrite16 (Controller, Controller->StreamBase + HDA_REG_SD_LVI, HDA_RING_PERIODS - 1);
  HdaWrite16 (Controller, Controller->StreamBase + HDA_REG_SD_FMT, HDA_HW_FORMAT);

  Controller->Playing = TRUE;
  HdaWrite8 (Controller, Controller->StreamBase + HDA_REG_SD_CTL, HDA_SD_CTL_RUN);
  gBS->SetTimer (Controller->PumpEvent, TimerPeriodic, HDA_PUMP_PERIOD);
  return EFI_SUCCESS;
}

/**
  Stop the stream and complete the pending token. Called at TPL_NOTIFY.
  Does nothing if nothing is playing.

  @param[in] Controller  The controller instance.
  @param[in] Status      Completion status to report.

**/
VOID
HdaStreamFinish (
  IN HDA_CONTROLLER  *Controller,
  IN EFI_STATUS      Status
  )
{
  EDKII_AUDIO_OUTPUT_TOKEN  *Token;

  if (!Controller->Playing) {
    return;
  }

  gBS->SetTimer (Controller->PumpEvent, TimerCancel, 0);
  HdaWrite8 (Controller, Controller->StreamBase + HDA_REG_SD_CTL, 0);
  HdaWaitReg8 (Controller, Controller->StreamBase + HDA_REG_SD_CTL, HDA_SD_CTL_RUN, 0, HDA_RESET_TIMEOUT_US);

  Token                      = Controller->Token;
  Controller->Token          = NULL;
  Controller->Source         = NULL;
  Controller->PlayingOutput  = NULL;
  Controller->PlaybackStatus = Status;
  Controller->Playing        = FALSE;

  if (Token != NULL) {
    Token->TransactionStatus = Status;
    gBS->SignalEvent (Token->Event);
  }
}

/**
  Read how far the hardware has played and refill the ring. Called at
  TPL_NOTIFY.

  @param[in] Controller  The controller instance.

**/
VOID
HdaStreamPump (
  IN HDA_CONTROLLER  *Controller
  )
{
  UINT8   Status;
  UINT32  Lpib;
  UINT32  Delta;

  if (!Controller->Playing) {
    return;
  }

  Status = HdaRead8 (Controller, Controller->StreamBase + HDA_REG_SD_STS);
  if ((Status & HDA_SD_STS_DESE) != 0) {
    DEBUG ((DEBUG_ERROR, "%a: descriptor error, SDnSTS 0x%x\n", __func__, Status));
    HdaWrite8 (Controller, Controller->StreamBase + HDA_REG_SD_STS, Status);
    HdaStreamFinish (Controller, EFI_DEVICE_ERROR);
    return;
  }

  if ((Status & HDA_SD_STS_FIFOE) != 0) {
    //
    // An output FIFO underrun is recoverable. The stream keeps running.
    //
    DEBUG ((DEBUG_WARN, "%a: FIFO error\n", __func__));
    HdaWrite8 (Controller, Controller->StreamBase + HDA_REG_SD_STS, HDA_SD_STS_FIFOE);
  }

  //
  // SDnLPIB counts up to SDnCBL inclusive (Section 3.3.37), so SDnCBL and 0
  // are the same ring position.
  //
  Lpib  = HdaRead32 (Controller, Controller->StreamBase + HDA_REG_SD_LPIB) % HDA_RING_BYTES;
  Delta = (Lpib + HDA_RING_BYTES - Controller->LastLpib) % HDA_RING_BYTES;

  Controller->LastLpib         = Lpib;
  Controller->RingBytesPlayed += Delta;

  if (Controller->RingBytesPlayed >= Controller->RingBytesTotal) {
    HdaStreamFinish (Controller, EFI_SUCCESS);
    return;
  }

  HdaStreamFill (Controller);
}

/**
  Refill timer notification.

  @param[in] Event    The timer event.
  @param[in] Context  The controller instance.

**/
VOID
EFIAPI
HdaStreamPumpNotify (
  IN EFI_EVENT  Event,
  IN VOID       *Context
  )
{
  HdaStreamPump ((HDA_CONTROLLER *)Context);
}
