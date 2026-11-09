/** @file
  Header file for the Intel High Definition Audio output driver.

  Each analog output pin is a child of the controller handle. The children
  share one output stream. Playing on a child routes the codec to its pin.

  Copyright (c) 2026, Ethin Probst. All rights reserved.<BR>
  SPDX-License-Identifier: BSD-2-Clause-Patent

**/

#pragma once

#include <Uefi.h>

#include <IndustryStandard/HighDefinitionAudio.h>
#include <IndustryStandard/Pci.h>

#include <Guid/EventGroup.h>

#include <Protocol/ComponentName.h>
#include <Protocol/ComponentName2.h>
#include <Protocol/DevicePath.h>
#include <Protocol/DriverBinding.h>
#include <Protocol/PciIo.h>
#include <Protocol/SimpleAudioOutput.h>

#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/DevicePathLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/PrintLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiLib.h>

#define HDA_CONTROLLER_SIGNATURE  SIGNATURE_32 ('H', 'D', 'A', 'C')
#define HDA_OUTPUT_SIGNATURE      SIGNATURE_32 ('H', 'D', 'A', 'O')

#define HDA_BAR_INDEX  0

#define HDA_RESET_TIMEOUT_US  100000        ///< Controller and stream reset handshakes
#define HDA_RESET_HOLD_US     100           ///< Time CRST is held asserted
#define HDA_CODEC_WAKE_US     1000          ///< Spec minimum is 521 us (25 frames)
#define HDA_VERB_TIMEOUT_US   10000         ///< Per codec command
#define HDA_POWER_TIMEOUT_US  10000         ///< Widget power state transition
#define HDA_POLL_US           10            ///< Register polling interval
#define HDA_BLOCKING_POLL_US  1000          ///< Polling interval for blocking Play()

#define HDA_STREAM_TAG         1
#define HDA_HW_FORMAT          ((HDA_FMT_BITS_16 << HDA_FMT_BITS_SHIFT) | HDA_FMT_CHANNELS (2))
#define HDA_HW_FRAME_BYTES     4
#define HDA_RING_PERIODS       4
#define HDA_RING_PERIOD_BYTES  SIZE_16KB
#define HDA_RING_BYTES         (HDA_RING_PERIODS * HDA_RING_PERIOD_BYTES)
//
// Never write closer than this behind the hardware read position.
//
#define HDA_RING_GUARD_BYTES  HDA_RING_PERIOD_BYTES
//
// Silence played after the last sample. It lets data still buffered in the
// controller FIFO and the codec reach the output before the stream stops.
//
#define HDA_TAIL_BYTES  HDA_RING_PERIOD_BYTES
//
// Refill timer period, in 100 ns units (5 ms).
//
#define HDA_PUMP_PERIOD  50000
//
// Jack presence polling period, in 100 ns units (500 ms).
//
#define HDA_PRESENCE_PERIOD  5000000

//
// Unity gain for the software volume path, in Q16 fixed point.
//
#define HDA_SOFTWARE_GAIN_UNITY  0x10000

#define HDA_DEFAULT_VOLUME  80

#define HDA_MAX_PATH_NODES   6              ///< Pin, up to four mixers/selectors, DAC
#define HDA_MAX_OUTPUTS      16
#define HDA_MAX_CONNECTIONS  32
#define HDA_NO_GAIN_NODE     0xFF

#define HDA_OUTPUT_NAME_LENGTH  40

//
// Platform quirks, selected in HdaQuirks.c.
//
#define HDA_QUIRK_NONE  0

///
/// The controller stops fetching verbs after RINTCNT responses until
/// software clears RIRBSTS.RINTFL. It sets RINTFL only if RIRBCTL.RINTCTL is
/// set, and a RINTCNT of 0 means no responses instead of 256. Program
/// RINTCNT to 0xFF and set RINTCTL. No interrupt is delivered because
/// INTCTL.GIE stays clear. Seen on QEMU.
///
#define HDA_QUIRK_RIRB_RESPONSE_COUNT  BIT0

///
/// CORBSIZE and RIRBSIZE are read-only. Do not program the size fields.
/// Seen on QEMU.
///
#define HDA_QUIRK_RING_SIZE_READ_ONLY  BIT1

