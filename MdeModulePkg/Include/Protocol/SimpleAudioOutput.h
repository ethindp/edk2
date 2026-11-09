/** @file
  This file defines the EDK II Simple Audio Output Protocol, which plays raw
  PCM audio on one output of an audio device.

  Copyright (c) 2026, Ethin Probst. All rights reserved.<BR>
  SPDX-License-Identifier: BSD-2-Clause-Patent

**/

#pragma once

#define EDKII_SIMPLE_AUDIO_OUTPUT_PROTOCOL_GUID \
  { \
    0xef467623, 0xff43, 0x4672, { 0x9b, 0x82, 0x3d, 0xb6, 0x48, 0x08, 0xcc, 0xd1 } \
  }

#define EDKII_SIMPLE_AUDIO_OUTPUT_PROTOCOL_REVISION  0x00010000

typedef struct _EDKII_SIMPLE_AUDIO_OUTPUT_PROTOCOL EDKII_SIMPLE_AUDIO_OUTPUT_PROTOCOL;

///
/// Baseline modes. Every implementation MUST report these two modes with
/// these numbers. Additional modes, if any, start at
/// EDKII_AUDIO_OUTPUT_MODE_FIRST_OPTIONAL.
///
/// Mode 0: 48000 Hz, 2 channels, signed 16-bit little-endian, interleaved.
/// Mode 1: 48000 Hz, 1 channel,  signed 16-bit little-endian.
///
#define EDKII_AUDIO_OUTPUT_MODE_STEREO_48K      0
#define EDKII_AUDIO_OUTPUT_MODE_MONO_48K        1
#define EDKII_AUDIO_OUTPUT_MODE_FIRST_OPTIONAL  2

///
/// Description of a PCM stream format.
///
/// Samples are signed, little-endian and BitsPerSample wide. Channels are
/// interleaved, left first. In revision 1 of this protocol BitsPerSample is
/// always 16.
///
typedef struct {
  ///
  /// Frames per second, in Hz.
  ///
  UINT32    SampleRate;
  ///
  /// Number of interleaved channels: 1 (mono) or 2 (stereo).
  ///
  UINT8     Channels;
  ///
  /// Bits per sample. Always 16 in revision 1.
  ///
  UINT8     BitsPerSample;
  ///
  /// Reserved. Must be zero.
  ///
  UINT16    Reserved;
} EDKII_AUDIO_OUTPUT_FORMAT;

//
// Type of output (Mode->OutputType).
//
#define EDKII_AUDIO_OUTPUT_TYPE_UNKNOWN     0
#define EDKII_AUDIO_OUTPUT_TYPE_SPEAKER     1
#define EDKII_AUDIO_OUTPUT_TYPE_HEADPHONES  2
#define EDKII_AUDIO_OUTPUT_TYPE_LINE_OUT    3
#define EDKII_AUDIO_OUTPUT_TYPE_DIGITAL     4

//
// Whether something is connected to the output (Mode->Presence).
// Built-in speakers report CONNECTED. Jacks without presence detection
// report UNKNOWN.
//
#define EDKII_AUDIO_OUTPUT_PRESENCE_UNKNOWN       0
#define EDKII_AUDIO_OUTPUT_PRESENCE_CONNECTED     1
#define EDKII_AUDIO_OUTPUT_PRESENCE_DISCONNECTED  2

///
/// Current state of the device. Consumers must not modify it. The driver
/// updates it when SetMode(), SetVolume(), SetMute() or Reset() succeeds.
/// Presence is updated at least once a second while boot services run.
///
typedef struct {
  ///
  /// Number of modes supported. Valid mode numbers are 0 .. MaxMode - 1.
  /// Always at least 2.
  ///
  UINT32                       MaxMode;
  ///
  /// Current mode number.
  ///
  UINT32                       Mode;
  ///
  /// Format of the current mode.
  ///
  EDKII_AUDIO_OUTPUT_FORMAT    Format;
  ///
  /// Current volume, 0 (silent) to 100 (maximum).
  ///
  UINT8                        Volume;
  ///
  /// TRUE if output is muted. Muting does not change Volume.
  ///
  BOOLEAN                      Muted;
  ///
  /// EDKII_AUDIO_OUTPUT_TYPE_* value.
  ///
  UINT8                        OutputType;
  ///
  /// EDKII_AUDIO_OUTPUT_PRESENCE_* value.
  ///
  UINT8                        Presence;
} EDKII_SIMPLE_AUDIO_OUTPUT_MODE;

