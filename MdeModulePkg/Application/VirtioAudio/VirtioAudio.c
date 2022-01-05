/*
Initializes a Virtio audio device

Copyright (C) 2021 Ethin Probst

Spdx-License-Identifier: BSD-2-Clause-Patent
*/

#include <Uefi.h>
#include <Library/UefiLib.h>
#include <Library/UefiApplicationEntryPoint.h>
#include <Library/UefiBootServicesTableLib.h>
#include <IndustryStandard/Virtio.h>
#include <Library/VirtioLib.h>
#include "VirtioAudio.h"

EFI_STATUS EFIAPI UefiMain(IN EFI_HANDLE imageHandle, IN EFI_SYSTEM_TABLE* st) {
UINT16 QueueSizes[4] = {0};
  Print(L"Attempting to find VirtIO protocol\n");
  UINTN numHandles = 0;
  EFI_HANDLE* handles = NULL;
  EFI_STATUS status = st->BootServices->LocateHandleBuffer(ByProtocol, &gVirtioDeviceProtocolGuid, NULL, &numHandles, &handles);
  if (EFI_ERROR(status)) {
    Print(L"Cannot find any handles for VirtIO devices, reason: %r\n", status);
    return EFI_ABORTED;
  }
  Print(L"Found %d VirtIO devices; enumerating\n", numHandles);
  for (UINTN i = 0; i < numHandles; ++i) {
    VIRTIO_DEVICE_PROTOCOL    *VirtIo = NULL;
    Print(L"Trying to open handle %d (%x)... ", i, handles[i]);
    status = st->BootServices->OpenProtocol(handles[i], &gVirtioProtocolGuid, (void**)&VirtIo, imageHandle, NULL, EFI_OPEN_PROTOCOL_EXCLUSIVE);
    if (EFI_ERROR(status)) {
      Print(L"error: %r\n", status);
      return EFI_ABORTED;
    }
    Print(L"OK\n");
    if (VirtIo->SubSystemDeviceId != 0x19) {
        Print(L"Not a VirtIO sound device\n");
        goto TerminateHandlesOnly;
    }
    VIRTIO_SND_DEV Dev = {0};
    dev.VirtIo = VirtIo;
    if (Dev.VirtIo->Revision < VIRTIO_SPEC_REVISION (1, 1, 0)) {
      print(L"Unsupported VirtIO version\n");
      goto TerminateOnlyHandles;
    }
    Print(L"Resetting device... ");
    UINT8 NextDevStat = 0;
    status = Dev.VirtIo->SetDeviceStatus (Dev.VirtIo, NextDevStat);
    if (EFI_ERROR(status))
      goto TerminateHandlesOnly;

    Print(L"OK\n");
    Print(L"Acknowledging device... ");
    NextDevStat |= VSTAT_ACK;
    status = Dev.VirtIo->SetDeviceStatus (Dev.VirtIo, NextDevStat);
    if (EFI_ERROR(status))
      goto TerminateOnlyHandles;

    Print(L"OK\n");
    Print(L"Indicating that we can drive this device... ");
    NextDevStat |= VSTAT_DRIVER;
    status = Dev.VirtIo->SetDeviceStatus (Dev.VirtIo, NextDevStat);
    if (EFI_ERROR(status))
      goto TerminateOnlyHandles;

    Print(L"OK\n");
    Print(L"Setting page size to %d bytes... ", EFI_PAGE_SIZE);
    status = Dev.VirtIo->SetPageSize (Dev.VirtIo, EFI_PAGE_SIZE);
    if (EFI_ERROR(status))
      goto TerminateOnlyHandles;

    Print(L"OK\n");
    Print(L"Reading features... ");
    UINT64 Features = 0;
    status = Dev.VirtIo->GetDeviceFeatures (Dev.VirtIo, &Features);
    if (EFI_ERROR(status))
      goto TerminateHandlesOnly;

    Print(L"OK: %x\n", Features);
    Print(L"Declaring features... ");
    Features &= VIRTIO_F_VERSION_1 | VIRTIO_F_IOMMU_PLATFORM;
    status = Virtio10WriteFeatures (Dev.VirtIo, Features, &NextDevStat);
    if (EFI_ERROR(status))
      goto TerminateHandlesOnly;

Print(L"OK\n");
    Print(L"Allocating VRINGs\n");
    for (UINT8 i = 0; i < 4; ++i) {
      Print(L"Allocating VRING %d\n", i);
      Print(L"Selecting queue %d... ", i);
      status = Dev.VirtIo->SetQueueSel (Dev.VirtIo, i);
      if (EFI_ERROR(status))
        goto TerminateHandlesOnly;

Print(L"OK\n");
Print(L"Retrieving queue size for queue %d... ", i);
        status = Dev.VirtIo->GetQueueNumMax (Dev.VirtIo, &QueueSize[i]);
        if (EFI_ERROR(status))
          goto TerminateHandlesOnly;

        Print(L"OK\n");
        Print(L"Mapping VRING %d... ", i);
        status = VirtioRingInit (Dev.VirtIo, QueueSize[i], &Dev.Rings[i]);
        if (EFI_ERROR(status))
          goto TerminateHandlesOnly;

        Print(L"OK\n");

