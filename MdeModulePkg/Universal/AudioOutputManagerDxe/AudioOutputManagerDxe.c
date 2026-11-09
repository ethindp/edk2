/** @file
  Driver binding, entry point, and EDKII_AUDIO_OUTPUT_MANAGER_PROTOCOL of
  the audio output manager.

  Copyright (c) 2026, Ethin Probst. All rights reserved.<BR>
  SPDX-License-Identifier: BSD-2-Clause-Patent

**/

#include "AudioOutputManagerDxe.h"

AOM_PRIVATE  gAom;

/**
  Tests whether the manager should bind to a handle.

  @param[in] This                 A pointer to the EFI_DRIVER_BINDING_PROTOCOL
                                  instance.
  @param[in] ControllerHandle     The handle to test.
  @param[in] RemainingDevicePath  Unused.

  @retval EFI_SUCCESS      The handle is an audio output device.
  @retval EFI_UNSUPPORTED  It is not, or it is DefaultOutput's own handle.
  @retval Others           The protocol is already opened BY_DRIVER.

**/
EFI_STATUS
EFIAPI
AomDriverBindingSupported (
  IN EFI_DRIVER_BINDING_PROTOCOL  *This,
  IN EFI_HANDLE                   ControllerHandle,
  IN EFI_DEVICE_PATH_PROTOCOL     *RemainingDevicePath OPTIONAL
  )
{
  EFI_STATUS                          Status;
  EDKII_SIMPLE_AUDIO_OUTPUT_PROTOCOL  *Audio;

  if (ControllerHandle == gAom.Handle) {
    return EFI_UNSUPPORTED;
  }

  Status = gBS->OpenProtocol (
                  ControllerHandle,
                  &gEdkiiSimpleAudioOutputProtocolGuid,
                  (VOID **)&Audio,
                  This->DriverBindingHandle,
                  ControllerHandle,
                  EFI_OPEN_PROTOCOL_BY_DRIVER
                  );
  if (EFI_ERROR (Status)) {
    return Status;
  }

  gBS->CloseProtocol (
         ControllerHandle,
         &gEdkiiSimpleAudioOutputProtocolGuid,
         This->DriverBindingHandle,
         ControllerHandle
         );
  return EFI_SUCCESS;
}

/**
  Starts tracking an audio output device.

  @param[in] This                 A pointer to the EFI_DRIVER_BINDING_PROTOCOL
                                  instance.
  @param[in] ControllerHandle     The device handle.
  @param[in] RemainingDevicePath  Unused.

  @retval EFI_SUCCESS           The device is tracked.
  @retval EFI_OUT_OF_RESOURCES  Memory could not be allocated.
  @retval Others                The protocol could not be opened.

**/
EFI_STATUS
EFIAPI
AomDriverBindingStart (
  IN EFI_DRIVER_BINDING_PROTOCOL  *This,
  IN EFI_HANDLE                   ControllerHandle,
  IN EFI_DEVICE_PATH_PROTOCOL     *RemainingDevicePath OPTIONAL
  )
{
  EFI_STATUS                Status;
  AOM_DEVICE                *Device;
  EFI_DEVICE_PATH_PROTOCOL  *DevicePath;
  EFI_TPL                   OldTpl;

  Device = AllocateZeroPool (sizeof (*Device));
  if (Device == NULL) {
    return EFI_OUT_OF_RESOURCES;
  }

  //
  // Open the protocol BY_DRIVER so that the driver of the device disconnects
  // the manager before removing the device.
  //
  Status = gBS->OpenProtocol (
                  ControllerHandle,
                  &gEdkiiSimpleAudioOutputProtocolGuid,
                  (VOID **)&Device->Audio,
                  This->DriverBindingHandle,
                  ControllerHandle,
                  EFI_OPEN_PROTOCOL_BY_DRIVER
                  );
  if (EFI_ERROR (Status)) {
    FreePool (Device);
    return Status;
  }

  Device->Signature    = AOM_DEVICE_SIGNATURE;
  Device->Handle       = ControllerHandle;
  Device->LastPresence = Device->Audio->Mode->Presence;

  DevicePath = DevicePathFromHandle (ControllerHandle);
  if (DevicePath != NULL) {
    Device->DevicePath = DuplicateDevicePath (DevicePath);
  }

  //
  // AomPollNotify() reads the device list at TPL_CALLBACK.
  //
  OldTpl = gBS->RaiseTPL (TPL_CALLBACK);
  InsertTailList (&gAom.Devices, &Device->Link);
  gAom.DeviceCount++;
  AomSelectDevice ();
  gBS->RestoreTPL (OldTpl);

  DEBUG ((
    DEBUG_INFO,
    "%a: device %p type %u presence %u\n",
    __func__,
    ControllerHandle,
    Device->Audio->Mode->OutputType,
    Device->Audio->Mode->Presence
    ));

  return EFI_SUCCESS;
}

