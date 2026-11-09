/** @file
  Audio output device selection and saved settings.

  Copyright (c) 2026, Ethin Probst. All rights reserved.<BR>
  SPDX-License-Identifier: BSD-2-Clause-Patent

**/

#include "AudioOutputManagerDxe.h"

//
// Device scores. A device that reports nothing connected can still be
// used, because presence detection can be wrong.
//
#define AOM_SCORE_PREFERRED             100
#define AOM_SCORE_HEADPHONES_CONNECTED  60
#define AOM_SCORE_SPEAKER               50
#define AOM_SCORE_LINE_OUT              40
#define AOM_SCORE_HEADPHONES_UNKNOWN    30
#define AOM_SCORE_OTHER                 20
#define AOM_SCORE_DISCONNECTED          10

/**
  Find the device record for a handle.

  @param[in] Handle  A device handle.

  @return The record, or NULL.

**/
AOM_DEVICE *
AomFindDevice (
  IN EFI_HANDLE  Handle
  )
{
  LIST_ENTRY  *Link;
  AOM_DEVICE  *Device;

  for (Link = GetFirstNode (&gAom.Devices); !IsNull (&gAom.Devices, Link); Link = GetNextNode (&gAom.Devices, Link)) {
    Device = AOM_DEVICE_FROM_LINK (Link);
    if (Device->Handle == Handle) {
      return Device;
    }
  }

  return NULL;
}

/**
  Check whether a device is the saved preference.

  @param[in] Device  The device.

  @retval TRUE   Its device path equals the preferred one.
  @retval FALSE  It does not, or there is no preference.

**/
STATIC
BOOLEAN
AomIsPreferred (
  IN AOM_DEVICE  *Device
  )
{
  UINTN  Size;

  if ((gAom.Preferred == NULL) || (Device->DevicePath == NULL)) {
    return FALSE;
  }

  Size = GetDevicePathSize (gAom.Preferred);
  return (BOOLEAN)((GetDevicePathSize (Device->DevicePath) == Size) &&
                   (CompareMem (Device->DevicePath, gAom.Preferred, Size) == 0));
}

/**
  Rank a device.

  @param[in] Device  The device.

  @return AOM_SCORE_* value; higher is better.

**/
STATIC
UINTN
AomScore (
  IN AOM_DEVICE  *Device
  )
{
  EDKII_SIMPLE_AUDIO_OUTPUT_MODE  *Mode;

  Mode = Device->Audio->Mode;
  if (Mode->Presence == EDKII_AUDIO_OUTPUT_PRESENCE_DISCONNECTED) {
    return AOM_SCORE_DISCONNECTED;
  }

  if (AomIsPreferred (Device)) {
    return AOM_SCORE_PREFERRED;
  }

  switch (Mode->OutputType) {
    case EDKII_AUDIO_OUTPUT_TYPE_HEADPHONES:
      return (Mode->Presence == EDKII_AUDIO_OUTPUT_PRESENCE_CONNECTED) ?
             AOM_SCORE_HEADPHONES_CONNECTED : AOM_SCORE_HEADPHONES_UNKNOWN;
    case EDKII_AUDIO_OUTPUT_TYPE_SPEAKER:
      return AOM_SCORE_SPEAKER;
    case EDKII_AUDIO_OUTPUT_TYPE_LINE_OUT:
      return AOM_SCORE_LINE_OUT;
    default:
      return AOM_SCORE_OTHER;
  }
}

/**
  Find the best device that has not failed.

  @return The device, or NULL if every device has failed or there are none.

**/
STATIC
AOM_DEVICE *
AomBestDevice (
  VOID
  )
{
  LIST_ENTRY  *Link;
  AOM_DEVICE  *Device;
  AOM_DEVICE  *Best;
  UINTN       Score;
  UINTN       BestScore;

  Best      = NULL;
  BestScore = 0;
  for (Link = GetFirstNode (&gAom.Devices); !IsNull (&gAom.Devices, Link); Link = GetNextNode (&gAom.Devices, Link)) {
    Device = AOM_DEVICE_FROM_LINK (Link);
    if (Device->Failed) {
      continue;
    }

    //
    // If two devices have the same score, keep the first one.
    //
    Score = AomScore (Device);
    if (Score > BestScore) {
      Best      = Device;
      BestScore = Score;
    }
  }

  return Best;
}

/**
  Choose the active device from the preference and policy, and copy its
  OutputType and Presence to the mode of DefaultOutput.

**/
VOID
AomSelectDevice (
  VOID
  )
{
  AOM_DEVICE  *Best;
  AOM_DEVICE  *Device;
  LIST_ENTRY  *Link;

  Best = AomBestDevice ();
  if ((Best == NULL) && !IsListEmpty (&gAom.Devices)) {
    //
    // Every device has failed. Try them all again, in case the failures
    // were transient.
    //
    for (Link = GetFirstNode (&gAom.Devices); !IsNull (&gAom.Devices, Link); Link = GetNextNode (&gAom.Devices, Link)) {
      Device         = AOM_DEVICE_FROM_LINK (Link);
      Device->Failed = FALSE;
    }

    Best = AomBestDevice ();
  }

  if (Best != gAom.Active) {
    DEBUG ((
      DEBUG_INFO,
      "%a: active device changed from %p to %p%a\n",
      __func__,
      (gAom.Active != NULL) ? gAom.Active->Handle : NULL,
      (Best != NULL) ? Best->Handle : NULL,
      ((Best != NULL) && AomIsPreferred (Best)) ? " (preferred)" : ""
      ));
    gAom.Active = Best;
  }

  if (Best != NULL) {
    gAom.Mode.OutputType = Best->Audio->Mode->OutputType;
    gAom.Mode.Presence   = Best->Audio->Mode->Presence;
  } else {
    gAom.Mode.OutputType = EDKII_AUDIO_OUTPUT_TYPE_UNKNOWN;
    gAom.Mode.Presence   = EDKII_AUDIO_OUTPUT_PRESENCE_DISCONNECTED;
  }
}

