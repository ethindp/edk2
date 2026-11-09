/** @file
  UEFI Shell application that tests EDKII_SIMPLE_AUDIO_OUTPUT_PROTOCOL.

  Without arguments, the protocol tests run against the DefaultOutput of the
  audio output manager, or against the first audio output if there is no
  manager. Each test plays a tone at a different frequency.

  The following commands test the audio output manager:

    AudioOutputTest list           List the devices.
    AudioOutputTest prefer N|other Prefer device N, or the first inactive
                                   device.
    AudioOutputTest check [N]      Check that the preferred device is active.
    AudioOutputTest failover       Disconnect the active device and check
                                   that another device takes over.
    AudioOutputTest forget         Clear the preferred device.

  Copyright (c) 2026, Ethin Probst. All rights reserved.<BR>
  SPDX-License-Identifier: BSD-2-Clause-Patent

**/

#include <Uefi.h>

#include <Protocol/AudioOutputManager.h>
#include <Protocol/ComponentName.h>
#include <Protocol/ComponentName2.h>
#include <Protocol/LoadedImage.h>
#include <Protocol/PciIo.h>
#include <Protocol/SimpleAudioOutput.h>

#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DevicePathLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiLib.h>

#define MAX_ARGS  4

#define SAMPLE_RATE  48000

//
// 2 * pi in Q30 fixed point.
//
#define TWO_PI_Q30  6746518852LL
#define ONE_Q30     (1LL << 30)

STATIC UINTN  mFailures;

/**
  Report one check.

  @param[in] Name       What was checked.
  @param[in] Condition  TRUE if the check passed.

**/
STATIC
VOID
Check (
  IN CONST CHAR16  *Name,
  IN BOOLEAN       Condition
  )
{
  Print (L"%s: %s\n", Condition ? L"PASS" : L"FAIL", Name);
  if (!Condition) {
    mFailures++;
  }
}

/**
  Multiply two Q30 values.

  @param[in] A  Q30 value.
  @param[in] B  Q30 value.

  @return A * B in Q30.

**/
STATIC
INT64
MulQ30 (
  IN INT64  A,
  IN INT64  B
  )
{
  return DivS64x64Remainder (MultS64x64 (A, B), ONE_Q30, NULL);
}

/**
  Allocate and fill a buffer with a sine tone at half full scale.

  @param[in]  Frequency     Tone frequency in Hz.
  @param[in]  Channels      1 or 2.
  @param[in]  Milliseconds  Duration.
  @param[out] Size          Size of the returned buffer in bytes.

  @return The buffer (caller frees), or NULL.

**/
STATIC
INT16 *
MakeTone (
  IN  UINT32  Frequency,
  IN  UINT8   Channels,
  IN  UINT32  Milliseconds,
  OUT UINTN   *Size
  )
{
  INT16  *Samples;
  UINTN  Frames;
  UINTN  Frame;
  UINT8  Channel;
  INT64  X;
  INT64  X2;
  INT64  Cos;
  INT64  Sin;
  INT64  Prev;
  INT64  Curr;
  INT64  Next;
  INT64  Amplitude;

  Frames  = (UINTN)SAMPLE_RATE * Milliseconds / 1000;
  *Size   = Frames * Channels * sizeof (INT16);
  Samples = AllocatePool (*Size);
  if (Samples == NULL) {
    return NULL;
  }

  //
  // Angle step per sample, and its sine and cosine from a Taylor series.
  // The step is small, so three terms are enough.
  //
  X   = DivS64x64Remainder (MultS64x64 (TWO_PI_Q30, Frequency), SAMPLE_RATE, NULL);
  X2  = MulQ30 (X, X);
  Cos = ONE_Q30 - DivS64x64Remainder (X2, 2, NULL) +
        DivS64x64Remainder (MulQ30 (X2, X2), 24, NULL);
  Sin = X - DivS64x64Remainder (MulQ30 (X2, X), 6, NULL) +
        DivS64x64Remainder (MulQ30 (MulQ30 (X2, X2), X), 120, NULL);

  //
  // y[n] = 2 cos(x) y[n-1] - y[n-2], in units of 2^-16 of a sample.
  //
  Amplitude = 16384LL << 16;
  Prev      = 0;
  Curr      = MulQ30 (Amplitude, Sin);
  for (Frame = 0; Frame < Frames; Frame++) {
    for (Channel = 0; Channel < Channels; Channel++) {
      Samples[Frame * Channels + Channel] = (INT16)DivS64x64Remainder (Prev, 1 << 16, NULL);
    }

    Next = 2 * MulQ30 (Cos, Curr) - Prev;
    Prev = Curr;
    Curr = Next;
  }

  return Samples;
}