///
/// Codec amplifier gain and mute settings are ignored on most converters.
/// Use software volume and leave every amplifier at unity gain.
/// Seen on VirtualBox.
///
#define HDA_QUIRK_CODEC_AMP_IGNORED  BIT2

///
/// The converters do not report 48 kHz support but play 48 kHz streams.
/// Seen on VirtualBox.
///
#define HDA_QUIRK_48K_UNADVERTISED  BIT3

///
/// Pin sense returns fixed values, and every pin plays to the same host
/// device. Report presence as unknown. Seen on VirtualBox.
///
#define HDA_QUIRK_PRESENCE_UNRELIABLE  BIT4

typedef enum {
  HdaPlatformGeneric,
  HdaPlatformQemu,
  HdaPlatformVirtualBox,
  HdaPlatformMax
} HDA_PLATFORM;

///
/// One widget on an output path.
///
typedef struct {
  UINT8     Nid;
  UINT8     Type;
  ///
  /// Index, in this widget's connection list, of the next widget toward the
  /// DAC. Unused for the DAC itself.
  ///
  UINT8     ConnIndex;
  UINT8     NumConnections;
  UINT32    Caps;
  UINT32    InAmpCaps;
  UINT32    OutAmpCaps;
} HDA_PATH_NODE;

///
/// A route from a pin complex back to an audio output converter (DAC).
///
typedef struct {
  ///
  /// Nodes[0] is the pin, Nodes[Count - 1] is the DAC.
  ///
  UINT8            Count;
  ///
  /// Index of the node whose output amplifier is used for volume, or
  /// HDA_NO_GAIN_NODE.
  ///
  UINT8            GainNode;
  HDA_PATH_NODE    Nodes[HDA_MAX_PATH_NODES];
  UINT32           PinCaps;
  UINT32           PinConfig;
} HDA_OUTPUT_PATH;

typedef struct _HDA_CONTROLLER HDA_CONTROLLER;

///
/// One output pin, exposed as a child handle.
///
typedef struct {
  UINT32                                Signature;
  HDA_CONTROLLER                        *Controller;

  EFI_HANDLE                            Handle;
  EFI_DEVICE_PATH_PROTOCOL              *DevicePath;
  CHAR16                                Name[HDA_OUTPUT_NAME_LENGTH];
  EFI_UNICODE_STRING_TABLE              *NameTable;

  UINT8                                 Codec;
  UINT8                                 Afg;
  UINT32                                AfgInAmpCaps;
  UINT32                                AfgOutAmpCaps;
  HDA_OUTPUT_PATH                       Path;
  ///
  /// Order of preference among the outputs of the controller. Higher is
  /// preferred.
  ///
  UINT32                                Score;
  BOOLEAN                               HwVolume;
  BOOLEAN                               HwMute;
  BOOLEAN                               CanSensePresence;
  UINT32                                SoftwareGain;

  EDKII_SIMPLE_AUDIO_OUTPUT_PROTOCOL    Audio;
  EDKII_SIMPLE_AUDIO_OUTPUT_MODE        Mode;
} HDA_OUTPUT;

#define HDA_OUTPUT_FROM_AUDIO(a) \
  CR (a, HDA_OUTPUT, Audio, HDA_OUTPUT_SIGNATURE)

struct _HDA_CONTROLLER {
  UINT32                      Signature;

  EFI_HANDLE                  Handle;
  EFI_HANDLE                  DriverBindingHandle;
  EFI_PCI_IO_PROTOCOL         *PciIo;
  EFI_DEVICE_PATH_PROTOCOL    *DevicePath;
  UINT64                      OriginalPciAttributes;
  BOOLEAN                     PciAttributesSaved;

  UINT16                      Gcap;
  HDA_PLATFORM                Platform;
  UINT32                      Quirks;

  //
  // Command Outbound Ring Buffer and Response Inbound Ring Buffer. Both are
  // in one page of common buffer.
  //
  VOID                        *CmdBuffer;
  VOID                        *CmdMapping;
  EFI_PHYSICAL_ADDRESS        CmdDeviceAddress;
  volatile UINT32             *Corb;
  volatile HDA_RIRB_ENTRY     *Rirb;
  UINT16                      CorbEntries;
  UINT16                      RirbEntries;
  UINT16                      CorbWritePointer;
  UINT16                      RirbReadPointer;