/**
  Periodic check for presence changes.

  @param[in] Event    The timer event.
  @param[in] Context  Unused.

**/
VOID
EFIAPI
AomPollNotify (
  IN EFI_EVENT  Event,
  IN VOID       *Context
  )
{
  LIST_ENTRY  *Link;
  AOM_DEVICE  *Device;
  UINT8       Presence;
  BOOLEAN     Changed;

  Changed = FALSE;
  for (Link = GetFirstNode (&gAom.Devices); !IsNull (&gAom.Devices, Link); Link = GetNextNode (&gAom.Devices, Link)) {
    Device   = AOM_DEVICE_FROM_LINK (Link);
    Presence = Device->Audio->Mode->Presence;
    if (Presence != Device->LastPresence) {
      Device->LastPresence = Presence;
      if (Presence == EDKII_AUDIO_OUTPUT_PRESENCE_CONNECTED) {
        //
        // Something was plugged in. Try the device again.
        //
        Device->Failed = FALSE;
      }

      Changed = TRUE;
    }
  }

  if (Changed) {
    AomSelectDevice ();
  }
}

/**
  Load the saved volume, mute state, and preferred device into gAom.
  Missing or malformed settings leave the defaults in place.

**/
VOID
AomLoadSettings (
  VOID
  )
{
  EFI_STATUS                   Status;
  EDKII_AUDIO_OUTPUT_SETTINGS  *Settings;
  UINTN                        Size;
  EFI_DEVICE_PATH_PROTOCOL     *DevicePath;

  Status = GetVariable2 (
             EDKII_AUDIO_OUTPUT_SETTINGS_VARIABLE_NAME,
             &gEdkiiAudioOutputSettingsVariableGuid,
             (VOID **)&Settings,
             &Size
             );
  if (EFI_ERROR (Status)) {
    return;
  }

  if ((Size < sizeof (*Settings)) ||
      (Settings->Version != EDKII_AUDIO_OUTPUT_SETTINGS_VERSION) ||
      (Settings->Volume > 100))
  {
    DEBUG ((DEBUG_WARN, "%a: ignoring malformed settings (%u bytes)\n", __func__, Size));
    FreePool (Settings);
    return;
  }

  gAom.Mode.Volume = Settings->Volume;
  gAom.Mode.Muted  = (BOOLEAN)(Settings->Muted != 0);

  if (Size > sizeof (*Settings)) {
    DevicePath = (EFI_DEVICE_PATH_PROTOCOL *)(Settings + 1);
    if (IsDevicePathValid (DevicePath, Size - sizeof (*Settings))) {
      gAom.Preferred = DuplicateDevicePath (DevicePath);
    } else {
      DEBUG ((DEBUG_WARN, "%a: ignoring malformed preferred device\n", __func__));
    }
  }

  FreePool (Settings);
}

/**
  Save gAom's volume, mute state, and preferred device.

  @retval EFI_SUCCESS           The settings were saved.
  @retval EFI_OUT_OF_RESOURCES  The buffer could not be allocated.
  @retval Others                The variable could not be written.

**/
EFI_STATUS
AomSaveSettings (
  VOID
  )
{
  EFI_STATUS                   Status;
  EDKII_AUDIO_OUTPUT_SETTINGS  *Settings;
  UINTN                        PathSize;
  UINTN                        Size;

  PathSize = (gAom.Preferred != NULL) ? GetDevicePathSize (gAom.Preferred) : 0;
  Size     = sizeof (*Settings) + PathSize;
  Settings = AllocateZeroPool (Size);
  if (Settings == NULL) {
    return EFI_OUT_OF_RESOURCES;
  }

  Settings->Version = EDKII_AUDIO_OUTPUT_SETTINGS_VERSION;
  Settings->Volume  = gAom.Mode.Volume;
  Settings->Muted   = gAom.Mode.Muted ? 1 : 0;
  if (PathSize != 0) {
    CopyMem (Settings + 1, gAom.Preferred, PathSize);
  }

  Status = gRT->SetVariable (
                  EDKII_AUDIO_OUTPUT_SETTINGS_VARIABLE_NAME,
                  &gEdkiiAudioOutputSettingsVariableGuid,
                  EDKII_AUDIO_OUTPUT_SETTINGS_VARIABLE_ATTRIBUTES,
                  Size,
                  Settings
                  );
  FreePool (Settings);
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "%a: %r\n", __func__, Status));
  }

  return Status;
}