/**
  Play a tone, blocking.

  @param[in] Audio         The protocol.
  @param[in] Frequency     Tone frequency in Hz.
  @param[in] Milliseconds  Duration.

  @return The status from Play().

**/
STATIC
EFI_STATUS
PlayToneBlocking (
  IN EDKII_SIMPLE_AUDIO_OUTPUT_PROTOCOL  *Audio,
  IN UINT32                              Frequency,
  IN UINT32                              Milliseconds
  )
{
  INT16       *Samples;
  UINTN       Size;
  EFI_STATUS  Status;

  Samples = MakeTone (Frequency, Audio->Mode->Format.Channels, Milliseconds, &Size);
  if (Samples == NULL) {
    return EFI_OUT_OF_RESOURCES;
  }

  Status = Audio->Play (Audio, Samples, Size, NULL);
  FreePool (Samples);
  return Status;
}

/**
  Run the protocol test suite against one instance.

  @param[in] Audio  The instance to test.

**/
STATIC
VOID
RunBaseTests (
  IN EDKII_SIMPLE_AUDIO_OUTPUT_PROTOCOL  *Audio
  )
{
  EFI_STATUS                 Status;
  EDKII_AUDIO_OUTPUT_FORMAT  Format;
  EDKII_AUDIO_OUTPUT_TOKEN   Token;
  UINT32                     ModeNumber;
  INT16                      *Samples;
  UINTN                      Size;
  UINTN                      Index;
  EFI_EVENT                  MinTimer;
  EFI_EVENT                  MaxTimer;

  Print (
    L"Revision 0x%lx, %u modes, volume %u, muted %u\n",
    Audio->Revision,
    Audio->Mode->MaxMode,
    Audio->Mode->Volume,
    Audio->Mode->Muted
    );
  for (ModeNumber = 0; ModeNumber < Audio->Mode->MaxMode; ModeNumber++) {
    if (!EFI_ERROR (Audio->QueryMode (Audio, ModeNumber, &Format))) {
      Print (L"  mode %u: %u Hz, %u ch, %u bit\n", ModeNumber, Format.SampleRate, Format.Channels, Format.BitsPerSample);
    }
  }

  Check (L"at least the two baseline modes", (BOOLEAN)(Audio->Mode->MaxMode >= 2));
  Check (L"QueryMode rejects MaxMode", (BOOLEAN)(Audio->QueryMode (Audio, Audio->Mode->MaxMode, &Format) == EFI_INVALID_PARAMETER));
  Check (L"SetVolume rejects 101", (BOOLEAN)(Audio->SetVolume (Audio, 101) == EFI_INVALID_PARAMETER));
  Check (L"Reset", (BOOLEAN)(Audio->Reset (Audio) == EFI_SUCCESS));
  Check (L"SetVolume 100", (BOOLEAN)(Audio->SetVolume (Audio, 100) == EFI_SUCCESS));

  //
  // A 1.0 s tone must block for 0.9 s to 2.5 s.
  //
  gBS->CreateEvent (EVT_TIMER, TPL_CALLBACK, NULL, NULL, &MinTimer);
  gBS->CreateEvent (EVT_TIMER, TPL_CALLBACK, NULL, NULL, &MaxTimer);
  gBS->SetTimer (MinTimer, TimerRelative, 9000000);
  gBS->SetTimer (MaxTimer, TimerRelative, 25000000);
  Status = PlayToneBlocking (Audio, 440, 1000);
  Check (L"1: 440 Hz stereo blocking", (BOOLEAN)(Status == EFI_SUCCESS));
  Check (L"1: blocked at least 0.9 s", (BOOLEAN)(gBS->CheckEvent (MinTimer) == EFI_SUCCESS));
  Check (L"1: returned within 2.5 s", (BOOLEAN)(gBS->CheckEvent (MaxTimer) == EFI_NOT_READY));
  gBS->CloseEvent (MinTimer);
  gBS->CloseEvent (MaxTimer);

  Check (L"SetMode mono", (BOOLEAN)(Audio->SetMode (Audio, EDKII_AUDIO_OUTPUT_MODE_MONO_48K) == EFI_SUCCESS));
  Check (L"mode reports 1 channel", (BOOLEAN)(Audio->Mode->Format.Channels == 1));
  Check (L"Play rejects odd size", (BOOLEAN)(Audio->Play (Audio, &Format, 3, NULL) == EFI_INVALID_PARAMETER));
  Check (L"2: 660 Hz mono blocking", (BOOLEAN)(PlayToneBlocking (Audio, 660, 500) == EFI_SUCCESS));
  Check (L"SetMode stereo", (BOOLEAN)(Audio->SetMode (Audio, EDKII_AUDIO_OUTPUT_MODE_STEREO_48K) == EFI_SUCCESS));

  Samples = MakeTone (880, 2, 1000, &Size);
  Status  = gBS->CreateEvent (0, TPL_CALLBACK, NULL, NULL, &Token.Event);
  if ((Samples == NULL) || EFI_ERROR (Status)) {
    Check (L"3: setup", FALSE);
    return;
  }

  Token.TransactionStatus = EFI_NOT_READY;
  Status                  = Audio->Play (Audio, Samples, Size, &Token);
  Check (L"3: 880 Hz async started", (BOOLEAN)(Status == EFI_SUCCESS));
  Check (L"3: second Play is EFI_NOT_READY", (BOOLEAN)(Audio->Play (Audio, Samples, Size, NULL) == EFI_NOT_READY));
  Check (L"3: SetMode while playing is EFI_NOT_READY", (BOOLEAN)(Audio->SetMode (Audio, 1) == EFI_NOT_READY));
  Check (L"3: not finished immediately", (BOOLEAN)(gBS->CheckEvent (Token.Event) == EFI_NOT_READY));
  gBS->WaitForEvent (1, &Token.Event, &Index);
  Check (L"3: completed with EFI_SUCCESS", (BOOLEAN)(Token.TransactionStatus == EFI_SUCCESS));
  FreePool (Samples);

  Samples                 = MakeTone (220, 2, 2000, &Size);
  Token.TransactionStatus = EFI_NOT_READY;
  Status                  = Audio->Play (Audio, Samples, Size, &Token);
  Check (L"4: 220 Hz async started", (BOOLEAN)(Status == EFI_SUCCESS));
  gBS->Stall (300000);
  Check (L"4: Stop", (BOOLEAN)(Audio->Stop (Audio) == EFI_SUCCESS));
  gBS->WaitForEvent (1, &Token.Event, &Index);
  Check (L"4: completed with EFI_ABORTED", (BOOLEAN)(Token.TransactionStatus == EFI_ABORTED));
  Check (L"4: Stop when idle", (BOOLEAN)(Audio->Stop (Audio) == EFI_SUCCESS));
  FreePool (Samples);
  gBS->CloseEvent (Token.Event);

  Check (L"SetVolume 25", (BOOLEAN)(Audio->SetVolume (Audio, 25) == EFI_SUCCESS));
  Check (L"5: 440 Hz at volume 25", (BOOLEAN)(PlayToneBlocking (Audio, 440, 500) == EFI_SUCCESS));
  Check (L"SetMute TRUE", (BOOLEAN)(Audio->SetMute (Audio, TRUE) == EFI_SUCCESS));
  Check (L"mode reports muted, volume kept", (BOOLEAN)(Audio->Mode->Muted && Audio->Mode->Volume == 25));
  Check (L"6: 440 Hz muted", (BOOLEAN)(PlayToneBlocking (Audio, 440, 500) == EFI_SUCCESS));
  Audio->SetMute (Audio, FALSE);
  Audio->SetVolume (Audio, 100);
}