/**
  Stops tracking an audio output device, and selects another device if it
  was the active one.

  @param[in] This               A pointer to the EFI_DRIVER_BINDING_PROTOCOL
                                instance.
  @param[in] ControllerHandle   The device handle.
  @param[in] NumberOfChildren   Not used. The manager creates no children.
  @param[in] ChildHandleBuffer  Unused.

  @retval EFI_SUCCESS  The device is no longer tracked.

**/
EFI_STATUS
EFIAPI
AomDriverBindingStop (
  IN EFI_DRIVER_BINDING_PROTOCOL  *This,
  IN EFI_HANDLE                   ControllerHandle,
  IN UINTN                        NumberOfChildren,
  IN EFI_HANDLE                   *ChildHandleBuffer OPTIONAL
  )
{
  AOM_DEVICE  *Device;
  EFI_TPL     OldTpl;

  Device = AomFindDevice (ControllerHandle);
  if (Device == NULL) {
    return EFI_SUCCESS;
  }

  if (gAom.LastPlayed == Device) {
    Device->Audio->Stop (Device->Audio);
    gAom.LastPlayed = NULL;
  }

  //
  // Remove the device before the presence poll can access it again.
  //
  OldTpl = gBS->RaiseTPL (TPL_CALLBACK);
  if (gAom.Active == Device) {
    gAom.Active = NULL;
  }

  RemoveEntryList (&Device->Link);
  gAom.DeviceCount--;
  AomSelectDevice ();
  gBS->RestoreTPL (OldTpl);

  gBS->CloseProtocol (
         ControllerHandle,
         &gEdkiiSimpleAudioOutputProtocolGuid,
         This->DriverBindingHandle,
         ControllerHandle
         );

  if (Device->DevicePath != NULL) {
    FreePool (Device->DevicePath);
  }

  FreePool (Device);

  DEBUG ((DEBUG_INFO, "%a: device %p removed\n", __func__, ControllerHandle));
  return EFI_SUCCESS;
}

EFI_DRIVER_BINDING_PROTOCOL  gAomDriverBinding = {
  AomDriverBindingSupported,
  AomDriverBindingStart,
  AomDriverBindingStop,
  0x10,
  NULL,
  NULL
};

/**
  Connect the manager to audio outputs as they are installed.

  @param[in] Event    The protocol notification event.
  @param[in] Context  Unused.

**/
STATIC
VOID
EFIAPI
AomProtocolNotify (
  IN EFI_EVENT  Event,
  IN VOID       *Context
  )
{
  EFI_STATUS  Status;
  EFI_HANDLE  Handle;
  UINTN       Size;
  EFI_HANDLE  Drivers[2];

  Drivers[0] = gAom.DriverBindingHandle;
  Drivers[1] = NULL;

  for ( ; ;) {
    Size   = sizeof (Handle);
    Status = gBS->LocateHandle (ByRegisterNotify, NULL, gAom.Registration, &Size, &Handle);
    if (EFI_ERROR (Status)) {
      break;
    }

    if (Handle != gAom.Handle) {
      gBS->ConnectController (Handle, Drivers, NULL, FALSE);
    }
  }
}