  //
  // Outputs, best first. Entries are NULL once their child is stopped.
  //
  UINT8                       OutputCount;
  HDA_OUTPUT                  *Outputs[HDA_MAX_OUTPUTS];
  ///
  /// The output the codec is currently routed to, or NULL.
  ///
  HDA_OUTPUT                  *RoutedOutput;

  //
  // Output stream. The first page of the buffer holds the BDL, and the
  // cyclic sample ring follows it.
  //
  UINT32                      StreamBase;
  BOOLEAN                     StreamIsBidirectional;
  VOID                        *StreamBuffer;
  UINTN                       StreamPages;
  VOID                        *StreamMapping;
  EFI_PHYSICAL_ADDRESS        StreamDeviceAddress;
  HDA_BDL_ENTRY               *Bdl;
  UINT8                       *Ring;

  //
  // Playback state. It is shared with the refill timer and must only be
  // accessed at TPL_NOTIFY.
  //
  EFI_EVENT                   PumpEvent;
  EFI_EVENT                   PresenceEvent;
  EFI_EVENT                   ExitBootServicesEvent;
  volatile BOOLEAN            Playing;
  HDA_OUTPUT                  *PlayingOutput;
  CONST UINT8                 *Source;
  UINTN                       SourceSize;
  UINTN                       SourceOffset;
  UINT8                       SourceChannels;
  UINT64                      RingBytesTotal;
  UINT64                      RingBytesWritten;
  UINT64                      RingBytesPlayed;
  UINT32                      LastLpib;
  EDKII_AUDIO_OUTPUT_TOKEN    *Token;
  EFI_STATUS                  PlaybackStatus;

  EFI_UNICODE_STRING_TABLE    *ControllerNameTable;
};

//
// Global variables
//
extern EFI_DRIVER_BINDING_PROTOCOL   gHdaAudioDriverBinding;
extern EFI_COMPONENT_NAME_PROTOCOL   gHdaAudioComponentName;
extern EFI_COMPONENT_NAME2_PROTOCOL  gHdaAudioComponentName2;

//
// HdaController.c
//

/**
  Read an 8-bit controller register.

  @param[in] Controller  The controller instance.
  @param[in] Offset      Register offset in BAR 0.

  @return The register value, or 0xFF if the read failed.

**/
UINT8
HdaRead8 (
  IN HDA_CONTROLLER  *Controller,
  IN UINT32          Offset
  );

/**
  Read a 16-bit controller register.

  @param[in] Controller  The controller instance.
  @param[in] Offset      Register offset in BAR 0.

  @return The register value, or 0xFFFF if the read failed.

**/
UINT16
HdaRead16 (
  IN HDA_CONTROLLER  *Controller,
  IN UINT32          Offset
  );

/**
  Read a 32-bit controller register.

  @param[in] Controller  The controller instance.
  @param[in] Offset      Register offset in BAR 0.

  @return The register value, or 0xFFFFFFFF if the read failed.

**/
UINT32
HdaRead32 (
  IN HDA_CONTROLLER  *Controller,
  IN UINT32          Offset
  );

/**
  Write an 8-bit controller register.

  @param[in] Controller  The controller instance.
  @param[in] Offset      Register offset in BAR 0.
  @param[in] Value       The value to write.

**/
VOID
HdaWrite8 (
  IN HDA_CONTROLLER  *Controller,
  IN UINT32          Offset,
  IN UINT8           Value
  );

/**
  Write a 16-bit controller register.

  @param[in] Controller  The controller instance.
  @param[in] Offset      Register offset in BAR 0.
  @param[in] Value       The value to write.

**/
VOID
HdaWrite16 (
  IN HDA_CONTROLLER  *Controller,
  IN UINT32          Offset,
  IN UINT16          Value
  );

/**
  Write a 32-bit controller register.

  @param[in] Controller  The controller instance.
  @param[in] Offset      Register offset in BAR 0.
  @param[in] Value       The value to write.

**/
VOID
HdaWrite32 (
  IN HDA_CONTROLLER  *Controller,
  IN UINT32          Offset,
  IN UINT32          Value
  );