/**
  Print a device's name, as its driver reports it.

  @param[in] Device  A device handle.

**/
STATIC
VOID
PrintDeviceName (
  IN EFI_HANDLE  Device
  )
{
  EFI_STATUS                           Status;
  EFI_DEVICE_PATH_PROTOCOL             *DevicePath;
  EFI_HANDLE                           Parent;
  EFI_OPEN_PROTOCOL_INFORMATION_ENTRY  *Entries;
  UINTN                                EntryCount;
  UINTN                                Index;
  EFI_COMPONENT_NAME2_PROTOCOL         *ComponentName2;
  EFI_COMPONENT_NAME_PROTOCOL          *ComponentName;
  CHAR16                               *Name;

  //
  // Ask the driver that created the device, which has the PCI I/O protocol
  // of the parent open BY_CHILD_CONTROLLER. Some drivers return names for
  // controllers they do not manage, so other drivers are not asked.
  //
  DevicePath = DevicePathFromHandle (Device);
  Status     = gBS->LocateDevicePath (&gEfiPciIoProtocolGuid, &DevicePath, &Parent);
  if (!EFI_ERROR (Status)) {
    Status = gBS->OpenProtocolInformation (Parent, &gEfiPciIoProtocolGuid, &Entries, &EntryCount);
  }

  if (EFI_ERROR (Status)) {
    Print (L"(unnamed)");
    return;
  }

  for (Index = 0; Index < EntryCount; Index++) {
    if ((Entries[Index].ControllerHandle != Device) ||
        ((Entries[Index].Attributes & EFI_OPEN_PROTOCOL_BY_CHILD_CONTROLLER) == 0))
    {
      continue;
    }

    //
    // Platforms may disable either Component Name protocol, so try both.
    //
    if ((!EFI_ERROR (gBS->HandleProtocol (Entries[Index].AgentHandle, &gEfiComponentName2ProtocolGuid, (VOID **)&ComponentName2)) &&
         !EFI_ERROR (ComponentName2->GetControllerName (ComponentName2, Parent, Device, "en", &Name))) ||
        (!EFI_ERROR (gBS->HandleProtocol (Entries[Index].AgentHandle, &gEfiComponentNameProtocolGuid, (VOID **)&ComponentName)) &&
         !EFI_ERROR (ComponentName->GetControllerName (ComponentName, Parent, Device, "eng", &Name))))
    {
      Print (L"%s", Name);
      FreePool (Entries);
      return;
    }
  }

  FreePool (Entries);
  Print (L"(unnamed)");
}

