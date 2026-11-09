/** @file
  UEFI Component Name(2) protocol implementation for the audio output
  manager.

  Copyright (c) 2026, Ethin Probst. All rights reserved.<BR>
  SPDX-License-Identifier: BSD-2-Clause-Patent

**/

#include "AudioOutputManagerDxe.h"

//
// EFI Component Name Protocol
//
GLOBAL_REMOVE_IF_UNREFERENCED EFI_COMPONENT_NAME_PROTOCOL  gAomComponentName = {
  AomComponentNameGetDriverName,
  AomComponentNameGetControllerName,
  "eng"
};

//
// EFI Component Name 2 Protocol
//
GLOBAL_REMOVE_IF_UNREFERENCED EFI_COMPONENT_NAME2_PROTOCOL  gAomComponentName2 = {
  (EFI_COMPONENT_NAME2_GET_DRIVER_NAME)AomComponentNameGetDriverName,
  (EFI_COMPONENT_NAME2_GET_CONTROLLER_NAME)AomComponentNameGetControllerName,
  "en"
};

GLOBAL_REMOVE_IF_UNREFERENCED EFI_UNICODE_STRING_TABLE  mAomDriverNameTable[] = {
  { "eng;en", L"Audio Output Manager" },
  { NULL,     NULL                    }
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
AomComponentNameGetDriverName (
  IN  EFI_COMPONENT_NAME_PROTOCOL  *This,
  IN  CHAR8                        *Language,
  OUT CHAR16                       **DriverName
  )
{
  return LookupUnicodeString2 (
           Language,
           This->SupportedLanguages,
           mAomDriverNameTable,
           DriverName,
           (BOOLEAN)(This == &gAomComponentName)
           );
}

/**
  Retrieves a Unicode string that is the user readable name of a
  controller. The devices the manager binds to are named by their own
  drivers.

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
  )
{
  return EFI_UNSUPPORTED;
}
