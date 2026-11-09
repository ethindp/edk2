/** @file
  Driver binding and entry point of the Intel High Definition Audio output
  driver.

  Copyright (c) 2026, Ethin Probst. All rights reserved.<BR>
  SPDX-License-Identifier: BSD-2-Clause-Patent

**/

#include "HdaAudioDxe.h"

/**
  Tests to see if this driver supports a given controller.

  @param[in] This                 A pointer to the EFI_DRIVER_BINDING_PROTOCOL
                                  instance.
  @param[in] ControllerHandle     The handle of the controller to test.
  @param[in] RemainingDevicePath  Optional device path. If it is not the end node,
                                  it must be a Controller() node.

  @retval EFI_SUCCESS          The controller is an HD Audio controller.
  @retval EFI_ALREADY_STARTED  The controller is already managed.
  @retval EFI_UNSUPPORTED      The controller is not an HD Audio controller.

**/
EFI_STATUS
EFIAPI
HdaAudioDriverBindingSupported (
  IN EFI_DRIVER_BINDING_PROTOCOL  *This,
  IN EFI_HANDLE                   ControllerHandle,
  IN EFI_DEVICE_PATH_PROTOCOL     *RemainingDevicePath OPTIONAL
  )
{
  EFI_STATUS           Status;
  EFI_PCI_IO_PROTOCOL  *PciIo;
  UINT8                ClassCode[3];

  if ((RemainingDevicePath != NULL) && !IsDevicePathEnd (RemainingDevicePath) &&
      ((DevicePathType (RemainingDevicePath) != HARDWARE_DEVICE_PATH) ||
       (DevicePathSubType (RemainingDevicePath) != HW_CONTROLLER_DP)))
  {
    return EFI_UNSUPPORTED;
  }

  Status = gBS->OpenProtocol (
                  ControllerHandle,
                  &gEfiPciIoProtocolGuid,
                  (VOID **)&PciIo,
                  This->DriverBindingHandle,
                  ControllerHandle,
                  EFI_OPEN_PROTOCOL_BY_DRIVER
                  );
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Status = PciIo->Pci.Read (
                        PciIo,
                        EfiPciIoWidthUint8,
                        PCI_CLASSCODE_OFFSET,
                        sizeof (ClassCode),
                        ClassCode
                        );
  if (!EFI_ERROR (Status) &&
      ((ClassCode[2] != PCI_CLASS_MEDIA) || (ClassCode[1] != HDA_PCI_SUBCLASS)))
  {
    Status = EFI_UNSUPPORTED;
  }

  gBS->CloseProtocol (
         ControllerHandle,
         &gEfiPciIoProtocolGuid,
         This->DriverBindingHandle,
         ControllerHandle
         );
  return Status;
}

/**
  Stop DMA at ExitBootServices().

  @param[in] Event    The ExitBootServices event.
  @param[in] Context  The controller instance.

**/
STATIC
VOID
EFIAPI
HdaAudioExitBootServices (
  IN EFI_EVENT  Event,
  IN VOID       *Context
  )
{
  HdaControllerHalt ((HDA_CONTROLLER *)Context);
}

/**
  Refresh jack presence on every output.

  @param[in] Event    The timer event.
  @param[in] Context  The controller instance.

**/
STATIC
VOID
EFIAPI
HdaAudioPresenceNotify (
  IN EFI_EVENT  Event,
  IN VOID       *Context
  )
{
  HDA_CONTROLLER  *Controller;
  UINT8           Index;

  Controller = (HDA_CONTROLLER *)Context;
  for (Index = 0; Index < Controller->OutputCount; Index++) {
    if ((Controller->Outputs[Index] != NULL) && Controller->Outputs[Index]->CanSensePresence) {
      HdaCodecUpdatePresence (Controller->Outputs[Index]);
    }
  }
}

/**
  Free an output that has no child handle.

  @param[in] Output  The output.

**/
STATIC
VOID
HdaFreeOutput (
  IN HDA_OUTPUT  *Output
  )
{
  if (Output->Controller->RoutedOutput == Output) {
    Output->Controller->RoutedOutput = NULL;
  }

  if (Output->NameTable != NULL) {
    FreeUnicodeStringTable (Output->NameTable);
  }

  if (Output->DevicePath != NULL) {
    FreePool (Output->DevicePath);
  }

  FreePool (Output);
}