/**
  Compare two device paths.

  @param[in] A  A device path, or NULL.
  @param[in] B  A device path, or NULL.

  @retval TRUE   Both are non-NULL and equal.
  @retval FALSE  Otherwise.

**/
STATIC
BOOLEAN
SameDevicePath (
  IN EFI_DEVICE_PATH_PROTOCOL  *A,
  IN EFI_DEVICE_PATH_PROTOCOL  *B
  )
{
  return (BOOLEAN)((A != NULL) && (B != NULL) &&
                   (GetDevicePathSize (A) == GetDevicePathSize (B)) &&
                   (CompareMem (A, B, GetDevicePathSize (A)) == 0));
}

/**
  Return the device path of the active device.

  @param[in] Manager  The manager.

  @return The device path (owned by the device), or NULL if none is active.

**/
STATIC
EFI_DEVICE_PATH_PROTOCOL *
ActiveDevicePath (
  IN EDKII_AUDIO_OUTPUT_MANAGER_PROTOCOL  *Manager
  )
{
  EFI_HANDLE  Active;

  if (EFI_ERROR (Manager->GetActiveDevice (Manager, &Active))) {
    return NULL;
  }

  return DevicePathFromHandle (Active);
}

/**
  List devices, marking the active and preferred ones.

  @param[in]  Manager  The manager.
  @param[out] Count    Number of devices.

  @return The device handles (caller frees), or NULL.

**/
STATIC
EFI_HANDLE *
ListDevices (
  IN  EDKII_AUDIO_OUTPUT_MANAGER_PROTOCOL  *Manager,
  OUT UINTN                                *Count
  )
{
  EFI_HANDLE                          *Handles;
  EFI_HANDLE                          Active;
  EFI_DEVICE_PATH_PROTOCOL            *Preferred;
  EDKII_SIMPLE_AUDIO_OUTPUT_PROTOCOL  *Audio;
  CHAR16                              *Text;
  UINTN                               Index;

  if (EFI_ERROR (Manager->GetDevices (Manager, Count, &Handles))) {
    *Count = 0;
    return NULL;
  }

  if (EFI_ERROR (Manager->GetActiveDevice (Manager, &Active))) {
    Active = NULL;
  }

  if (EFI_ERROR (Manager->GetPreferredDevice (Manager, &Preferred))) {
    Preferred = NULL;
  }

  for (Index = 0; Index < *Count; Index++) {
    gBS->HandleProtocol (Handles[Index], &gEdkiiSimpleAudioOutputProtocolGuid, (VOID **)&Audio);
    Print (L"  device %u: ", Index);
    PrintDeviceName (Handles[Index]);
    Text = ConvertDevicePathToText (DevicePathFromHandle (Handles[Index]), FALSE, FALSE);
    Print (
      L" type %u presence %u%s%s\n    %s\n",
      Audio->Mode->OutputType,
      Audio->Mode->Presence,
      (Handles[Index] == Active) ? L" [active]" : L"",
      SameDevicePath (Preferred, DevicePathFromHandle (Handles[Index])) ? L" [preferred]" : L"",
      (Text != NULL) ? Text : L"(no device path)"
      );
    if (Text != NULL) {
      FreePool (Text);
    }
  }

  if (Preferred != NULL) {
    FreePool (Preferred);
  }

  return Handles;
}

