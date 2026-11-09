/** @file
  GUID and layout of the AudioOutputSettings variable, which holds the saved
  audio output volume, mute state, and preferred output device.

  Copyright (c) 2026, Ethin Probst. All rights reserved.<BR>
  SPDX-License-Identifier: BSD-2-Clause-Patent

**/

#pragma once

#define EDKII_AUDIO_OUTPUT_SETTINGS_VARIABLE_GUID \
  { \
    0xe0240242, 0xf3df, 0x43ee, { 0xbe, 0x4d, 0x1b, 0xee, 0x92, 0xf6, 0x9a, 0x77 } \
  }

#define EDKII_AUDIO_OUTPUT_SETTINGS_VARIABLE_NAME  L"AudioOutputSettings"

///
/// EFI_VARIABLE_NON_VOLATILE | EFI_VARIABLE_BOOTSERVICE_ACCESS |
/// EFI_VARIABLE_RUNTIME_ACCESS
///
#define EDKII_AUDIO_OUTPUT_SETTINGS_VARIABLE_ATTRIBUTES  0x00000007

#define EDKII_AUDIO_OUTPUT_SETTINGS_VERSION  1

#pragma pack(1)

///
/// Variable contents. When a preferred device is saved, its device path
/// immediately follows this header and the variable size covers it.
///
typedef struct {
  ///
  /// EDKII_AUDIO_OUTPUT_SETTINGS_VERSION.
  ///
  UINT32    Version;
  ///
  /// 0 to 100.
  ///
  UINT8     Volume;
  ///
  /// Non-zero if muted.
  ///
  UINT8     Muted;
  UINT16    Reserved;
} EDKII_AUDIO_OUTPUT_SETTINGS;

#pragma pack()

extern EFI_GUID  gEdkiiAudioOutputSettingsVariableGuid;
