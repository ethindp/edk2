/** @file
  EDKII_SIMPLE_AUDIO_OUTPUT_PROTOCOL implementation for HD Audio outputs.

  Copyright (c) 2026, Ethin Probst. All rights reserved.<BR>
  SPDX-License-Identifier: BSD-2-Clause-Patent

**/

#include "HdaAudioDxe.h"

STATIC CONST EDKII_AUDIO_OUTPUT_FORMAT  mHdaModes[] = {
  { 48000, 2, 16, 0 },  // EDKII_AUDIO_OUTPUT_MODE_STEREO_48K
  { 48000, 1, 16, 0 },  // EDKII_AUDIO_OUTPUT_MODE_MONO_48K
};

/**
  Reset the audio output device.

  @param[in] This  A pointer to the EDKII_SIMPLE_AUDIO_OUTPUT_PROTOCOL instance.

  @retval EFI_SUCCESS       The device was reset.
  @retval EFI_DEVICE_ERROR  The device could not be reset.

**/
STATIC
EFI_STATUS
EFIAPI
HdaAudioReset (
  IN EDKII_SIMPLE_AUDIO_OUTPUT_PROTOCOL  *This
  )
{
  HDA_OUTPUT      *Output;
  HDA_CONTROLLER  *Controller;
  EFI_TPL         OldTpl;

  Output     = HDA_OUTPUT_FROM_AUDIO (This);
  Controller = Output->Controller;

  OldTpl = gBS->RaiseTPL (TPL_NOTIFY);
  if (Controller->PlayingOutput == Output) {
    HdaStreamFinish (Controller, EFI_ABORTED);
  }

  Output->Mode.Mode = EDKII_AUDIO_OUTPUT_MODE_STEREO_48K;
  CopyMem (&Output->Mode.Format, &mHdaModes[EDKII_AUDIO_OUTPUT_MODE_STEREO_48K], sizeof (Output->Mode.Format));
  gBS->RestoreTPL (OldTpl);

  //
  // Force the path to be reprogrammed in case something else touched the
  // codec.
  //
  if (Controller->RoutedOutput == Output) {
    Controller->RoutedOutput = NULL;
    HdaCodecRouteOutput (Output);
  }

  return EFI_ERROR (HdaCodecApplyVolume (Output)) ? EFI_DEVICE_ERROR : EFI_SUCCESS;
}

/**
  Return the format of a mode.

  @param[in]  This        A pointer to the EDKII_SIMPLE_AUDIO_OUTPUT_PROTOCOL instance.
  @param[in]  ModeNumber  The mode to query.
  @param[out] Format      On success, the format of ModeNumber.

  @retval EFI_SUCCESS            Format was filled in.
  @retval EFI_INVALID_PARAMETER  ModeNumber is not less than Mode->MaxMode, or
                                 Format is NULL.

**/
STATIC
EFI_STATUS
EFIAPI
HdaAudioQueryMode (
  IN  EDKII_SIMPLE_AUDIO_OUTPUT_PROTOCOL  *This,
  IN  UINT32                              ModeNumber,
  OUT EDKII_AUDIO_OUTPUT_FORMAT           *Format
  )
{
  if ((Format == NULL) || (ModeNumber >= ARRAY_SIZE (mHdaModes))) {
    return EFI_INVALID_PARAMETER;
  }

  CopyMem (Format, &mHdaModes[ModeNumber], sizeof (*Format));
  return EFI_SUCCESS;
}