/**
  Disconnect the controller of the active device and check that another
  device is used. Then reconnect it and check that the preferred device is
  active again.

  @param[in] Manager  The manager.

**/
STATIC
VOID
TestFailover (
  IN EDKII_AUDIO_OUTPUT_MANAGER_PROTOCOL  *Manager
  )
{
  EFI_DEVICE_PATH_PROTOCOL  *Original;
  EFI_DEVICE_PATH_PROTOCOL  *Remaining;
  EFI_DEVICE_PATH_PROTOCOL  *Now;
  EFI_HANDLE                Controller;

  Original = DuplicateDevicePath (ActiveDevicePath (Manager));
  if (Original == NULL) {
    Check (L"failover: a device is active", FALSE);
    return;
  }

  Remaining = Original;
  Check (
    L"failover: found the active device's controller",
    (BOOLEAN) !EFI_ERROR (gBS->LocateDevicePath (&gEfiPciIoProtocolGuid, &Remaining, &Controller))
    );

  Check (L"failover: disconnect", (BOOLEAN) !EFI_ERROR (gBS->DisconnectController (Controller, NULL, NULL)));
  Now = ActiveDevicePath (Manager);
  Check (L"failover: another device took over", (BOOLEAN)((Now != NULL) && !SameDevicePath (Now, Original)));
  Check (L"failover: 880 Hz on the fallback", (BOOLEAN)(PlayToneBlocking (Manager->DefaultOutput, 880, 500) == EFI_SUCCESS));

  Check (L"failover: reconnect", (BOOLEAN) !EFI_ERROR (gBS->ConnectController (Controller, NULL, NULL, TRUE)));
  Check (L"failover: original device is back", SameDevicePath (ActiveDevicePath (Manager), Original));
  Check (L"failover: 220 Hz on the original", (BOOLEAN)(PlayToneBlocking (Manager->DefaultOutput, 220, 500) == EFI_SUCCESS));

  FreePool (Original);
}

/**
  Split the image's load options into words.

  @param[in]  ImageHandle  The image handle.
  @param[out] Argv         Receives up to MAX_ARGS words, in place.

  @return The number of words, including the program name.

**/
STATIC
UINTN
ParseArgs (
  IN  EFI_HANDLE  ImageHandle,
  OUT CHAR16      **Argv
  )
{
  EFI_LOADED_IMAGE_PROTOCOL  *LoadedImage;
  CHAR16                     *Cursor;
  CHAR16                     *End;
  UINTN                      Argc;

  if (EFI_ERROR (gBS->HandleProtocol (ImageHandle, &gEfiLoadedImageProtocolGuid, (VOID **)&LoadedImage)) ||
      (LoadedImage->LoadOptions == NULL))
  {
    return 0;
  }

  Cursor = (CHAR16 *)LoadedImage->LoadOptions;
  End    = Cursor + LoadedImage->LoadOptionsSize / sizeof (CHAR16);
  Argc   = 0;
  while ((Cursor < End) && (*Cursor != L'\0') && (Argc < MAX_ARGS)) {
    while ((Cursor < End) && (*Cursor == L' ')) {
      Cursor++;
    }

    if ((Cursor >= End) || (*Cursor == L'\0')) {
      break;
    }

    Argv[Argc++] = Cursor;
    while ((Cursor < End) && (*Cursor != L' ') && (*Cursor != L'\0')) {
      Cursor++;
    }

    if ((Cursor < End) && (*Cursor == L' ')) {
      *Cursor++ = L'\0';
    }
  }

  return Argc;
}