/**
  Poll an 8-bit register until (Register & Mask) == Value.

  @param[in] Controller  The controller instance.
  @param[in] Offset      Register offset in BAR 0.
  @param[in] Mask        Bits to test.
  @param[in] Value       Expected value of the tested bits.
  @param[in] TimeoutUs   Timeout in microseconds.

  @retval EFI_SUCCESS  The bits reached the expected value.
  @retval EFI_TIMEOUT  The timeout expired first.

**/
EFI_STATUS
HdaWaitReg8 (
  IN HDA_CONTROLLER  *Controller,
  IN UINT32          Offset,
  IN UINT8           Mask,
  IN UINT8           Value,
  IN UINTN           TimeoutUs
  );

/**
  Poll a 16-bit register until (Register & Mask) == Value.

  @param[in] Controller  The controller instance.
  @param[in] Offset      Register offset in BAR 0.
  @param[in] Mask        Bits to test.
  @param[in] Value       Expected value of the tested bits.
  @param[in] TimeoutUs   Timeout in microseconds.

  @retval EFI_SUCCESS  The bits reached the expected value.
  @retval EFI_TIMEOUT  The timeout expired first.

**/
EFI_STATUS
HdaWaitReg16 (
  IN HDA_CONTROLLER  *Controller,
  IN UINT32          Offset,
  IN UINT16          Mask,
  IN UINT16          Value,
  IN UINTN           TimeoutUs
  );

/**
  Poll a 32-bit register until (Register & Mask) == Value.

  @param[in] Controller  The controller instance.
  @param[in] Offset      Register offset in BAR 0.
  @param[in] Mask        Bits to test.
  @param[in] Value       Expected value of the tested bits.
  @param[in] TimeoutUs   Timeout in microseconds.

  @retval EFI_SUCCESS  The bits reached the expected value.
  @retval EFI_TIMEOUT  The timeout expired first.

**/
EFI_STATUS
HdaWaitReg32 (
  IN HDA_CONTROLLER  *Controller,
  IN UINT32          Offset,
  IN UINT32          Mask,
  IN UINT32          Value,
  IN UINTN           TimeoutUs
  );

/**
  Reset the controller, discover attached codecs, and start the command
  rings.

  @param[in]  Controller  The controller instance.
  @param[out] CodecMask   Bit N is set if a codec answered at address N.

  @retval EFI_SUCCESS       The controller is ready to accept verbs.
  @retval EFI_NOT_FOUND     No codecs are attached.
  @retval EFI_DEVICE_ERROR  The controller did not leave reset.
  @retval Others            Allocating or mapping the rings failed.

**/
EFI_STATUS
HdaControllerInit (
  IN  HDA_CONTROLLER  *Controller,
  OUT UINT16          *CodecMask
  );

/**
  Stop all DMA engines and put the controller into reset. Frees and unmaps
  the command rings.

  @param[in] Controller  The controller instance.

**/
VOID
HdaControllerShutdown (
  IN HDA_CONTROLLER  *Controller
  );

/**
  Stop all DMA without touching memory allocations. Safe to call from an
  ExitBootServices() notification.

  @param[in] Controller  The controller instance.

**/
VOID
HdaControllerHalt (
  IN HDA_CONTROLLER  *Controller
  );

/**
  Send one verb to a codec and wait for its response.

  @param[in]  Controller  The controller instance.
  @param[in]  Verb        The fully encoded verb (HDA_VERB_12 or HDA_VERB_4).
  @param[out] Response    The response of the codec. Optional.

  @retval EFI_SUCCESS  The codec responded.
  @retval EFI_TIMEOUT  No response arrived in time.

**/
EFI_STATUS
HdaSendVerb (
  IN  HDA_CONTROLLER  *Controller,
  IN  UINT32          Verb,
  OUT UINT32          *Response OPTIONAL
  );

//
// HdaCodec.c
//