/**
  Create the child handle of one output.

  @param[in] Output  The output.

  @retval EFI_SUCCESS           The child is installed.
  @retval EFI_OUT_OF_RESOURCES  Memory could not be allocated.
  @retval Others                Protocol installation failed.

**/
STATIC
EFI_STATUS
HdaCreateChild (
  IN HDA_OUTPUT  *Output
  )
{
  EFI_STATUS              Status;
  HDA_CONTROLLER          *Controller;
  CONTROLLER_DEVICE_PATH  Node;
  EFI_PCI_IO_PROTOCOL     *PciIo;

  Controller = Output->Controller;

  Node.Header.Type    = HARDWARE_DEVICE_PATH;
  Node.Header.SubType = HW_CONTROLLER_DP;
  SetDevicePathNodeLength (&Node.Header, sizeof (Node));
  Node.ControllerNumber = ((UINT32)Output->Codec << 8) | Output->Path.Nodes[0].Nid;

  Output->DevicePath = AppendDevicePathNode (Controller->DevicePath, &Node.Header);
  if (Output->DevicePath == NULL) {
    return EFI_OUT_OF_RESOURCES;
  }

  HdaAudioInitProtocol (Output);
  AddUnicodeString2 ("eng", gHdaAudioComponentName.SupportedLanguages, &Output->NameTable, Output->Name, TRUE);
  AddUnicodeString2 ("en", gHdaAudioComponentName2.SupportedLanguages, &Output->NameTable, Output->Name, FALSE);

  Status = gBS->InstallMultipleProtocolInterfaces (
                  &Output->Handle,
                  &gEfiDevicePathProtocolGuid,
                  Output->DevicePath,
                  &gEdkiiSimpleAudioOutputProtocolGuid,
                  &Output->Audio,
                  NULL
                  );
  if (EFI_ERROR (Status)) {
    Output->Handle = NULL;
    return Status;
  }

  Status = gBS->OpenProtocol (
                  Controller->Handle,
                  &gEfiPciIoProtocolGuid,
                  (VOID **)&PciIo,
                  Controller->DriverBindingHandle,
                  Output->Handle,
                  EFI_OPEN_PROTOCOL_BY_CHILD_CONTROLLER
                  );
  if (EFI_ERROR (Status)) {
    gBS->UninstallMultipleProtocolInterfaces (
           Output->Handle,
           &gEfiDevicePathProtocolGuid,
           Output->DevicePath,
           &gEdkiiSimpleAudioOutputProtocolGuid,
           &Output->Audio,
           NULL
           );
    Output->Handle = NULL;
  }

  return Status;
}

/**
  Release the resources Start() acquired for the controller. All children
  must already be destroyed. The controller may be partially initialized.

  @param[in] Controller  The controller instance.

**/
STATIC
VOID
HdaAudioCleanup (
  IN HDA_CONTROLLER  *Controller
  )
{
  EFI_TPL  OldTpl;
  UINT8    Index;

  if (Controller->PresenceEvent != NULL) {
    gBS->CloseEvent (Controller->PresenceEvent);
  }

  if (Controller->PumpEvent != NULL) {
    OldTpl = gBS->RaiseTPL (TPL_NOTIFY);
    HdaStreamFinish (Controller, EFI_ABORTED);
    gBS->RestoreTPL (OldTpl);
    gBS->CloseEvent (Controller->PumpEvent);
  }

  if (Controller->ExitBootServicesEvent != NULL) {
    gBS->CloseEvent (Controller->ExitBootServicesEvent);
  }

  for (Index = 0; Index < Controller->OutputCount; Index++) {
    if (Controller->Outputs[Index] != NULL) {
      HdaFreeOutput (Controller->Outputs[Index]);
      Controller->Outputs[Index] = NULL;
    }
  }

  if (Controller->Corb != NULL) {
    HdaControllerShutdown (Controller);
  }

  HdaStreamFree (Controller);

  if (Controller->PciAttributesSaved) {
    Controller->PciIo->Attributes (
                         Controller->PciIo,
                         EfiPciIoAttributeOperationSet,
                         Controller->OriginalPciAttributes,
                         NULL
                         );
  }

  if (Controller->ControllerNameTable != NULL) {
    FreeUnicodeStringTable (Controller->ControllerNameTable);
  }

  gBS->CloseProtocol (
         Controller->Handle,
         &gEfiPciIoProtocolGuid,
         Controller->DriverBindingHandle,
         Controller->Handle
         );
  FreePool (Controller);
}