/**
  The application entry point.

  @param[in] ImageHandle  The image handle.
  @param[in] SystemTable  The system table.

  @retval EFI_SUCCESS            All checks passed.
  @retval EFI_NOT_FOUND          No audio output protocol is installed.
  @retval EFI_INVALID_PARAMETER  The command line was not understood.
  @retval EFI_ABORTED            At least one check failed.

**/
EFI_STATUS
EFIAPI
AudioOutputTestMain (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  EFI_STATUS                           Status;
  EDKII_AUDIO_OUTPUT_MANAGER_PROTOCOL  *Manager;
  EDKII_SIMPLE_AUDIO_OUTPUT_PROTOCOL   *Audio;
  EFI_DEVICE_PATH_PROTOCOL             *Preferred;
  CHAR16                               *Argv[MAX_ARGS];
  UINTN                                Argc;
  EFI_HANDLE                           *Handles;
  UINTN                                Count;
  UINTN                                Number;

  Argc = ParseArgs (ImageHandle, Argv);

  if (EFI_ERROR (gBS->LocateProtocol (&gEdkiiAudioOutputManagerProtocolGuid, NULL, (VOID **)&Manager))) {
    Manager = NULL;
    Status  = gBS->LocateProtocol (&gEdkiiSimpleAudioOutputProtocolGuid, NULL, (VOID **)&Audio);
    if (EFI_ERROR (Status)) {
      Print (L"No audio output found: %r\n", Status);
      return EFI_NOT_FOUND;
    }

    Print (L"No audio output manager; testing the first output\n");
  } else {
    Audio = Manager->DefaultOutput;
    Print (L"Audio output manager found; testing DefaultOutput\n");
  }

  if (Argc < 2) {
    RunBaseTests (Audio);
  } else if (Manager == NULL) {
    Print (L"'%s' needs the audio output manager\n", Argv[1]);
    return EFI_NOT_FOUND;
  } else {
    Handles = ListDevices (Manager, &Count);
    Number  = (Argc >= 3) ? StrDecimalToUintn (Argv[2]) : 0;

    if (StrCmp (Argv[1], L"list") == 0) {
      Check (L"list: at least one device", (BOOLEAN)(Count > 0));
    } else if (StrCmp (Argv[1], L"prefer") == 0) {
      if ((Argc >= 3) && (StrCmp (Argv[2], L"other") == 0)) {
        for (Number = 0; Number < Count; Number++) {
          if (!SameDevicePath (ActiveDevicePath (Manager), DevicePathFromHandle (Handles[Number]))) {
            break;
          }
        }

        Print (L"  preferring device %u\n", Number);
      }

      Check (L"prefer: device exists", (BOOLEAN)(Number < Count));
      if (Number < Count) {
        Check (L"prefer: SetPreferredDevice", (BOOLEAN) !EFI_ERROR (Manager->SetPreferredDevice (Manager, Handles[Number])));
        Check (L"prefer: it is active", SameDevicePath (ActiveDevicePath (Manager), DevicePathFromHandle (Handles[Number])));
        Check (L"prefer: 440 Hz", (BOOLEAN)(PlayToneBlocking (Audio, 440, 500) == EFI_SUCCESS));
      }
    } else if (StrCmp (Argv[1], L"check") == 0) {
      Status = Manager->GetPreferredDevice (Manager, &Preferred);
      Check (L"check: preference saved", (BOOLEAN) !EFI_ERROR (Status));
      if (!EFI_ERROR (Status)) {
        if (Argc >= 3) {
          Check (
            L"check: device N is the preferred device",
            (BOOLEAN)((Number < Count) && SameDevicePath (Preferred, DevicePathFromHandle (Handles[Number])))
            );
        }

        Check (L"check: preferred device is active", SameDevicePath (ActiveDevicePath (Manager), Preferred));
        FreePool (Preferred);
      }

      Check (L"check: 660 Hz", (BOOLEAN)(PlayToneBlocking (Audio, 660, 500) == EFI_SUCCESS));
    } else if (StrCmp (Argv[1], L"failover") == 0) {
      TestFailover (Manager);
    } else if (StrCmp (Argv[1], L"forget") == 0) {
      Check (L"forget: SetPreferredDevice (NULL)", (BOOLEAN) !EFI_ERROR (Manager->SetPreferredDevice (Manager, NULL)));
    } else {
      Print (L"Unknown command '%s'\n", Argv[1]);
      return EFI_INVALID_PARAMETER;
    }

    if (Handles != NULL) {
      FreePool (Handles);
    }
  }

  Print (L"%u failure(s)\n", mFailures);
  return (mFailures == 0) ? EFI_SUCCESS : EFI_ABORTED;
}