///
/// Token for asynchronous playback, in the style of EFI_BLOCK_IO2_TOKEN.
///
typedef struct {
  ///
  /// Signaled when playback of the buffer finishes, fails, or is stopped.
  ///
  EFI_EVENT     Event;
  ///
  /// Result of the playback, valid once Event is signaled:
  ///   EFI_SUCCESS       The whole buffer was played.
  ///   EFI_ABORTED       Playback was cancelled by Stop() or Reset().
  ///   EFI_DEVICE_ERROR  The device reported an error during playback.
  ///
  EFI_STATUS    TransactionStatus;
} EDKII_AUDIO_OUTPUT_TOKEN;

/**
  Reset the audio output device.

  Stops any playback in progress, reinitializes the hardware, and selects
  mode 0. The token of the stopped playback completes with EFI_ABORTED.
  Volume and mute are not changed.

  @param[in] This  A pointer to the EDKII_SIMPLE_AUDIO_OUTPUT_PROTOCOL instance.

  @retval EFI_SUCCESS       The device was reset.
  @retval EFI_DEVICE_ERROR  The device could not be reset.

**/
typedef
EFI_STATUS
(EFIAPI *EDKII_SIMPLE_AUDIO_OUTPUT_RESET)(
  IN EDKII_SIMPLE_AUDIO_OUTPUT_PROTOCOL  *This
  );

/**
  Return the format of a mode.

  @param[in]  This        A pointer to the EDKII_SIMPLE_AUDIO_OUTPUT_PROTOCOL instance.
  @param[in]  ModeNumber  The mode to query.
  @param[out] Format      On success, the format of ModeNumber.

  @retval EFI_SUCCESS            Format was filled in.
  @retval EFI_INVALID_PARAMETER  ModeNumber is not less than Mode->MaxMode, or
                                 Format is NULL.

**/
typedef
EFI_STATUS
(EFIAPI *EDKII_SIMPLE_AUDIO_OUTPUT_QUERY_MODE)(
  IN  EDKII_SIMPLE_AUDIO_OUTPUT_PROTOCOL  *This,
  IN  UINT32                              ModeNumber,
  OUT EDKII_AUDIO_OUTPUT_FORMAT           *Format
  );

/**
  Select the format that subsequent Play() calls use.

  @param[in] This        A pointer to the EDKII_SIMPLE_AUDIO_OUTPUT_PROTOCOL instance.
  @param[in] ModeNumber  The mode to select.

  @retval EFI_SUCCESS            The mode was selected.
  @retval EFI_INVALID_PARAMETER  ModeNumber is not less than Mode->MaxMode.
  @retval EFI_NOT_READY          Playback is in progress.
  @retval EFI_DEVICE_ERROR       The device could not be configured.

**/
typedef
EFI_STATUS
(EFIAPI *EDKII_SIMPLE_AUDIO_OUTPUT_SET_MODE)(
  IN EDKII_SIMPLE_AUDIO_OUTPUT_PROTOCOL  *This,
  IN UINT32                              ModeNumber
  );

/**
  Play a buffer of PCM samples in the current mode's format.

  If Token is NULL, the call blocks until the whole buffer has been played.

  If Token is not NULL, the call returns as soon as playback has started.
  Token->Event is signaled when playback ends, and Token->TransactionStatus
  holds the result. Buffer and Token must stay valid and unmodified until
  then.
  An implementation without real asynchronous support may play the buffer
  synchronously and signal Token->Event before returning.

  Only one playback may be in progress at a time. To interrupt a sound, call
  Stop() and then Play() again.

  This function must be called at TPL_CALLBACK or lower.

  @param[in]      This        A pointer to the EDKII_SIMPLE_AUDIO_OUTPUT_PROTOCOL instance.
  @param[in]      Buffer      The PCM samples to play.
  @param[in]      BufferSize  Size of Buffer in bytes. Must be a non-zero
                              multiple of the frame size
                              (Channels * BitsPerSample / 8).
  @param[in, out] Token       Optional token for asynchronous playback.

  @retval EFI_SUCCESS            The buffer was played if Token is NULL.
                                 Playback was started if Token is not NULL.
  @retval EFI_INVALID_PARAMETER  Buffer is NULL, BufferSize is zero or not a
                                 multiple of the frame size, or Token is not
                                 NULL and Token->Event is NULL.
  @retval EFI_NOT_READY          Another playback is in progress.
  @retval EFI_OUT_OF_RESOURCES   Resources for playback could not be allocated.
  @retval EFI_ABORTED            Token is NULL and Stop() or Reset() was called
                                 from an event notification during playback.
  @retval EFI_DEVICE_ERROR       The device reported an error.

**/
typedef
EFI_STATUS
(EFIAPI *EDKII_SIMPLE_AUDIO_OUTPUT_PLAY)(
  IN     EDKII_SIMPLE_AUDIO_OUTPUT_PROTOCOL  *This,
  IN     CONST VOID                          *Buffer,
  IN     UINTN                               BufferSize,
  IN OUT EDKII_AUDIO_OUTPUT_TOKEN            *Token  OPTIONAL
  );