/**
  Enable memory decoding and bus mastering, and 64-bit DMA if the
  controller supports it.

  @param[in] Controller  The controller instance.

  @retval EFI_SUCCESS  The attributes were set.
  @retval Others       The PCI I/O protocol reported an error.

**/
STATIC
EFI_STATUS
HdaAudioEnablePci (
  IN HDA_CONTROLLER  *Controller
  )
{
  EFI_STATUS  Status;
  UINT64      Supports;
  UINT16      Gcap;

  Status = Controller->PciIo->Attributes (
                                Controller->PciIo,
                                EfiPciIoAttributeOperationGet,
                                0,
                                &Controller->OriginalPciAttributes
                                );
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Controller->PciAttributesSaved = TRUE;

  Status = Controller->PciIo->Attributes (
                                Controller->PciIo,
                                EfiPciIoAttributeOperationSupported,
                                0,
                                &Supports
                                );
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Supports &= (UINT64)EFI_PCI_DEVICE_ENABLE;
  Status    = Controller->PciIo->Attributes (
                                   Controller->PciIo,
                                   EfiPciIoAttributeOperationEnable,
                                   Supports,
                                   NULL
                                   );
  if (EFI_ERROR (Status)) {
    return Status;
  }

  //
  // GCAP is readable now that memory decoding is on.
  //
  Gcap = HdaRead16 (Controller, HDA_REG_GCAP);
  if ((Gcap != MAX_UINT16) && ((Gcap & HDA_GCAP_64OK) != 0)) {
    Status = Controller->PciIo->Attributes (
                                  Controller->PciIo,
                                  EfiPciIoAttributeOperationEnable,
                                  EFI_PCI_IO_ATTRIBUTE_DUAL_ADDRESS_CYCLE,
                                  NULL
                                  );
    if (EFI_ERROR (Status)) {
      DEBUG ((DEBUG_INFO, "%a: 64-bit DMA not enabled: %r\n", __func__, Status));
    }
  }

  return EFI_SUCCESS;
}