/**
  Return the audio output devices the manager knows about.

  @param[in]  This     A pointer to the EDKII_AUDIO_OUTPUT_MANAGER_PROTOCOL instance.
  @param[out] Count    Number of handles returned.
  @param[out] Handles  Pool-allocated array of device handles.

  @retval EFI_SUCCESS            The list was returned.
  @retval EFI_INVALID_PARAMETER  Count or Handles is NULL.
  @retval EFI_OUT_OF_RESOURCES   The list could not be allocated.

**/
STATIC
EFI_STATUS
EFIAPI
AomGetDevices (
  IN  EDKII_AUDIO_OUTPUT_MANAGER_PROTOCOL  *This,
  OUT UINTN                                *Count,
  OUT EFI_HANDLE                           **Handles
  )
{
  LIST_ENTRY  *Link;
  AOM_DEVICE  *Device;
  UINTN       Index;

  if ((Count == NULL) || (Handles == NULL)) {
    return EFI_INVALID_PARAMETER;
  }

  *Count   = gAom.DeviceCount;
  *Handles = NULL;
  if (gAom.DeviceCount == 0) {
    return EFI_SUCCESS;
  }

  *Handles = AllocatePool (gAom.DeviceCount * sizeof (EFI_HANDLE));
  if (*Handles == NULL) {
    *Count = 0;
    return EFI_OUT_OF_RESOURCES;
  }

  Index = 0;
  for (Link = GetFirstNode (&gAom.Devices); !IsNull (&gAom.Devices, Link); Link = GetNextNode (&gAom.Devices, Link)) {
    Device              = AOM_DEVICE_FROM_LINK (Link);
    (*Handles)[Index++] = Device->Handle;
  }

  return EFI_SUCCESS;
}