/**
  Select the format that subsequent Play() calls use.

  @param[in] This        A pointer to the EDKII_SIMPLE_AUDIO_OUTPUT_PROTOCOL instance.
  @param[in] ModeNumber  The mode to select.

  @retval EFI_SUCCESS            The mode was selected.
  @retval EFI_INVALID_PARAMETER  ModeNumber is not less than Mode->MaxMode.
  @retval EFI_NOT_READY          This output is playing.

**/
STATIC
EFI_STATUS
EFIAPI
HdaAudioSetMode (
  IN EDKII_SIMPLE_AUDIO_OUTPUT_PROTOCOL  *This,
  IN UINT32                              ModeNumber
  )
{
  HDA_OUTPUT  *Output;
  EFI_TPL     OldTpl;
  EFI_STATUS  Status;

  if (ModeNumber >= ARRAY_SIZE (mHdaModes)) {
    return EFI_INVALID_PARAMETER;
  }

  Output = HDA_OUTPUT_FROM_AUDIO (This);

  OldTpl = gBS->RaiseTPL (TPL_NOTIFY);
  if (Output->Controller->PlayingOutput == Output) {
    Status = EFI_NOT_READY;
  } else {
    Output->Mode.Mode = ModeNumber;
    CopyMem (&Output->Mode.Format, &mHdaModes[ModeNumber], sizeof (Output->Mode.Format));
    Status = EFI_SUCCESS;
  }

  gBS->RestoreTPL (OldTpl);
  return Status;
}

/**
  Play a buffer of PCM samples in the current mode's format.

  @param[in]      This        A pointer to the EDKII_SIMPLE_AUDIO_OUTPUT_PROTOCOL instance.
  @param[in]      Buffer      The PCM samples to play.
  @param[in]      BufferSize  Size of Buffer in bytes.
  @param[in, out] Token       Optional token for asynchronous playback.

  @retval EFI_SUCCESS            The buffer was played if Token is NULL.
                                 Playback was started if Token is not NULL.
  @retval EFI_INVALID_PARAMETER  A parameter is invalid.
  @retval EFI_NOT_READY          This or another output of the controller is
                                 playing.
  @retval EFI_ABORTED            Token is NULL and playback was stopped.
  @retval EFI_DEVICE_ERROR       The device reported an error.

**/
STATIC
EFI_STATUS
EFIAPI
HdaAudioPlay (
  IN     EDKII_SIMPLE_AUDIO_OUTPUT_PROTOCOL  *This,
  IN     CONST VOID                          *Buffer,
  IN     UINTN                               BufferSize,
  IN OUT EDKII_AUDIO_OUTPUT_TOKEN            *Token  OPTIONAL
  )
{
  HDA_OUTPUT      *Output;
  HDA_CONTROLLER  *Controller;
  EFI_TPL         OldTpl;
  EFI_STATUS      Status;
  UINTN           FrameBytes;
  BOOLEAN         Playing;

  if ((Buffer == NULL) || ((Token != NULL) && (Token->Event == NULL))) {
    return EFI_INVALID_PARAMETER;
  }

  Output     = HDA_OUTPUT_FROM_AUDIO (This);
  Controller = Output->Controller;
  FrameBytes = (UINTN)Output->Mode.Format.Channels * (Output->Mode.Format.BitsPerSample / 8);
  if ((BufferSize == 0) || ((BufferSize % FrameBytes) != 0)) {
    return EFI_INVALID_PARAMETER;
  }

  //
  // Routing sends verbs, so we do it before raising the TPL.
  //
  if (Controller->Playing) {
    return EFI_NOT_READY;
  }

  HdaCodecRouteOutput (Output);

  OldTpl = gBS->RaiseTPL (TPL_NOTIFY);
  if (Controller->Playing) {
    gBS->RestoreTPL (OldTpl);
    return EFI_NOT_READY;
  }

  Status = HdaStreamStart (Output, Buffer, BufferSize, Token);
  gBS->RestoreTPL (OldTpl);
  if (EFI_ERROR (Status) || (Token != NULL)) {
    return Status;
  }

  //
  // Blocking playback. Refill the ring here instead of in the timer, which
  // does not run if the caller is at TPL_NOTIFY or above.
  //
  do {
    gBS->Stall (HDA_BLOCKING_POLL_US);
    OldTpl = gBS->RaiseTPL (TPL_NOTIFY);
    HdaStreamPump (Controller);
    Playing = Controller->Playing;
    Status  = Controller->PlaybackStatus;
    gBS->RestoreTPL (OldTpl);
  } while (Playing);

  return Status;
}