/**
  Starts managing an HD Audio controller and creates a child for each
  output.

  @param[in] This                 A pointer to the EFI_DRIVER_BINDING_PROTOCOL
                                  instance.
  @param[in] ControllerHandle     The handle of the controller to start.
  @param[in] RemainingDevicePath  Not used. A child is created for every output.

  @retval EFI_SUCCESS           At least one output child was created.
  @retval EFI_OUT_OF_RESOURCES  Memory could not be allocated.
  @retval Others                The controller could not be initialized.

**/
EFI_STATUS
EFIAPI
HdaAudioDriverBindingStart (
  IN EFI_DRIVER_BINDING_PROTOCOL  *This,
  IN EFI_HANDLE                   ControllerHandle,
  IN EFI_DEVICE_PATH_PROTOCOL     *RemainingDevicePath OPTIONAL
  )
{
  EFI_STATUS      Status;
  HDA_CONTROLLER  *Controller;
  UINT16          CodecMask;
  UINT8           Index;
  UINT8           Children;

  Controller = AllocateZeroPool (sizeof (*Controller));
  if (Controller == NULL) {
    return EFI_OUT_OF_RESOURCES;
  }

  Controller->Signature           = HDA_CONTROLLER_SIGNATURE;
  Controller->Handle              = ControllerHandle;
  Controller->DriverBindingHandle = This->DriverBindingHandle;

  Status = gBS->OpenProtocol (
                  ControllerHandle,
                  &gEfiPciIoProtocolGuid,
                  (VOID **)&Controller->PciIo,
                  This->DriverBindingHandle,
                  ControllerHandle,
                  EFI_OPEN_PROTOCOL_BY_DRIVER
                  );
  if (EFI_ERROR (Status)) {
    FreePool (Controller);
    return Status;
  }

  Status = gBS->OpenProtocol (
                  ControllerHandle,
                  &gEfiDevicePathProtocolGuid,
                  (VOID **)&Controller->DevicePath,
                  This->DriverBindingHandle,
                  ControllerHandle,
                  EFI_OPEN_PROTOCOL_GET_PROTOCOL
                  );
  if (EFI_ERROR (Status)) {
    goto Error;
  }

  Status = HdaAudioEnablePci (Controller);
  if (EFI_ERROR (Status)) {
    goto Error;
  }

  HdaDetectPlatform (Controller);

  Status = HdaControllerInit (Controller, &CodecMask);
  if (EFI_ERROR (Status)) {
    goto Error;
  }

  Status = HdaCodecEnumerateOutputs (Controller, CodecMask);
  if (EFI_ERROR (Status)) {
    goto Error;
  }

  Status = HdaStreamInit (Controller);
  if (EFI_ERROR (Status)) {
    goto Error;
  }

  Status = gBS->CreateEvent (
                  EVT_TIMER | EVT_NOTIFY_SIGNAL,
                  TPL_NOTIFY,
                  HdaStreamPumpNotify,
                  Controller,
                  &Controller->PumpEvent
                  );
  if (EFI_ERROR (Status)) {
    goto Error;
  }

  Status = gBS->CreateEvent (
                  EVT_TIMER | EVT_NOTIFY_SIGNAL,
                  TPL_CALLBACK,
                  HdaAudioPresenceNotify,
                  Controller,
                  &Controller->PresenceEvent
                  );
  if (EFI_ERROR (Status)) {
    goto Error;
  }

  Status = gBS->CreateEventEx (
                  EVT_NOTIFY_SIGNAL,
                  TPL_NOTIFY,
                  HdaAudioExitBootServices,
                  Controller,
                  &gEfiEventExitBootServicesGuid,
                  &Controller->ExitBootServicesEvent
                  );
  if (EFI_ERROR (Status)) {
    goto Error;
  }

  AddUnicodeString2 (
    "eng",
    gHdaAudioComponentName.SupportedLanguages,
    &Controller->ControllerNameTable,
    L"Intel High Definition Audio Controller",
    TRUE
    );
  AddUnicodeString2 (
    "en",
    gHdaAudioComponentName2.SupportedLanguages,
    &Controller->ControllerNameTable,
    L"Intel High Definition Audio Controller",
    FALSE
    );

  Status = gBS->InstallProtocolInterface (
                  &ControllerHandle,
                  &gEfiCallerIdGuid,
                  EFI_NATIVE_INTERFACE,
                  Controller
                  );
  if (EFI_ERROR (Status)) {
    goto Error;
  }

  Children = 0;
  for (Index = 0; Index < Controller->OutputCount; Index++) {
    Status = HdaCreateChild (Controller->Outputs[Index]);
    if (EFI_ERROR (Status)) {
      DEBUG ((DEBUG_ERROR, "%a: output \"%s\": %r\n", __func__, Controller->Outputs[Index]->Name, Status));
      HdaFreeOutput (Controller->Outputs[Index]);
      Controller->Outputs[Index] = NULL;
    } else {
      Children++;
    }
  }

  if (Children == 0) {
    gBS->UninstallProtocolInterface (ControllerHandle, &gEfiCallerIdGuid, Controller);
    Status = EFI_DEVICE_ERROR;
    goto Error;
  }

  gBS->SetTimer (Controller->PresenceEvent, TimerPeriodic, HDA_PRESENCE_PERIOD);
  DEBUG ((DEBUG_INFO, "%a: %u audio output(s) ready\n", __func__, Children));
  return EFI_SUCCESS;

Error:
  DEBUG ((DEBUG_ERROR, "%a: %r\n", __func__, Status));
  HdaAudioCleanup (Controller);
  return Status;
}

