/** @file
  Identification of HD Audio implementations that deviate from the High
  Definition Audio Specification. We use PCI IDs because some quirks are
  needed before we can talk to the codecs.

  Copyright (c) 2026, Ethin Probst. All rights reserved.<BR>
  SPDX-License-Identifier: BSD-2-Clause-Patent

**/

#include "HdaAudioDxe.h"

#define HDA_ANY_ID  MAX_UINT16

typedef struct {
  HDA_PLATFORM    Platform;
  UINT16          VendorId;
  UINT16          DeviceId;
  UINT16          SubsystemVendorId;
  UINT16          SubsystemId;
  UINT32          Quirks;
} HDA_PLATFORM_ENTRY;

//
// First match wins.
//
STATIC CONST HDA_PLATFORM_ENTRY  mHdaPlatforms[] = {
  //
  // QEMU intel-hda (ICH6) and ich9-intel-hda. QEMU gives every PCI device
  // the Red Hat/Qumranet subsystem vendor.
  //
  {
    HdaPlatformQemu,
    HDA_ANY_ID,
    HDA_ANY_ID,
    0x1AF4,
    HDA_ANY_ID,
    HDA_QUIRK_RIRB_RESPONSE_COUNT | HDA_QUIRK_RING_SIZE_READ_ONLY
  },
  //
  // VirtualBox ICH6 and Sunrise Point controllers. The subsystem ID is the
  // ID of the emulated SigmaTel STAC9221 codec.
  //
  {
    HdaPlatformVirtualBox,
    0x8086,
    HDA_ANY_ID,
    0x8384,
    0x7680,
    HDA_QUIRK_CODEC_AMP_IGNORED | HDA_QUIRK_48K_UNADVERTISED | HDA_QUIRK_PRESENCE_UNRELIABLE
  },
};

STATIC CONST CHAR8  *mHdaPlatformNames[HdaPlatformMax] = {
  "generic",
  "QEMU",
  "VirtualBox",
};

/**
  Compare one ID against a table value.

  @param[in] Table   Table value, or HDA_ANY_ID.
  @param[in] Actual  Value read from the device.

  @retval TRUE   The ID matches.
  @retval FALSE  It does not.

**/
STATIC
BOOLEAN
HdaIdMatches (
  IN UINT16  Table,
  IN UINT16  Actual
  )
{
  return (BOOLEAN)((Table == HDA_ANY_ID) || (Table == Actual));
}

/**
  Identify the platform hosting the controller from its PCI IDs and set
  Controller->Platform and Controller->Quirks. This runs before the
  controller is initialized, because some quirks are needed to talk to the
  codecs.

  @param[in] Controller  The controller instance. PciIo must be valid.

**/
VOID
HdaDetectPlatform (
  IN HDA_CONTROLLER  *Controller
  )
{
  EFI_STATUS  Status;
  PCI_TYPE00  Pci;
  UINTN       Index;

  Controller->Platform = HdaPlatformGeneric;
  Controller->Quirks   = HDA_QUIRK_NONE;

  Status = Controller->PciIo->Pci.Read (
                                    Controller->PciIo,
                                    EfiPciIoWidthUint8,
                                    0,
                                    sizeof (Pci),
                                    &Pci
                                    );
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_WARN, "%a: PCI config read failed: %r\n", __func__, Status));
    return;
  }

  for (Index = 0; Index < ARRAY_SIZE (mHdaPlatforms); Index++) {
    if (HdaIdMatches (mHdaPlatforms[Index].VendorId, Pci.Hdr.VendorId) &&
        HdaIdMatches (mHdaPlatforms[Index].DeviceId, Pci.Hdr.DeviceId) &&
        HdaIdMatches (mHdaPlatforms[Index].SubsystemVendorId, Pci.Device.SubsystemVendorID) &&
        HdaIdMatches (mHdaPlatforms[Index].SubsystemId, Pci.Device.SubsystemID))
    {
      Controller->Platform = mHdaPlatforms[Index].Platform;
      Controller->Quirks   = mHdaPlatforms[Index].Quirks;
      break;
    }
  }

  DEBUG ((
    DEBUG_INFO,
    "%a: controller %04x:%04x subsystem %04x:%04x, platform %a, quirks 0x%x\n",
    __func__,
    Pci.Hdr.VendorId,
    Pci.Hdr.DeviceId,
    Pci.Device.SubsystemVendorID,
    Pci.Device.SubsystemID,
    mHdaPlatformNames[Controller->Platform],
    Controller->Quirks
    ));
}
