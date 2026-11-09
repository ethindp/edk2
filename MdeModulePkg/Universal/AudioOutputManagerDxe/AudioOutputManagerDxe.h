/** @file
  Header file for the audio output manager.

  Copyright (c) 2026, Ethin Probst. All rights reserved.<BR>
  SPDX-License-Identifier: BSD-2-Clause-Patent

**/

#pragma once

#include <Uefi.h>

#include <Guid/AudioOutputSettings.h>

#include <Protocol/AudioOutputManager.h>
#include <Protocol/ComponentName.h>
#include <Protocol/ComponentName2.h>
#include <Protocol/DevicePath.h>
#include <Protocol/DriverBinding.h>
#include <Protocol/SimpleAudioOutput.h>

#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/DevicePathLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiLib.h>
#include <Library/UefiRuntimeServicesTableLib.h>

#define AOM_DEVICE_SIGNATURE  SIGNATURE_32 ('A', 'O', 'M', 'D')

//
// Volume used when no settings are saved.
//
#define AOM_DEFAULT_VOLUME  80

//
// How often device presence is re-examined, in 100 ns units (500 ms).
//
#define AOM_POLL_PERIOD  5000000

///
/// One audio output device the manager is bound to.
///
typedef struct {
  UINT32                                Signature;
  LIST_ENTRY                            Link;
  EFI_HANDLE                            Handle;
  EDKII_SIMPLE_AUDIO_OUTPUT_PROTOCOL    *Audio;
  ///
  /// Pool copy of the device's device path, or NULL if it has none.
  ///
  EFI_DEVICE_PATH_PROTOCOL              *DevicePath;
  ///
  /// The device returned EFI_DEVICE_ERROR. It is skipped until it is
  /// reconnected or its presence changes.
  ///
  BOOLEAN                               Failed;
  UINT8                                 LastPresence;
} AOM_DEVICE;

#define AOM_DEVICE_FROM_LINK(a) \
  CR (a, AOM_DEVICE, Link, AOM_DEVICE_SIGNATURE)

///
/// The manager's singleton state.
///
typedef struct {
  EFI_HANDLE                             DriverBindingHandle;
  ///
  /// Handle carrying DefaultOutput and the manager protocol.
  ///
  EFI_HANDLE                             Handle;
  EDKII_SIMPLE_AUDIO_OUTPUT_PROTOCOL     Audio;
  EDKII_SIMPLE_AUDIO_OUTPUT_MODE         Mode;
  EDKII_AUDIO_OUTPUT_MANAGER_PROTOCOL    Manager;

  LIST_ENTRY                             Devices;
  UINTN                                  DeviceCount;
  AOM_DEVICE                             *Active;
  ///
  /// The device that received the most recent Play(). Stop() is sent to it.
  ///
  AOM_DEVICE                             *LastPlayed;
  ///
  /// The saved preference, or NULL.
  ///
  EFI_DEVICE_PATH_PROTOCOL               *Preferred;

  EFI_EVENT                              PollEvent;
  EFI_EVENT                              NotifyEvent;
  VOID                                   *Registration;
} AOM_PRIVATE;

extern AOM_PRIVATE                   gAom;
extern EFI_DRIVER_BINDING_PROTOCOL   gAomDriverBinding;
extern EFI_COMPONENT_NAME_PROTOCOL   gAomComponentName;
extern EFI_COMPONENT_NAME2_PROTOCOL  gAomComponentName2;

//
// Policy.c
//

/**
  Load the saved volume, mute state, and preferred device into gAom.
  Missing or malformed settings leave the defaults in place.

**/
VOID
AomLoadSettings (
  VOID
  );

/**
  Save gAom's volume, mute state, and preferred device.

  @retval EFI_SUCCESS  The settings were saved.
  @retval Others       The variable could not be written.

**/
EFI_STATUS
AomSaveSettings (
  VOID
  );

/**
  Choose the active device from the preference and policy, and copy its
  OutputType and Presence to the mode of DefaultOutput.

**/
VOID
AomSelectDevice (
  VOID
  );

/**
  Find the device record for a handle.

  @param[in] Handle  A device handle.

  @return The record, or NULL.

**/
AOM_DEVICE *
AomFindDevice (
  IN EFI_HANDLE  Handle
  );

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
  );

//
// DefaultOutput.c
//

/**
  Initialize DefaultOutput's protocol instance and mode. Volume and Muted
  must already be loaded.

**/
VOID
AomInitDefaultOutput (
  VOID
  );

//
// ComponentName.c
//

/**
  Retrieves a Unicode string that is the user readable name of the driver.

  @param[in]  This        A pointer to the EFI_COMPONENT_NAME2_PROTOCOL or
                          EFI_COMPONENT_NAME_PROTOCOL instance.
  @param[in]  Language    A pointer to a Null-terminated ASCII string
                          array indicating the language.
  @param[out] DriverName  A pointer to the Unicode string to return.

  @retval EFI_SUCCESS            The driver name was returned.
  @retval EFI_INVALID_PARAMETER  Language or DriverName is NULL.
  @retval EFI_UNSUPPORTED        The language is not supported.

**/
EFI_STATUS
EFIAPI
AomComponentNameGetDriverName (
  IN  EFI_COMPONENT_NAME_PROTOCOL  *This,
  IN  CHAR8                        *Language,
  OUT CHAR16                       **DriverName
  );

/**
  Retrieves a Unicode string that is the user readable name of a
  controller. The manager names no controllers.

  @param[in]  This              A pointer to the EFI_COMPONENT_NAME2_PROTOCOL or
                                EFI_COMPONENT_NAME_PROTOCOL instance.
  @param[in]  ControllerHandle  The handle of a controller.
  @param[in]  ChildHandle       The handle of the child controller, or NULL.
  @param[in]  Language          A pointer to a Null-terminated ASCII string
                                array indicating the language.
  @param[out] ControllerName    A pointer to the Unicode string to return.

  @retval EFI_UNSUPPORTED  Always.

**/
EFI_STATUS
EFIAPI
AomComponentNameGetControllerName (
  IN  EFI_COMPONENT_NAME_PROTOCOL  *This,
  IN  EFI_HANDLE                   ControllerHandle,
  IN  EFI_HANDLE                   ChildHandle        OPTIONAL,
  IN  CHAR8                        *Language,
  OUT CHAR16                       **ControllerName
  );