/**
  Return the device DefaultOutput currently plays through.

  @param[in]  This    A pointer to the EDKII_AUDIO_OUTPUT_MANAGER_PROTOCOL instance.
  @param[out] Handle  The active device.

  @retval EFI_SUCCESS            Handle was returned.
  @retval EFI_INVALID_PARAMETER  Handle is NULL.
  @retval EFI_NO_MEDIA           No audio output device is available.

**/
STATIC
EFI_STATUS
EFIAPI
AomGetActiveDevice (
  IN  EDKII_AUDIO_OUTPUT_MANAGER_PROTOCOL  *This,
  OUT EFI_HANDLE                           *Handle
  )
{
  if (Handle == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  AomSelectDevice ();
  if (gAom.Active == NULL) {
    return EFI_NO_MEDIA;
  }

  *Handle = gAom.Active->Handle;
  return EFI_SUCCESS;
}

/**
  Choose the device DefaultOutput should use, and remember it.

  @param[in] This    A pointer to the EDKII_AUDIO_OUTPUT_MANAGER_PROTOCOL instance.
  @param[in] Handle  A device handle, or NULL to forget the preference.

  @retval EFI_SUCCESS      The preference was set and saved.
  @retval EFI_NOT_FOUND    Handle is not a known device.
  @retval EFI_UNSUPPORTED  The device has no device path.
  @retval Others           The preference could not be saved.

**/
STATIC
EFI_STATUS
EFIAPI
AomSetPreferredDevice (
  IN EDKII_AUDIO_OUTPUT_MANAGER_PROTOCOL  *This,
  IN EFI_HANDLE                           Handle OPTIONAL
  )
{
  AOM_DEVICE                *Device;
  EFI_DEVICE_PATH_PROTOCOL  *Preferred;
  EFI_STATUS                Status;

  Preferred = NULL;
  if (Handle != NULL) {
    Device = AomFindDevice (Handle);
    if (Device == NULL) {
      return EFI_NOT_FOUND;
    }

    if (Device->DevicePath == NULL) {
      return EFI_UNSUPPORTED;
    }

    Preferred = DuplicateDevicePath (Device->DevicePath);
    if (Preferred == NULL) {
      return EFI_OUT_OF_RESOURCES;
    }

    Device->Failed = FALSE;
  }

  if (gAom.Preferred != NULL) {
    FreePool (gAom.Preferred);
  }

  gAom.Preferred = Preferred;
  Status         = AomSaveSettings ();
  AomSelectDevice ();
  return Status;
}

/**
  Return the saved preferred device.

  @param[in]  This        A pointer to the EDKII_AUDIO_OUTPUT_MANAGER_PROTOCOL instance.
  @param[out] DevicePath  Pool-allocated copy of the preferred device path.

  @retval EFI_SUCCESS            DevicePath was returned.
  @retval EFI_INVALID_PARAMETER  DevicePath is NULL.
  @retval EFI_NOT_FOUND          No preference is saved.
  @retval EFI_OUT_OF_RESOURCES   The copy could not be allocated.

**/
STATIC
EFI_STATUS
EFIAPI
AomGetPreferredDevice (
  IN  EDKII_AUDIO_OUTPUT_MANAGER_PROTOCOL  *This,
  OUT EFI_DEVICE_PATH_PROTOCOL             **DevicePath
  )
{
  if (DevicePath == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  if (gAom.Preferred == NULL) {
    return EFI_NOT_FOUND;
  }

  *DevicePath = DuplicateDevicePath (gAom.Preferred);
  return (*DevicePath == NULL) ? EFI_OUT_OF_RESOURCES : EFI_SUCCESS;
}

/**
  The driver entry point.

  @param[in] ImageHandle  The firmware allocated handle for the EFI image.
  @param[in] SystemTable  A pointer to the EFI System Table.

  @retval EFI_SUCCESS  The manager is installed.
  @retval Others       Installation failed.

**/
EFI_STATUS
EFIAPI
AudioOutputManagerEntryPoint (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  EFI_STATUS  Status;

  InitializeListHead (&gAom.Devices);
  gAom.Mode.Volume = AOM_DEFAULT_VOLUME;
  gAom.Mode.Muted  = FALSE;
  AomLoadSettings ();
  AomInitDefaultOutput ();

  gAom.Manager.Revision           = EDKII_AUDIO_OUTPUT_MANAGER_PROTOCOL_REVISION;
  gAom.Manager.DefaultOutput      = &gAom.Audio;
  gAom.Manager.GetDevices         = AomGetDevices;
  gAom.Manager.GetActiveDevice    = AomGetActiveDevice;
  gAom.Manager.SetPreferredDevice = AomSetPreferredDevice;
  gAom.Manager.GetPreferredDevice = AomGetPreferredDevice;

  Status = EfiLibInstallDriverBindingComponentName2 (
             ImageHandle,
             SystemTable,
             &gAomDriverBinding,
             ImageHandle,
             &gAomComponentName,
             &gAomComponentName2
             );
  if (EFI_ERROR (Status)) {
    return Status;
  }

  gAom.DriverBindingHandle = gAomDriverBinding.DriverBindingHandle;

  Status = gBS->InstallMultipleProtocolInterfaces (
                  &gAom.Handle,
                  &gEdkiiSimpleAudioOutputProtocolGuid,
                  &gAom.Audio,
                  &gEdkiiAudioOutputManagerProtocolGuid,
                  &gAom.Manager,
                  NULL
                  );
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Status = gBS->CreateEvent (
                  EVT_TIMER | EVT_NOTIFY_SIGNAL,
                  TPL_CALLBACK,
                  AomPollNotify,
                  NULL,
                  &gAom.PollEvent
                  );
  if (!EFI_ERROR (Status)) {
    gBS->SetTimer (gAom.PollEvent, TimerPeriodic, AOM_POLL_PERIOD);
  }

  //
  // The event is also signaled now, which connects the existing devices.
  //
  gAom.NotifyEvent = EfiCreateProtocolNotifyEvent (
                       &gEdkiiSimpleAudioOutputProtocolGuid,
                       TPL_CALLBACK,
                       AomProtocolNotify,
                       NULL,
                       &gAom.Registration
                       );

  DEBUG ((
    DEBUG_INFO,
    "%a: volume %u, muted %u, preference %a\n",
    __func__,
    gAom.Mode.Volume,
    gAom.Mode.Muted,
    (gAom.Preferred != NULL) ? "saved" : "none"
    ));
  return EFI_SUCCESS;
}