/**
  Find every analog output pin on the attached codecs that can reach a DAC
  and add it to Controller->Outputs in order of preference. Child handles
  are not created here.

  @param[in] Controller  The controller instance.
  @param[in] CodecMask   Codecs found by HdaControllerInit().

  @retval EFI_SUCCESS           At least one output was found.
  @retval EFI_NOT_FOUND         No usable output exists.
  @retval EFI_OUT_OF_RESOURCES  An output could not be allocated.

**/
EFI_STATUS
HdaCodecEnumerateOutputs (
  IN HDA_CONTROLLER  *Controller,
  IN UINT16          CodecMask
  );

/**
  Route the codec to an output. Disable every other output pin, select,
  power and unmute the path of the output, bind its DAC to the stream, and
  apply its volume. Nothing is done if the output is already routed.

  @param[in] Output  The output to route.

**/
VOID
HdaCodecRouteOutput (
  IN HDA_OUTPUT  *Output
  );

/**
  Apply Mode.Volume and Mode.Muted to the hardware amplifiers or to the
  software gain. The amplifiers are only programmed while the output is
  routed.

  @param[in] Output  The output.

  @retval EFI_SUCCESS       The volume was applied.
  @retval EFI_DEVICE_ERROR  A codec did not respond.

**/
EFI_STATUS
HdaCodecApplyVolume (
  IN HDA_OUTPUT  *Output
  );

/**
  Refresh Mode.Presence from the pin's presence detect.

  @param[in] Output  The output.

**/
VOID
HdaCodecUpdatePresence (
  IN HDA_OUTPUT  *Output
  );

//
// HdaQuirks.c
//

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
  );

//
// HdaStream.c
//

/**
  Locate the output stream descriptor and allocate the DMA ring and BDL.

  @param[in] Controller  The controller instance.

  @retval EFI_SUCCESS      The stream is ready.
  @retval EFI_UNSUPPORTED  The controller has no output-capable stream.
  @retval Others           Allocating or mapping the buffer failed.

**/
EFI_STATUS
HdaStreamInit (
  IN HDA_CONTROLLER  *Controller
  );

/**
  Free the DMA ring and BDL. The stream must be stopped.

  @param[in] Controller  The controller instance.

**/
VOID
HdaStreamFree (
  IN HDA_CONTROLLER  *Controller
  );

/**
  Start playing a buffer through an output. The output must already be
  routed. Called at TPL_NOTIFY.

  @param[in] Output      The output.
  @param[in] Buffer      PCM samples in the output's current mode format.
  @param[in] BufferSize  Size of Buffer in bytes.
  @param[in] Token       Optional completion token.

  @retval EFI_SUCCESS       The stream is running.
  @retval EFI_DEVICE_ERROR  The stream descriptor could not be reset.

**/
EFI_STATUS
HdaStreamStart (
  IN HDA_OUTPUT                *Output,
  IN CONST VOID                *Buffer,
  IN UINTN                     BufferSize,
  IN EDKII_AUDIO_OUTPUT_TOKEN  *Token OPTIONAL
  );

/**
  Stop the stream and complete the pending token. Called at TPL_NOTIFY.
  Does nothing if nothing is playing.

  @param[in] Controller  The controller instance.
  @param[in] Status      Completion status to report.

**/
VOID
HdaStreamFinish (
  IN HDA_CONTROLLER  *Controller,
  IN EFI_STATUS      Status
  );

/**
  Read how far the hardware has played and refill the ring. Called at
  TPL_NOTIFY.

  @param[in] Controller  The controller instance.

**/
VOID
HdaStreamPump (
  IN HDA_CONTROLLER  *Controller
  );

/**
  Refill timer notification.

  @param[in] Event    The timer event.
  @param[in] Context  The controller instance.

**/
VOID
EFIAPI
HdaStreamPumpNotify (
  IN EFI_EVENT  Event,
  IN VOID       *Context
  );

//
// SimpleAudioOutput.c
//

/**
  Initialize the protocol instance and mode of an output.

  @param[in] Output  The output.

**/
VOID
HdaAudioInitProtocol (
  IN HDA_OUTPUT  *Output
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
HdaAudioComponentNameGetDriverName (
  IN  EFI_COMPONENT_NAME_PROTOCOL  *This,
  IN  CHAR8                        *Language,
  OUT CHAR16                       **DriverName
  );

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
  );
