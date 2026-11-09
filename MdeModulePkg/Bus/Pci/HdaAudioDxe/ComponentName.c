/** @file
  UEFI Component Name(2) protocol implementation for the HD Audio driver.

  Copyright (c) 2026, Ethin Probst. All rights reserved.<BR>
  SPDX-License-Identifier: BSD-2-Clause-Patent

**/

#include "HdaAudioDxe.h"

//
// EFI Component Name Protocol
//
GLOBAL_REMOVE_IF_UNREFERENCED EFI_COMPONENT_NAME_PROTOCOL  gHdaAudioComponentName = {
  HdaAudioComponentNameGetDriverName,
  HdaAudioComponentNameGetControllerName,
  "eng"
};

//
// EFI Component Name 2 Protocol
//
GLOBAL_REMOVE_IF_UNREFERENCED EFI_COMPONENT_NAME2_PROTOCOL  gHdaAudioComponentName2 = {
  (EFI_COMPONENT_NAME2_GET_DRIVER_NAME)HdaAudioComponentNameGetDriverName,
  (EFI_COMPONENT_NAME2_GET_CONTROLLER_NAME)HdaAudioComponentNameGetControllerName,
  "en"
};

GLOBAL_REMOVE_IF_UNREFERENCED EFI_UNICODE_STRING_TABLE  mHdaAudioDriverNameTable[] = {
  { "eng;en", L"Intel High Definition Audio Driver" },
  { NULL,     NULL                                  }
};

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
HdaAudioComponentNameGetDriverName (
  IN  EFI_COMPONENT_NAME_PROTOCOL  *This,
  IN  CHAR8                        *Language,
  OUT CHAR16                       **DriverName
  )
{
  return LookupUnicodeString2 (
           Language,
           This->SupportedLanguages,
           mHdaAudioDriverNameTable,
           DriverName,
           (BOOLEAN)(This == &gHdaAudioComponentName)
           );
}

/**
  Retrieves a Unicode string that is the user readable name of the
  controller, or of one of its outputs.

  @param[in]  This              A pointer to the EFI_COMPONENT_NAME2_PROTOCOL or
                                EFI_COMPONENT_NAME_PROTOCOL instance.
  @param[in]  ControllerHandle  The handle of a controller.
  @param[in]  ChildHandle       The handle of an output, or NULL.
  @param[in]  Language          A pointer to a Null-terminated ASCII string
                                array indicating the language.
  @param[out] ControllerName    A pointer to the Unicode string to return.

  @retval EFI_SUCCESS            The name was returned.
  @retval EFI_INVALID_PARAMETER  A parameter is invalid.
  @retval EFI_UNSUPPORTED        The driver is not managing the controller or
                                 child, or the language is not supported.

**/
EFI_STATUS
EFIAPI
HdaAudioComponentNameGetControllerName (
  IN  EFI_COMPONENT_NAME_PROTOCOL  *This,
  IN  EFI_HANDLE                   ControllerHandle,
  IN  EFI_HANDLE                   ChildHandle        OPTIONAL,
  IN  CHAR8                        *Language,
  OUT CHAR16                       **ControllerName
  )
{
  EFI_STATUS                          Status;
  EDKII_SIMPLE_AUDIO_OUTPUT_PROTOCOL  *Audio;
  HDA_CONTROLLER                      *Controller;
  HDA_OUTPUT                          *Output;
  EFI_UNICODE_STRING_TABLE            *Table;

  if ((Language == NULL) || (ControllerName == NULL)) {
    return EFI_INVALID_PARAMETER;
  }

  Status = EfiTestManagedDevice (
             ControllerHandle,
             gHdaAudioDriverBinding.DriverBindingHandle,
             &gEfiPciIoProtocolGuid
             );
  if (EFI_ERROR (Status)) {
    return Status;
  }

  if (ChildHandle == NULL) {
    Status = gBS->OpenProtocol (
                    ControllerHandle,
                    &gEfiCallerIdGuid,
                    (VOID **)&Controller,
                    gHdaAudioDriverBinding.DriverBindingHandle,
                    ControllerHandle,
                    EFI_OPEN_PROTOCOL_GET_PROTOCOL
                    );
    if (EFI_ERROR (Status)) {
      return Status;
    }

    Table = Controller->ControllerNameTable;
  } else {
    Status = EfiTestChildHandle (ControllerHandle, ChildHandle, &gEfiPciIoProtocolGuid);
    if (EFI_ERROR (Status)) {
      return Status;
    }

    Status = gBS->OpenProtocol (
                    ChildHandle,
                    &gEdkiiSimpleAudioOutputProtocolGuid,
                    (VOID **)&Audio,
                    gHdaAudioDriverBinding.DriverBindingHandle,
                    ChildHandle,
                    EFI_OPEN_PROTOCOL_GET_PROTOCOL
                    );
    if (EFI_ERROR (Status)) {
      return Status;
    }

    Output = HDA_OUTPUT_FROM_AUDIO (Audio);
    Table  = Output->NameTable;
  }

  return LookupUnicodeString2 (
           Language,
           This->SupportedLanguages,
           Table,
           ControllerName,
           (BOOLEAN)(This == &gHdaAudioComponentName)
           );
}
