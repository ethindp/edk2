/** @file
  This file defines the EDK II Audio Output Manager Protocol, which selects
  the audio output device to use and remembers the user's preferred device.

  Copyright (c) 2026, Ethin Probst. All rights reserved.<BR>
  SPDX-License-Identifier: BSD-2-Clause-Patent

**/

#pragma once

#include <Protocol/DevicePath.h>
#include <Protocol/SimpleAudioOutput.h>

#define EDKII_AUDIO_OUTPUT_MANAGER_PROTOCOL_GUID \
  { \
    0xc1ab7e5c, 0x0c6f, 0x49e1, { 0x9b, 0xc3, 0xc9, 0xdc, 0x8d, 0x44, 0x76, 0x4b } \
  }

#define EDKII_AUDIO_OUTPUT_MANAGER_PROTOCOL_REVISION  0x00010000

typedef struct _EDKII_AUDIO_OUTPUT_MANAGER_PROTOCOL EDKII_AUDIO_OUTPUT_MANAGER_PROTOCOL;

/**
  Return the audio output devices the manager knows about.

  DefaultOutput itself is not included.

  @param[in]  This     A pointer to the EDKII_AUDIO_OUTPUT_MANAGER_PROTOCOL instance.
  @param[out] Count    Number of handles returned.
  @param[out] Handles  Pool-allocated array of device handles, each carrying
                       EDKII_SIMPLE_AUDIO_OUTPUT_PROTOCOL. The caller frees
                       it with FreePool(). NULL when Count is 0.

  @retval EFI_SUCCESS            The list was returned.
  @retval EFI_INVALID_PARAMETER  Count or Handles is NULL.
  @retval EFI_OUT_OF_RESOURCES   The list could not be allocated.

**/
typedef
EFI_STATUS
(EFIAPI *EDKII_AUDIO_OUTPUT_MANAGER_GET_DEVICES)(
  IN  EDKII_AUDIO_OUTPUT_MANAGER_PROTOCOL  *This,
  OUT UINTN                                *Count,
  OUT EFI_HANDLE                           **Handles
  );

/**
  Return the device DefaultOutput currently plays through.

  @param[in]  This    A pointer to the EDKII_AUDIO_OUTPUT_MANAGER_PROTOCOL instance.
  @param[out] Handle  The active device.

  @retval EFI_SUCCESS            Handle was returned.
  @retval EFI_INVALID_PARAMETER  Handle is NULL.
  @retval EFI_NO_MEDIA           No audio output device is available.

**/
typedef
EFI_STATUS
(EFIAPI *EDKII_AUDIO_OUTPUT_MANAGER_GET_ACTIVE_DEVICE)(
  IN  EDKII_AUDIO_OUTPUT_MANAGER_PROTOCOL  *This,
  OUT EFI_HANDLE                           *Handle
  );

/**
  Choose the device DefaultOutput should use, and remember the choice
  across boots by its device path.

  If playback is in progress, the choice takes effect at the next Play().

  @param[in] This    A pointer to the EDKII_AUDIO_OUTPUT_MANAGER_PROTOCOL instance.
  @param[in] Handle  A handle returned by GetDevices(), or NULL to forget the
                     preference and go back to automatic selection.

  @retval EFI_SUCCESS      The preference was set and saved.
  @retval EFI_NOT_FOUND    Handle is not a known device.
  @retval EFI_UNSUPPORTED  The device has no device path.
  @retval Others           The preference could not be saved.

**/
typedef
EFI_STATUS
(EFIAPI *EDKII_AUDIO_OUTPUT_MANAGER_SET_PREFERRED_DEVICE)(
  IN EDKII_AUDIO_OUTPUT_MANAGER_PROTOCOL  *This,
  IN EFI_HANDLE                           Handle OPTIONAL
  );

/**
  Return the saved preferred device, whether or not it is present.

  @param[in]  This        A pointer to the EDKII_AUDIO_OUTPUT_MANAGER_PROTOCOL instance.
  @param[out] DevicePath  Pool-allocated copy of the preferred device's
                          device path. The caller frees it with FreePool().

  @retval EFI_SUCCESS            DevicePath was returned.
  @retval EFI_INVALID_PARAMETER  DevicePath is NULL.
  @retval EFI_NOT_FOUND          No preference is saved.
  @retval EFI_OUT_OF_RESOURCES   The copy could not be allocated.

**/
typedef
EFI_STATUS
(EFIAPI *EDKII_AUDIO_OUTPUT_MANAGER_GET_PREFERRED_DEVICE)(
  IN  EDKII_AUDIO_OUTPUT_MANAGER_PROTOCOL  *This,
  OUT EFI_DEVICE_PATH_PROTOCOL             **DevicePath
  );

///
/// The EDK II Audio Output Manager Protocol. There is at most one instance.
///
struct _EDKII_AUDIO_OUTPUT_MANAGER_PROTOCOL {
  ///
  /// EDKII_AUDIO_OUTPUT_MANAGER_PROTOCOL_REVISION.
  ///
  UINT64    Revision;
  ///
  /// An EDKII_SIMPLE_AUDIO_OUTPUT_PROTOCOL that plays through the active
  /// device. The active device is the preferred device if it is present,
  /// and otherwise the best available device. If the active device fails,
  /// another device becomes active.
  ///
  /// Mode reports the OutputType and Presence of the active device, and the
  /// saved Volume and Muted state, which apply to every device. Play()
  /// returns EFI_NO_MEDIA if no device is available.
  ///
  EDKII_SIMPLE_AUDIO_OUTPUT_PROTOCOL                 *DefaultOutput;
  EDKII_AUDIO_OUTPUT_MANAGER_GET_DEVICES             GetDevices;
  EDKII_AUDIO_OUTPUT_MANAGER_GET_ACTIVE_DEVICE       GetActiveDevice;
  EDKII_AUDIO_OUTPUT_MANAGER_SET_PREFERRED_DEVICE    SetPreferredDevice;
  EDKII_AUDIO_OUTPUT_MANAGER_GET_PREFERRED_DEVICE    GetPreferredDevice;
};

extern EFI_GUID  gEdkiiAudioOutputManagerProtocolGuid;
