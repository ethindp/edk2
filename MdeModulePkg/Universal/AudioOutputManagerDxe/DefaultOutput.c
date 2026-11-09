/** @file
  DefaultOutput, the EDKII_SIMPLE_AUDIO_OUTPUT_PROTOCOL instance that plays
  through the active device. Its mode, volume and mute state are applied to
  a device before it plays.

  Copyright (c) 2026, Ethin Probst. All rights reserved.<BR>
  SPDX-License-Identifier: BSD-2-Clause-Patent

**/

#include "AudioOutputManagerDxe.h"

//
// The modes every device must support.
//
STATIC CONST EDKII_AUDIO_OUTPUT_FORMAT  mAomModes[] = {
  { 48000, 2, 16, 0 },  // EDKII_AUDIO_OUTPUT_MODE_STEREO_48K
  { 48000, 1, 16, 0 },  // EDKII_AUDIO_OUTPUT_MODE_MONO_48K
};

/**
  Set the mode, volume and mute state of a device to match DefaultOutput.

  @param[in] Device  The device about to play.

**/
STATIC
VOID
AomPrepareDevice (
  IN AOM_DEVICE  *Device
  )
{
  EDKII_SIMPLE_AUDIO_OUTPUT_PROTOCOL  *Audio;

  Audio = Device->Audio;
  if (Audio->Mode->Mode != gAom.Mode.Mode) {
    Audio->SetMode (Audio, gAom.Mode.Mode);
  }

  if (Audio->Mode->Volume != gAom.Mode.Volume) {
    Audio->SetVolume (Audio, gAom.Mode.Volume);
  }

  if (Audio->Mode->Muted != gAom.Mode.Muted) {
    Audio->SetMute (Audio, gAom.Mode.Muted);
  }
}

/**
  Reset the active device and select mode 0.

  @param[in] This  A pointer to the EDKII_SIMPLE_AUDIO_OUTPUT_PROTOCOL instance.

  @retval EFI_SUCCESS       The device was reset, or there is none.
  @retval EFI_DEVICE_ERROR  The device could not be reset.

**/
STATIC
EFI_STATUS
EFIAPI
AomReset (
  IN EDKII_SIMPLE_AUDIO_OUTPUT_PROTOCOL  *This
  )
{
  EFI_STATUS  Status;

  if (gAom.LastPlayed != NULL) {
    gAom.LastPlayed->Audio->Stop (gAom.LastPlayed->Audio);
  }

  gAom.Mode.Mode = EDKII_AUDIO_OUTPUT_MODE_STEREO_48K;
  CopyMem (&gAom.Mode.Format, &mAomModes[EDKII_AUDIO_OUTPUT_MODE_STEREO_48K], sizeof (gAom.Mode.Format));

  AomSelectDevice ();
  if (gAom.Active == NULL) {
    return EFI_SUCCESS;
  }

  Status = gAom.Active->Audio->Reset (gAom.Active->Audio);
  AomPrepareDevice (gAom.Active);
  return Status;
}

/**
  Return the format of a mode.

  @param[in]  This        A pointer to the EDKII_SIMPLE_AUDIO_OUTPUT_PROTOCOL instance.
  @param[in]  ModeNumber  The mode to query.
  @param[out] Format      On success, the format of ModeNumber.

  @retval EFI_SUCCESS            Format was filled in.
  @retval EFI_INVALID_PARAMETER  ModeNumber is out of range or Format is NULL.

**/
STATIC
EFI_STATUS
EFIAPI
AomQueryMode (
  IN  EDKII_SIMPLE_AUDIO_OUTPUT_PROTOCOL  *This,
  IN  UINT32                              ModeNumber,
  OUT EDKII_AUDIO_OUTPUT_FORMAT           *Format
  )
{
  if ((Format == NULL) || (ModeNumber >= ARRAY_SIZE (mAomModes))) {
    return EFI_INVALID_PARAMETER;
  }

  CopyMem (Format, &mAomModes[ModeNumber], sizeof (*Format));
  return EFI_SUCCESS;
}

/**
  Select the format that subsequent Play() calls use.

  @param[in] This        A pointer to the EDKII_SIMPLE_AUDIO_OUTPUT_PROTOCOL instance.
  @param[in] ModeNumber  The mode to select.

  @retval EFI_SUCCESS            The mode was selected.
  @retval EFI_INVALID_PARAMETER  ModeNumber is out of range.
  @retval EFI_NOT_READY          Playback started through DefaultOutput is in
                                 progress.

**/
STATIC
EFI_STATUS
EFIAPI
AomSetMode (
  IN EDKII_SIMPLE_AUDIO_OUTPUT_PROTOCOL  *This,
  IN UINT32                              ModeNumber
  )
{
  EFI_STATUS  Status;

  if (ModeNumber >= ARRAY_SIZE (mAomModes)) {
    return EFI_INVALID_PARAMETER;
  }

  //
  // The device that is playing decides whether the mode can be changed.
  //
  if (gAom.LastPlayed != NULL) {
    Status = gAom.LastPlayed->Audio->SetMode (gAom.LastPlayed->Audio, ModeNumber);
    if (Status == EFI_NOT_READY) {
      return Status;
    }
  }

  gAom.Mode.Mode = ModeNumber;
  CopyMem (&gAom.Mode.Format, &mAomModes[ModeNumber], sizeof (gAom.Mode.Format));
  return EFI_SUCCESS;
}