/**
  Stop playback in progress on this output.

  @param[in] This  A pointer to the EDKII_SIMPLE_AUDIO_OUTPUT_PROTOCOL instance.

  @retval EFI_SUCCESS  Playback is stopped.

**/
STATIC
EFI_STATUS
EFIAPI
HdaAudioStop (
  IN EDKII_SIMPLE_AUDIO_OUTPUT_PROTOCOL  *This
  )
{
  HDA_OUTPUT  *Output;
  EFI_TPL     OldTpl;

  Output = HDA_OUTPUT_FROM_AUDIO (This);

  OldTpl = gBS->RaiseTPL (TPL_NOTIFY);
  if (Output->Controller->PlayingOutput == Output) {
    HdaStreamFinish (Output->Controller, EFI_ABORTED);
  }

  gBS->RestoreTPL (OldTpl);
  return EFI_SUCCESS;
}

/**
  Set the output volume.

  @param[in] This    A pointer to the EDKII_SIMPLE_AUDIO_OUTPUT_PROTOCOL instance.
  @param[in] Volume  0 (silent) to 100 (maximum).

  @retval EFI_SUCCESS            The volume was set.
  @retval EFI_INVALID_PARAMETER  Volume is greater than 100.
  @retval EFI_DEVICE_ERROR       The device could not be configured.

**/
STATIC
EFI_STATUS
EFIAPI
HdaAudioSetVolume (
  IN EDKII_SIMPLE_AUDIO_OUTPUT_PROTOCOL  *This,
  IN UINT8                               Volume
  )
{
  HDA_OUTPUT  *Output;

  if (Volume > 100) {
    return EFI_INVALID_PARAMETER;
  }

  Output              = HDA_OUTPUT_FROM_AUDIO (This);
  Output->Mode.Volume = Volume;
  return HdaCodecApplyVolume (Output);
}

/**
  Mute or unmute the output.

  @param[in] This  A pointer to the EDKII_SIMPLE_AUDIO_OUTPUT_PROTOCOL instance.
  @param[in] Mute  TRUE to mute, FALSE to unmute.

  @retval EFI_SUCCESS       The mute state was set.
  @retval EFI_DEVICE_ERROR  The device could not be configured.

**/
STATIC
EFI_STATUS
EFIAPI
HdaAudioSetMute (
  IN EDKII_SIMPLE_AUDIO_OUTPUT_PROTOCOL  *This,
  IN BOOLEAN                             Mute
  )
{
  HDA_OUTPUT  *Output;

  Output             = HDA_OUTPUT_FROM_AUDIO (This);
  Output->Mode.Muted = Mute;
  return HdaCodecApplyVolume (Output);
}

/**
  Initialize the protocol instance and mode of an output. OutputType must
  already be set.

  @param[in] Output  The output.

**/
VOID
HdaAudioInitProtocol (
  IN HDA_OUTPUT  *Output
  )
{
  Output->Audio.Revision  = EDKII_SIMPLE_AUDIO_OUTPUT_PROTOCOL_REVISION;
  Output->Audio.Reset     = HdaAudioReset;
  Output->Audio.QueryMode = HdaAudioQueryMode;
  Output->Audio.SetMode   = HdaAudioSetMode;
  Output->Audio.Play      = HdaAudioPlay;
  Output->Audio.Stop      = HdaAudioStop;
  Output->Audio.SetVolume = HdaAudioSetVolume;
  Output->Audio.SetMute   = HdaAudioSetMute;
  Output->Audio.Mode      = &Output->Mode;

  Output->Mode.MaxMode = ARRAY_SIZE (mHdaModes);
  Output->Mode.Mode    = EDKII_AUDIO_OUTPUT_MODE_STEREO_48K;
  CopyMem (&Output->Mode.Format, &mHdaModes[EDKII_AUDIO_OUTPUT_MODE_STEREO_48K], sizeof (Output->Mode.Format));
  Output->Mode.Volume = HDA_DEFAULT_VOLUME;
  Output->Mode.Muted  = FALSE;
  HdaCodecUpdatePresence (Output);
  HdaCodecApplyVolume (Output);
}