/**
  Stop playback in progress.

  The pending token, if any, completes with EFI_ABORTED. Calling Stop() when
  nothing is playing is not an error.

  @param[in] This  A pointer to the EDKII_SIMPLE_AUDIO_OUTPUT_PROTOCOL instance.

  @retval EFI_SUCCESS       Playback is stopped.
  @retval EFI_DEVICE_ERROR  The device could not be stopped.

**/
typedef
EFI_STATUS
(EFIAPI *EDKII_SIMPLE_AUDIO_OUTPUT_STOP)(
  IN EDKII_SIMPLE_AUDIO_OUTPUT_PROTOCOL  *This
  );

/**
  Set the output volume.

  May be called at any time, including during playback.

  @param[in] This    A pointer to the EDKII_SIMPLE_AUDIO_OUTPUT_PROTOCOL instance.
  @param[in] Volume  0 (silent) to 100 (maximum).

  @retval EFI_SUCCESS            The volume was set.
  @retval EFI_INVALID_PARAMETER  Volume is greater than 100.
  @retval EFI_DEVICE_ERROR       The device could not be configured.

**/
typedef
EFI_STATUS
(EFIAPI *EDKII_SIMPLE_AUDIO_OUTPUT_SET_VOLUME)(
  IN EDKII_SIMPLE_AUDIO_OUTPUT_PROTOCOL  *This,
  IN UINT8                               Volume
  );

/**
  Mute or unmute the output.

  May be called at any time, including during playback. Does not change
  Mode->Volume.

  @param[in] This  A pointer to the EDKII_SIMPLE_AUDIO_OUTPUT_PROTOCOL instance.
  @param[in] Mute  TRUE to mute, FALSE to unmute.

  @retval EFI_SUCCESS       The mute state was set.
  @retval EFI_DEVICE_ERROR  The device could not be configured.

**/
typedef
EFI_STATUS
(EFIAPI *EDKII_SIMPLE_AUDIO_OUTPUT_SET_MUTE)(
  IN EDKII_SIMPLE_AUDIO_OUTPUT_PROTOCOL  *This,
  IN BOOLEAN                             Mute
  );

///
/// The EDK II Simple Audio Output Protocol.
///
/// Produced by an audio driver on a child handle, with a device path, for
/// each output it exposes. Consumers should prefer the DefaultOutput of
/// EDKII_AUDIO_OUTPUT_MANAGER_PROTOCOL when it is present.
///
struct _EDKII_SIMPLE_AUDIO_OUTPUT_PROTOCOL {
  ///
  /// EDKII_SIMPLE_AUDIO_OUTPUT_PROTOCOL_REVISION. Future revisions only add
  /// members at the end of this structure.
  ///
  UINT64                                  Revision;
  EDKII_SIMPLE_AUDIO_OUTPUT_RESET         Reset;
  EDKII_SIMPLE_AUDIO_OUTPUT_QUERY_MODE    QueryMode;
  EDKII_SIMPLE_AUDIO_OUTPUT_SET_MODE      SetMode;
  EDKII_SIMPLE_AUDIO_OUTPUT_PLAY          Play;
  EDKII_SIMPLE_AUDIO_OUTPUT_STOP          Stop;
  EDKII_SIMPLE_AUDIO_OUTPUT_SET_VOLUME    SetVolume;
  EDKII_SIMPLE_AUDIO_OUTPUT_SET_MUTE      SetMute;
  ///
  /// Current device state.
  ///
  EDKII_SIMPLE_AUDIO_OUTPUT_MODE          *Mode;
};

extern EFI_GUID  gEdkiiSimpleAudioOutputProtocolGuid;