/**
  Play a buffer through the active device.

  @param[in]      This        A pointer to the EDKII_SIMPLE_AUDIO_OUTPUT_PROTOCOL instance.
  @param[in]      Buffer      The PCM samples to play.
  @param[in]      BufferSize  Size of Buffer in bytes.
  @param[in, out] Token       Optional token for asynchronous playback.

  @retval EFI_NO_MEDIA  No audio output device is available.
  @retval Others        As for EDKII_SIMPLE_AUDIO_OUTPUT_PROTOCOL.Play().

**/
STATIC
EFI_STATUS
EFIAPI
AomPlay (
  IN     EDKII_SIMPLE_AUDIO_OUTPUT_PROTOCOL  *This,
  IN     CONST VOID                          *Buffer,
  IN     UINTN                               BufferSize,
  IN OUT EDKII_AUDIO_OUTPUT_TOKEN            *Token  OPTIONAL
  )
{
  EFI_STATUS  Status;
  AOM_DEVICE  *Device;
  UINTN       FrameBytes;
  UINTN       Attempt;

  FrameBytes = (UINTN)gAom.Mode.Format.Channels * (gAom.Mode.Format.BitsPerSample / 8);
  if ((Buffer == NULL) || (BufferSize == 0) || ((BufferSize % FrameBytes) != 0) ||
      ((Token != NULL) && (Token->Event == NULL)))
  {
    return EFI_INVALID_PARAMETER;
  }

  Status = EFI_NO_MEDIA;
  for (Attempt = 0; Attempt < 2; Attempt++) {
    AomSelectDevice ();
    Device = gAom.Active;
    if (Device == NULL) {
      return EFI_NO_MEDIA;
    }

    AomPrepareDevice (Device);
    Status = Device->Audio->Play (Device->Audio, Buffer, BufferSize, Token);
    if (Status != EFI_DEVICE_ERROR) {
      if (!EFI_ERROR (Status) || (Status == EFI_ABORTED)) {
        gAom.LastPlayed = Device;
      }

      return Status;
    }

    DEBUG ((DEBUG_WARN, "%a: device %p failed; trying another\n", __func__, Device->Handle));
    Device->Failed = TRUE;
  }

  return Status;
}

/**
  Stop playback started through DefaultOutput.

  @param[in] This  A pointer to the EDKII_SIMPLE_AUDIO_OUTPUT_PROTOCOL instance.

  @retval EFI_SUCCESS  Playback is stopped.

**/
STATIC
EFI_STATUS
EFIAPI
AomStop (
  IN EDKII_SIMPLE_AUDIO_OUTPUT_PROTOCOL  *This
  )
{
  if (gAom.LastPlayed != NULL) {
    return gAom.LastPlayed->Audio->Stop (gAom.LastPlayed->Audio);
  }

  return EFI_SUCCESS;
}

/**
  Set and save the volume, and apply it to the active device.

  @param[in] This    A pointer to the EDKII_SIMPLE_AUDIO_OUTPUT_PROTOCOL instance.
  @param[in] Volume  0 (silent) to 100 (maximum).

  @retval EFI_SUCCESS            The volume was set.
  @retval EFI_INVALID_PARAMETER  Volume is greater than 100.
  @retval EFI_DEVICE_ERROR       The active device could not be configured.

**/
STATIC
EFI_STATUS
EFIAPI
AomSetVolume (
  IN EDKII_SIMPLE_AUDIO_OUTPUT_PROTOCOL  *This,
  IN UINT8                               Volume
  )
{
  if (Volume > 100) {
    return EFI_INVALID_PARAMETER;
  }

  gAom.Mode.Volume = Volume;
  AomSaveSettings ();
  if (gAom.Active != NULL) {
    return gAom.Active->Audio->SetVolume (gAom.Active->Audio, Volume);
  }

  return EFI_SUCCESS;
}

/**
  Set and save the mute state, and apply it to the active device.

  @param[in] This  A pointer to the EDKII_SIMPLE_AUDIO_OUTPUT_PROTOCOL instance.
  @param[in] Mute  TRUE to mute, FALSE to unmute.

  @retval EFI_SUCCESS       The mute state was set.
  @retval EFI_DEVICE_ERROR  The active device could not be configured.

**/
STATIC
EFI_STATUS
EFIAPI
AomSetMute (
  IN EDKII_SIMPLE_AUDIO_OUTPUT_PROTOCOL  *This,
  IN BOOLEAN                             Mute
  )
{
  gAom.Mode.Muted = Mute;
  AomSaveSettings ();
  if (gAom.Active != NULL) {
    return gAom.Active->Audio->SetMute (gAom.Active->Audio, Mute);
  }

  return EFI_SUCCESS;
}

/**
  Initialize DefaultOutput's protocol instance and mode. Volume and Muted
  must already be loaded.

**/
VOID
AomInitDefaultOutput (
  VOID
  )
{
  gAom.Audio.Revision  = EDKII_SIMPLE_AUDIO_OUTPUT_PROTOCOL_REVISION;
  gAom.Audio.Reset     = AomReset;
  gAom.Audio.QueryMode = AomQueryMode;
  gAom.Audio.SetMode   = AomSetMode;
  gAom.Audio.Play      = AomPlay;
  gAom.Audio.Stop      = AomStop;
  gAom.Audio.SetVolume = AomSetVolume;
  gAom.Audio.SetMute   = AomSetMute;
  gAom.Audio.Mode      = &gAom.Mode;

  gAom.Mode.MaxMode = ARRAY_SIZE (mAomModes);
  gAom.Mode.Mode    = EDKII_AUDIO_OUTPUT_MODE_STEREO_48K;
  CopyMem (&gAom.Mode.Format, &mAomModes[EDKII_AUDIO_OUTPUT_MODE_STEREO_48K], sizeof (gAom.Mode.Format));
  gAom.Mode.OutputType = EDKII_AUDIO_OUTPUT_TYPE_UNKNOWN;
  gAom.Mode.Presence   = EDKII_AUDIO_OUTPUT_PRESENCE_DISCONNECTED;
}