/**
  Destroy one output child.

  @param[in] Controller  The controller instance.
  @param[in] Child       The child handle.

  @retval EFI_SUCCESS  The child is gone.
  @retval Others       The child is not ours, or a consumer would not let go.

**/
STATIC
EFI_STATUS
HdaDestroyChild (
  IN HDA_CONTROLLER  *Controller,
  IN EFI_HANDLE      Child
  )
{
  EFI_STATUS                          Status;
  EDKII_SIMPLE_AUDIO_OUTPUT_PROTOCOL  *Audio;
  HDA_OUTPUT                          *Output;
  EFI_PCI_IO_PROTOCOL                 *PciIo;
  EFI_TPL                             OldTpl;
  UINT8                               Index;

  Status = gBS->OpenProtocol (
                  Child,
                  &gEdkiiSimpleAudioOutputProtocolGuid,
                  (VOID **)&Audio,
                  Controller->DriverBindingHandle,
                  Controller->Handle,
                  EFI_OPEN_PROTOCOL_GET_PROTOCOL
                  );
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Output = HDA_OUTPUT_FROM_AUDIO (Audio);

  gBS->CloseProtocol (
         Controller->Handle,
         &gEfiPciIoProtocolGuid,
         Controller->DriverBindingHandle,
         Child
         );

  Status = gBS->UninstallMultipleProtocolInterfaces (
                  Child,
                  &gEfiDevicePathProtocolGuid,
                  Output->DevicePath,
                  &gEdkiiSimpleAudioOutputProtocolGuid,
                  &Output->Audio,
                  NULL
                  );
  if (EFI_ERROR (Status)) {
    gBS->OpenProtocol (
           Controller->Handle,
           &gEfiPciIoProtocolGuid,
           (VOID **)&PciIo,
           Controller->DriverBindingHandle,
           Child,
           EFI_OPEN_PROTOCOL_BY_CHILD_CONTROLLER
           );
    return Status;
  }

  OldTpl = gBS->RaiseTPL (TPL_NOTIFY);
  if (Controller->PlayingOutput == Output) {
    HdaStreamFinish (Controller, EFI_ABORTED);
  }

  gBS->RestoreTPL (OldTpl);

  for (Index = 0; Index < Controller->OutputCount; Index++) {
    if (Controller->Outputs[Index] == Output) {
      Controller->Outputs[Index] = NULL;
    }
  }

  HdaFreeOutput (Output);
  return EFI_SUCCESS;
}

/**
  Stops managing an HD Audio controller, or destroys some of its output
  children.

  @param[in] This               A pointer to the EFI_DRIVER_BINDING_PROTOCOL
                                instance.
  @param[in] ControllerHandle   The controller to stop.
  @param[in] NumberOfChildren   Number of children in ChildHandleBuffer. If 0, the
                                controller itself is stopped.
  @param[in] ChildHandleBuffer  The children to destroy.

  @retval EFI_SUCCESS       The request completed.
  @retval EFI_DEVICE_ERROR  A child could not be destroyed.

**/
EFI_STATUS
EFIAPI
HdaAudioDriverBindingStop (
  IN EFI_DRIVER_BINDING_PROTOCOL  *This,
  IN EFI_HANDLE                   ControllerHandle,
  IN UINTN                        NumberOfChildren,
  IN EFI_HANDLE                   *ChildHandleBuffer OPTIONAL
  )
{
  EFI_STATUS      Status;
  HDA_CONTROLLER  *Controller;
  UINTN           Index;
  BOOLEAN         AllDestroyed;

  Status = gBS->OpenProtocol (
                  ControllerHandle,
                  &gEfiCallerIdGuid,
                  (VOID **)&Controller,
                  This->DriverBindingHandle,
                  ControllerHandle,
                  EFI_OPEN_PROTOCOL_GET_PROTOCOL
                  );
  if (EFI_ERROR (Status)) {
    return EFI_SUCCESS;
  }

  ASSERT (Controller->Signature == HDA_CONTROLLER_SIGNATURE);

  if (NumberOfChildren > 0) {
    AllDestroyed = TRUE;
    for (Index = 0; Index < NumberOfChildren; Index++) {
      if (EFI_ERROR (HdaDestroyChild (Controller, ChildHandleBuffer[Index]))) {
        AllDestroyed = FALSE;
      }
    }

    return AllDestroyed ? EFI_SUCCESS : EFI_DEVICE_ERROR;
  }

  gBS->UninstallProtocolInterface (ControllerHandle, &gEfiCallerIdGuid, Controller);
  HdaAudioCleanup (Controller);
  return EFI_SUCCESS;
}

EFI_DRIVER_BINDING_PROTOCOL  gHdaAudioDriverBinding = {
  HdaAudioDriverBindingSupported,
  HdaAudioDriverBindingStart,
  HdaAudioDriverBindingStop,
  0x10,
  NULL,
  NULL
};

/**
  The driver entry point.

  @param[in] ImageHandle  The firmware allocated handle for the EFI image.
  @param[in] SystemTable  A pointer to the EFI System Table.

  @retval EFI_SUCCESS  The driver binding and component name protocols
                       were installed.
  @retval Others       Installation failed.

**/
EFI_STATUS
EFIAPI
HdaAudioDxeEntryPoint (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  return EfiLibInstallDriverBindingComponentName2 (
           ImageHandle,
           SystemTable,
           &gHdaAudioDriverBinding,
           ImageHandle,
           &gHdaAudioComponentName,
           &gHdaAudioComponentName2
           );
}
