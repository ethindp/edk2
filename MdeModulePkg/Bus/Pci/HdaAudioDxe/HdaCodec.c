/** @file
  HD Audio codec support. Finds the analog outputs, routes the codec to an
  output, and controls volume and jack presence detection.

  Copyright (c) 2026, Ethin Probst. All rights reserved.<BR>
  SPDX-License-Identifier: BSD-2-Clause-Patent

**/

#include "HdaAudioDxe.h"

//
// Pin priorities. Higher is preferred.
//
#define HDA_PIN_PRIORITY_NONE           0
#define HDA_PIN_PRIORITY_OTHER          1
#define HDA_PIN_PRIORITY_LINE_OUT       2
#define HDA_PIN_PRIORITY_SPEAKER_JACK   3
#define HDA_PIN_PRIORITY_HP_OUT         4
#define HDA_PIN_PRIORITY_SPEAKER_FIXED  5

///
/// Audio function group used by the path search.
///
typedef struct {
  HDA_CONTROLLER    *Controller;
  UINT8             Codec;
  UINT8             Afg;
  UINT32            InAmpCaps;
  UINT32            OutAmpCaps;
  UINT32            PcmSizeRates;
  UINT32            StreamFormats;
} HDA_AFG;

/**
  Send a 12-bit verb.

  @param[in]  Controller  The controller instance.
  @param[in]  Codec       Codec address.
  @param[in]  Nid         Node ID.
  @param[in]  Verb        12-bit verb.
  @param[in]  Payload     8-bit payload.
  @param[out] Response    Optional response.

  @return The status of HdaSendVerb().

**/
STATIC
EFI_STATUS
HdaVerb12 (
  IN  HDA_CONTROLLER  *Controller,
  IN  UINT8           Codec,
  IN  UINT8           Nid,
  IN  UINT16          Verb,
  IN  UINT8           Payload,
  OUT UINT32          *Response OPTIONAL
  )
{
  return HdaSendVerb (Controller, HDA_VERB_12 (Codec, Nid, Verb, Payload), Response);
}

/**
  Send a 4-bit verb.

  @param[in] Controller  The controller instance.
  @param[in] Codec       Codec address.
  @param[in] Nid         Node ID.
  @param[in] Verb        4-bit verb.
  @param[in] Payload     16-bit payload.

  @return The status of HdaSendVerb().

**/
STATIC
EFI_STATUS
HdaVerb4 (
  IN HDA_CONTROLLER  *Controller,
  IN UINT8           Codec,
  IN UINT8           Nid,
  IN UINT8           Verb,
  IN UINT16          Payload
  )
{
  return HdaSendVerb (Controller, HDA_VERB_4 (Codec, Nid, Verb, Payload), NULL);
}

/**
  Read a node parameter.

  @param[in] Controller  The controller instance.
  @param[in] Codec       Codec address.
  @param[in] Nid         Node ID.
  @param[in] Parameter   HDA_PARAM_* value.

  @return The parameter value, or 0 if the codec did not respond.

**/
STATIC
UINT32
HdaGetParameter (
  IN HDA_CONTROLLER  *Controller,
  IN UINT8           Codec,
  IN UINT8           Nid,
  IN UINT8           Parameter
  )
{
  UINT32  Response;

  if (EFI_ERROR (HdaVerb12 (Controller, Codec, Nid, HDA_VERB_GET_PARAMETER, Parameter, &Response))) {
    return 0;
  }

  return Response;
}

/**
  Read a widget's connection list, expanding ranges.

  @param[in]  Controller  The controller instance.
  @param[in]  Codec       Codec address.
  @param[in]  Nid         Widget node ID.
  @param[out] List        Receives up to HDA_MAX_CONNECTIONS node IDs.

  @return The number of entries stored in List.

**/
STATIC
UINT8
HdaGetConnections (
  IN  HDA_CONTROLLER  *Controller,
  IN  UINT8           Codec,
  IN  UINT8           Nid,
  OUT UINT8           *List
  )
{
  UINT32   LengthParam;
  UINT32   Length;
  BOOLEAN  LongForm;
  UINT32   PerResponse;
  UINT32   EntryBits;
  UINT32   RangeFlag;
  UINT32   EntryMask;
  UINT32   Response;
  UINT32   Index;
  UINT32   Entry;
  UINT32   Previous;
  UINT32   Node;
  UINT8    Count;

  LengthParam = HdaGetParameter (Controller, Codec, Nid, HDA_PARAM_CONNECTION_LIST_LENGTH);
  Length      = HDA_CONN_LIST_LENGTH (LengthParam);
  LongForm    = (BOOLEAN)((LengthParam & HDA_CONN_LIST_LONG_FORM) != 0);
  PerResponse = LongForm ? 2 : 4;
  EntryBits   = LongForm ? 16 : 8;
  RangeFlag   = LongForm ? BIT15 : BIT7;
  EntryMask   = RangeFlag - 1;
  Response    = 0;
  Previous    = 0;
  Count       = 0;

  for (Index = 0; Index < Length && Count < HDA_MAX_CONNECTIONS; Index++) {
    if ((Index % PerResponse) == 0) {
      if (EFI_ERROR (HdaVerb12 (Controller, Codec, Nid, HDA_VERB_GET_CONNECTION_LIST_ENTRY, (UINT8)Index, &Response))) {
        break;
      }
    }

    Entry = (Response >> ((Index % PerResponse) * EntryBits)) & ((1U << EntryBits) - 1);
    if (((Entry & RangeFlag) != 0) && (Previous != 0)) {
      for (Node = Previous + 1; Node <= (Entry & EntryMask) && Count < HDA_MAX_CONNECTIONS; Node++) {
        List[Count++] = (UINT8)Node;
      }
    } else if ((Entry & EntryMask) <= 0x7F) {
      List[Count++] = (UINT8)(Entry & EntryMask);
    }

    Previous = Entry & EntryMask;
  }

  return Count;
}

/**
  Fill in the capabilities of one path node.

  @param[in]  Afg   The function group.
  @param[in]  Nid   Widget node ID.
  @param[out] Node  The node to fill in.

**/
STATIC
VOID
HdaReadNode (
  IN  HDA_AFG        *Afg,
  IN  UINT8          Nid,
  OUT HDA_PATH_NODE  *Node
  )
{
  Node->Nid  = Nid;
  Node->Caps = HdaGetParameter (Afg->Controller, Afg->Codec, Nid, HDA_PARAM_AUDIO_WIDGET_CAPS);
  Node->Type = (UINT8)HDA_WIDGET_TYPE (Node->Caps);

  //
  // Widgets without the amplifier override bit use the function group's
  // amplifier capabilities.
  //
  if ((Node->Caps & HDA_WIDGET_CAP_AMP_OVERRIDE) != 0) {
    Node->InAmpCaps  = HdaGetParameter (Afg->Controller, Afg->Codec, Nid, HDA_PARAM_INPUT_AMP_CAPABILITIES);
    Node->OutAmpCaps = HdaGetParameter (Afg->Controller, Afg->Codec, Nid, HDA_PARAM_OUTPUT_AMP_CAPABILITIES);
  } else {
    Node->InAmpCaps  = Afg->InAmpCaps;
    Node->OutAmpCaps = Afg->OutAmpCaps;
  }

  if ((Node->Caps & HDA_WIDGET_CAP_IN_AMP) == 0) {
    Node->InAmpCaps = 0;
  }

  if ((Node->Caps & HDA_WIDGET_CAP_OUT_AMP) == 0) {
    Node->OutAmpCaps = 0;
  }
}

/**
  Check whether a converter can play the stream format, 48 kHz 16-bit PCM.

  @param[in] Afg   The function group.
  @param[in] Node  The audio output converter.

  @retval TRUE   The converter supports the format.
  @retval FALSE  It does not.

**/
STATIC
BOOLEAN
HdaDacSupportsFormat (
  IN HDA_AFG        *Afg,
  IN HDA_PATH_NODE  *Node
  )
{
  UINT32  SizeRates;
  UINT32  Formats;

  if ((Afg->Controller->Quirks & HDA_QUIRK_48K_UNADVERTISED) != 0) {
    return TRUE;
  }

  //
  // Converters without the format override bit use the function group's
  // formats.
  //
  if ((Node->Caps & HDA_WIDGET_CAP_FMT_OVERRIDE) != 0) {
    SizeRates = HdaGetParameter (Afg->Controller, Afg->Codec, Node->Nid, HDA_PARAM_SUPPORTED_PCM_SIZE_RATE);
    Formats   = HdaGetParameter (Afg->Controller, Afg->Codec, Node->Nid, HDA_PARAM_SUPPORTED_STREAM_FORMAT);
  } else {
    SizeRates = Afg->PcmSizeRates;
    Formats   = Afg->StreamFormats;
  }

  return (BOOLEAN)(((SizeRates & HDA_PCM_RATE_48K) != 0) &&
                   ((SizeRates & HDA_PCM_SIZE_16) != 0) &&
                   ((Formats & HDA_STREAM_FORMAT_PCM) != 0));
}

/**
  Depth-limited search from a widget back to an analog DAC.

  @param[in]      Afg       The function group.
  @param[in]      Nid       Widget at this depth.
  @param[in]      Depth     Position of Nid in the path.
  @param[in]      MaxDepth  Maximum number of nodes in the path.
  @param[in, out] Path      The path being built.

  @retval TRUE   Path now describes a complete route.
  @retval FALSE  No route within MaxDepth.

**/
STATIC
BOOLEAN
HdaSearchPath (
  IN     HDA_AFG          *Afg,
  IN     UINT8            Nid,
  IN     UINT8            Depth,
  IN     UINT8            MaxDepth,
  IN OUT HDA_OUTPUT_PATH  *Path
  )
{
  HDA_PATH_NODE  *Node;
  UINT8          Connections[HDA_MAX_CONNECTIONS];
  UINT8          Index;

  Node = &Path->Nodes[Depth];
  HdaReadNode (Afg, Nid, Node);

  if (Depth > 0) {
    if (Node->Type == HDA_WIDGET_TYPE_AUDIO_OUTPUT) {
      if (((Node->Caps & HDA_WIDGET_CAP_DIGITAL) != 0) || !HdaDacSupportsFormat (Afg, Node)) {
        return FALSE;
      }

      Path->Count = Depth + 1;
      return TRUE;
    }

    if ((Node->Type != HDA_WIDGET_TYPE_AUDIO_MIXER) &&
        (Node->Type != HDA_WIDGET_TYPE_AUDIO_SELECTOR))
    {
      return FALSE;
    }
  }

  if (((Depth + 1) >= MaxDepth) || ((Node->Caps & HDA_WIDGET_CAP_CONN_LIST) == 0)) {
    return FALSE;
  }

  Node->NumConnections = HdaGetConnections (Afg->Controller, Afg->Codec, Nid, Connections);
  for (Index = 0; Index < Node->NumConnections; Index++) {
    if (Connections[Index] == 0) {
      continue;
    }

    //
    // The recursive call overwrites the deeper nodes. Set the connection of
    // this node again before each call.
    //
    Node->ConnIndex = Index;
    if (HdaSearchPath (Afg, Connections[Index], Depth + 1, MaxDepth, Path)) {
      return TRUE;
    }
  }

  return FALSE;
}

/**
  Find the shortest path from a pin to a DAC.

  @param[in]  Afg     The function group.
  @param[in]  PinNid  The pin complex.
  @param[out] Path    Receives the path.

  @retval TRUE   A path was found.
  @retval FALSE  The pin cannot reach a DAC.

**/
STATIC
BOOLEAN
HdaFindPath (
  IN  HDA_AFG          *Afg,
  IN  UINT8            PinNid,
  OUT HDA_OUTPUT_PATH  *Path
  )
{
  UINT8  MaxDepth;

  for (MaxDepth = 2; MaxDepth <= HDA_MAX_PATH_NODES; MaxDepth++) {
    ZeroMem (Path, sizeof (*Path));
    if (HdaSearchPath (Afg, PinNid, 0, MaxDepth, Path)) {
      return TRUE;
    }
  }

  return FALSE;
}

/**
  Rank an output pin by its configuration default.

  @param[in] PinConfig  Configuration Default of the pin.

  @return HDA_PIN_PRIORITY_* value; HDA_PIN_PRIORITY_NONE means unusable.

**/
STATIC
UINT32
HdaPinPriority (
  IN UINT32  PinConfig
  )
{
  UINT32  Connectivity;

  Connectivity = HDA_CONFIG_DEFAULT_CONNECTIVITY (PinConfig);
  if (Connectivity == HDA_CONFIG_CONNECTIVITY_NONE) {
    return HDA_PIN_PRIORITY_NONE;
  }

  switch (HDA_CONFIG_DEFAULT_DEVICE (PinConfig)) {
    case HDA_CONFIG_DEVICE_SPEAKER:
      return (Connectivity == HDA_CONFIG_CONNECTIVITY_JACK) ?
             HDA_PIN_PRIORITY_SPEAKER_JACK : HDA_PIN_PRIORITY_SPEAKER_FIXED;
    case HDA_CONFIG_DEVICE_HP_OUT:
      return HDA_PIN_PRIORITY_HP_OUT;
    case HDA_CONFIG_DEVICE_LINE_OUT:
      return HDA_PIN_PRIORITY_LINE_OUT;
    case HDA_CONFIG_DEVICE_SPDIF_OUT:
    case HDA_CONFIG_DEVICE_DIGITAL_OTHER_OUT:
      //
      // Digital outputs are not supported.
      //
      return HDA_PIN_PRIORITY_NONE;
    case HDA_CONFIG_DEVICE_CD:
    case HDA_CONFIG_DEVICE_MODEM_LINE:
    case HDA_CONFIG_DEVICE_MODEM_HANDSET:
    case HDA_CONFIG_DEVICE_LINE_IN:
    case HDA_CONFIG_DEVICE_AUX:
    case HDA_CONFIG_DEVICE_MIC_IN:
    case HDA_CONFIG_DEVICE_TELEPHONY:
    case HDA_CONFIG_DEVICE_SPDIF_IN:
    case HDA_CONFIG_DEVICE_DIGITAL_OTHER_IN:
      //
      // A retaskable jack may be output capable, but the board vendor
      // assigned it to an input device.
      //
      return HDA_PIN_PRIORITY_NONE;
    default:
      return HDA_PIN_PRIORITY_OTHER;
  }
}

/**
  Check whether a widget is an analog output-capable pin and read its pin
  capabilities and configuration default.

  @param[in]  Controller  The controller instance.
  @param[in]  Codec       Codec address.
  @param[in]  Nid         Widget node ID.
  @param[out] PinCaps     Pin capabilities.
  @param[out] PinConfig   Configuration default.

  @retval TRUE   The widget is a usable output pin.
  @retval FALSE  It is not.

**/
STATIC
BOOLEAN
HdaIsOutputPin (
  IN  HDA_CONTROLLER  *Controller,
  IN  UINT8           Codec,
  IN  UINT8           Nid,
  OUT UINT32          *PinCaps,
  OUT UINT32          *PinConfig
  )
{
  UINT32  Caps;

  Caps = HdaGetParameter (Controller, Codec, Nid, HDA_PARAM_AUDIO_WIDGET_CAPS);
  if ((HDA_WIDGET_TYPE (Caps) != HDA_WIDGET_TYPE_PIN_COMPLEX) ||
      ((Caps & HDA_WIDGET_CAP_DIGITAL) != 0))
  {
    return FALSE;
  }

  *PinCaps = HdaGetParameter (Controller, Codec, Nid, HDA_PARAM_PIN_CAPABILITIES);
  if ((*PinCaps & HDA_PIN_CAP_OUTPUT) == 0) {
    return FALSE;
  }

  if (EFI_ERROR (HdaVerb12 (Controller, Codec, Nid, HDA_VERB_GET_CONFIGURATION_DEFAULT, 0, PinConfig))) {
    return FALSE;
  }

  return (BOOLEAN)(HdaPinPriority (*PinConfig) != HDA_PIN_PRIORITY_NONE);
}

/**
  Put a node into power state D0 and wait until it gets there.

  @param[in] Controller  The controller instance.
  @param[in] Codec       Codec address.
  @param[in] Nid         Node ID.

**/
STATIC
VOID
HdaPowerUp (
  IN HDA_CONTROLLER  *Controller,
  IN UINT8           Codec,
  IN UINT8           Nid
  )
{
  UINT32  Response;
  UINTN   Elapsed;

  HdaVerb12 (Controller, Codec, Nid, HDA_VERB_SET_POWER_STATE, HDA_POWER_STATE_D0, NULL);

  //
  // Bits 7:4 report the actual state.
  //
  for (Elapsed = 0; Elapsed <= HDA_POWER_TIMEOUT_US; Elapsed += HDA_POLL_US * 10) {
    if (EFI_ERROR (HdaVerb12 (Controller, Codec, Nid, HDA_VERB_GET_POWER_STATE, 0, &Response)) ||
        (((Response >> 4) & 0xF) == HDA_POWER_STATE_D0))
    {
      return;
    }

    gBS->Stall (HDA_POLL_US * 10);
  }

  DEBUG ((DEBUG_WARN, "%a: codec %u node %u did not reach D0\n", __func__, Codec, Nid));
}

/**
  Get the gain index for 0 dB, or the top of the range if 0 dB is out of
  range. Volume 100 uses this gain, so the output is never amplified.

  @param[in] AmpCaps  Amplifier capabilities.

  @return The maximum gain index the driver will use.

**/
STATIC
UINT8
HdaAmpUnityGain (
  IN UINT32  AmpCaps
  )
{
  UINT32  Offset;

  Offset = HDA_AMP_CAP_OFFSET (AmpCaps);
  if ((Offset == 0) || (Offset > HDA_AMP_CAP_NUM_STEPS (AmpCaps))) {
    return (UINT8)HDA_AMP_CAP_NUM_STEPS (AmpCaps);
  }

  return (UINT8)Offset;
}

/**
  Program one widget's output amplifier, both channels.

  @param[in] Controller  The controller instance.
  @param[in] Codec       Codec address.
  @param[in] Node        The widget.
  @param[in] Gain        Gain index.
  @param[in] Mute        TRUE to mute.

  @return The status of HdaSendVerb().

**/
STATIC
EFI_STATUS
HdaSetOutputAmp (
  IN HDA_CONTROLLER  *Controller,
  IN UINT8           Codec,
  IN HDA_PATH_NODE   *Node,
  IN UINT8           Gain,
  IN BOOLEAN         Mute
  )
{
  UINT16  Payload;

  Payload = HDA_AMP_SET_OUTPUT | HDA_AMP_SET_LEFT | HDA_AMP_SET_RIGHT |
            (Gain & HDA_AMP_SET_GAIN_MASK);
  if (Mute && ((Node->OutAmpCaps & HDA_AMP_CAP_MUTE) != 0)) {
    Payload |= HDA_AMP_SET_MUTE;
  }

  return HdaVerb4 (Controller, Codec, Node->Nid, HDA_VERB_SET_AMP_GAIN_MUTE, Payload);
}

/**
  Program one input amplifier of a widget, both channels.

  @param[in] Controller  The controller instance.
  @param[in] Codec       Codec address.
  @param[in] Node        The widget.
  @param[in] Index       Connection index of the input.
  @param[in] Mute        TRUE to mute.

**/
STATIC
VOID
HdaSetInputAmp (
  IN HDA_CONTROLLER  *Controller,
  IN UINT8           Codec,
  IN HDA_PATH_NODE   *Node,
  IN UINT8           Index,
  IN BOOLEAN         Mute
  )
{
  UINT16  Payload;

  Payload = HDA_AMP_SET_INPUT | HDA_AMP_SET_LEFT | HDA_AMP_SET_RIGHT |
            (UINT16)((Index & 0xF) << HDA_AMP_SET_INDEX_SHIFT) |
            (HdaAmpUnityGain (Node->InAmpCaps) & HDA_AMP_SET_GAIN_MASK);
  if (Mute && ((Node->InAmpCaps & HDA_AMP_CAP_MUTE) != 0)) {
    Payload |= HDA_AMP_SET_MUTE;
  }

  HdaVerb4 (Controller, Codec, Node->Nid, HDA_VERB_SET_AMP_GAIN_MUTE, Payload);
}

/**
  Choose how the volume and mute of an output are applied. The output
  amplifier closest to the DAC that has a gain range is used. Without one,
  volume is applied in software.

  @param[in, out] Output  The output.

**/
STATIC
VOID
HdaChooseGainStage (
  IN OUT HDA_OUTPUT  *Output
  )
{
  UINT8  Index;

  Output->Path.GainNode = HDA_NO_GAIN_NODE;
  Output->HwVolume      = FALSE;
  Output->HwMute        = FALSE;
  if ((Output->Controller->Quirks & HDA_QUIRK_CODEC_AMP_IGNORED) != 0) {
    return;
  }

  for (Index = 0; Index < Output->Path.Count; Index++) {
    if (HDA_AMP_CAP_NUM_STEPS (Output->Path.Nodes[Index].OutAmpCaps) != 0) {
      Output->Path.GainNode = Index;
    }
  }

  Output->HwVolume = (BOOLEAN)(Output->Path.GainNode != HDA_NO_GAIN_NODE);
  Output->HwMute   = (BOOLEAN)(Output->HwVolume &&
                               ((Output->Path.Nodes[Output->Path.GainNode].OutAmpCaps & HDA_AMP_CAP_MUTE) != 0));
}

/**
  Build a user-facing name such as "Front Headphones" or "Internal
  Speaker" from the pin's configuration default.

  @param[in, out] Output  The output. Its Name is set.

**/
STATIC
VOID
HdaNameOutput (
  IN OUT HDA_OUTPUT  *Output
  )
{
  UINT32        Config;
  CONST CHAR16  *Location;
  CONST CHAR16  *Kind;

  Config = Output->Path.PinConfig;

  switch (Output->Mode.OutputType) {
    case EDKII_AUDIO_OUTPUT_TYPE_SPEAKER:
      Kind = L"Speaker";
      break;
    case EDKII_AUDIO_OUTPUT_TYPE_HEADPHONES:
      Kind = L"Headphones";
      break;
    case EDKII_AUDIO_OUTPUT_TYPE_LINE_OUT:
      Kind = L"Line Out";
      break;
    default:
      Kind = L"Audio Output";
      break;
  }

  Location = NULL;
  if ((HDA_CONFIG_DEFAULT_GROSS (Config) == HDA_CONFIG_GROSS_INTERNAL) ||
      (HDA_CONFIG_DEFAULT_CONNECTIVITY (Config) == HDA_CONFIG_CONNECTIVITY_FIXED))
  {
    Location = L"Internal";
  } else {
    switch (HDA_CONFIG_DEFAULT_GEOMETRIC (Config)) {
      case HDA_CONFIG_GEOMETRIC_REAR:
        Location = L"Rear";
        break;
      case HDA_CONFIG_GEOMETRIC_FRONT:
        Location = L"Front";
        break;
      case HDA_CONFIG_GEOMETRIC_LEFT:
        Location = L"Left";
        break;
      case HDA_CONFIG_GEOMETRIC_RIGHT:
        Location = L"Right";
        break;
      case HDA_CONFIG_GEOMETRIC_TOP:
        Location = L"Top";
        break;
      case HDA_CONFIG_GEOMETRIC_BOTTOM:
        Location = L"Bottom";
        break;
      default:
        break;
    }
  }

  if (Location != NULL) {
    UnicodeSPrint (Output->Name, sizeof (Output->Name), L"%s %s", Location, Kind);
  } else {
    UnicodeSPrint (Output->Name, sizeof (Output->Name), L"%s", Kind);
  }
}

/**
  Create an output for a pin and insert it into the controller's list in
  order of preference.

  @param[in] Afg        The function group.
  @param[in] Path       The pin's path to a DAC.
  @param[in] PinCaps    Pin capabilities.
  @param[in] PinConfig  Configuration default.

  @retval EFI_SUCCESS           The output was added (or the list is full).
  @retval EFI_OUT_OF_RESOURCES  It could not be allocated.

**/
STATIC
EFI_STATUS
HdaAddOutput (
  IN HDA_AFG          *Afg,
  IN HDA_OUTPUT_PATH  *Path,
  IN UINT32           PinCaps,
  IN UINT32           PinConfig
  )
{
  HDA_CONTROLLER  *Controller;
  HDA_OUTPUT      *Output;
  UINT32          Connectivity;
  UINT8           Index;

  Controller = Afg->Controller;
  if (Controller->OutputCount >= HDA_MAX_OUTPUTS) {
    DEBUG ((DEBUG_WARN, "%a: too many outputs, ignoring pin %u\n", __func__, Path->Nodes[0].Nid));
    return EFI_SUCCESS;
  }

  Output = AllocateZeroPool (sizeof (*Output));
  if (Output == NULL) {
    return EFI_OUT_OF_RESOURCES;
  }

  Output->Signature     = HDA_OUTPUT_SIGNATURE;
  Output->Controller    = Controller;
  Output->Codec         = Afg->Codec;
  Output->Afg           = Afg->Afg;
  Output->AfgInAmpCaps  = Afg->InAmpCaps;
  Output->AfgOutAmpCaps = Afg->OutAmpCaps;
  Output->SoftwareGain  = HDA_SOFTWARE_GAIN_UNITY;
  CopyMem (&Output->Path, Path, sizeof (*Path));
  Output->Path.PinCaps   = PinCaps;
  Output->Path.PinConfig = PinConfig;

  //
  // Higher priority first. Within a priority, lower association and sequence
  // numbers first, as assigned by the board vendor.
  //
  Output->Score = (HdaPinPriority (PinConfig) << 8) |
                  (0xFF - (UINT32)((HDA_CONFIG_DEFAULT_ASSOCIATION (PinConfig) << 4) |
                                   HDA_CONFIG_DEFAULT_SEQUENCE (PinConfig)));

  switch (HDA_CONFIG_DEFAULT_DEVICE (PinConfig)) {
    case HDA_CONFIG_DEVICE_SPEAKER:
      Output->Mode.OutputType = EDKII_AUDIO_OUTPUT_TYPE_SPEAKER;
      break;
    case HDA_CONFIG_DEVICE_HP_OUT:
      Output->Mode.OutputType = EDKII_AUDIO_OUTPUT_TYPE_HEADPHONES;
      break;
    case HDA_CONFIG_DEVICE_LINE_OUT:
      Output->Mode.OutputType = EDKII_AUDIO_OUTPUT_TYPE_LINE_OUT;
      break;
    default:
      Output->Mode.OutputType = EDKII_AUDIO_OUTPUT_TYPE_UNKNOWN;
      break;
  }

  Connectivity             = HDA_CONFIG_DEFAULT_CONNECTIVITY (PinConfig);
  Output->CanSensePresence = (BOOLEAN)(((PinCaps & HDA_PIN_CAP_PRESENCE_DETECT) != 0) &&
                                       ((Connectivity == HDA_CONFIG_CONNECTIVITY_JACK) ||
                                        (Connectivity == HDA_CONFIG_CONNECTIVITY_BOTH)) &&
                                       ((HDA_CONFIG_DEFAULT_MISC (PinConfig) & HDA_CONFIG_MISC_NO_PRESENCE_DETECT) == 0) &&
                                       ((Controller->Quirks & HDA_QUIRK_PRESENCE_UNRELIABLE) == 0));

  HdaChooseGainStage (Output);
  HdaNameOutput (Output);

  for (Index = Controller->OutputCount; Index > 0; Index--) {
    if (Controller->Outputs[Index - 1]->Score >= Output->Score) {
      break;
    }

    Controller->Outputs[Index] = Controller->Outputs[Index - 1];
  }

  Controller->Outputs[Index] = Output;
  Controller->OutputCount++;

  DEBUG ((
    DEBUG_INFO,
    "%a: \"%s\": codec %u pin %u (config 0x%08x), DAC %u, %u nodes, %a volume%a\n",
    __func__,
    Output->Name,
    Output->Codec,
    Path->Nodes[0].Nid,
    PinConfig,
    Path->Nodes[Path->Count - 1].Nid,
    Path->Count,
    Output->HwVolume ? "hardware" : "software",
    Output->CanSensePresence ? ", presence detect" : ""
    ));
  return EFI_SUCCESS;
}

/**
  Append a number to outputs that share a name. The second "Rear Line Out"
  becomes "Rear Line Out 2".

  @param[in] Controller  The controller instance.

**/
STATIC
VOID
HdaNumberDuplicateNames (
  IN HDA_CONTROLLER  *Controller
  )
{
  UINT8  Index;
  UINT8  Earlier;
  UINTN  Number;
  UINTN  Length;

  for (Index = 1; Index < Controller->OutputCount; Index++) {
    Number = 1;
    for (Earlier = 0; Earlier < Index; Earlier++) {
      if (StrnCmp (
            Controller->Outputs[Earlier]->Name,
            Controller->Outputs[Index]->Name,
            StrLen (Controller->Outputs[Index]->Name)
            ) == 0)
      {
        Number++;
      }
    }

    if (Number > 1) {
      Length = StrLen (Controller->Outputs[Index]->Name);
      UnicodeSPrint (
        Controller->Outputs[Index]->Name + Length,
        sizeof (Controller->Outputs[Index]->Name) - Length * sizeof (CHAR16),
        L" %u",
        Number
        );
    }
  }
}

/**
  Locate the audio function group of a codec and read its amplifier
  defaults.

  @param[in]  Controller  The controller instance.
  @param[in]  Codec       Codec address.
  @param[out] Afg         The function group.

  @retval TRUE   The codec has an audio function group.
  @retval FALSE  It does not.

**/
STATIC
BOOLEAN
HdaFindAfg (
  IN  HDA_CONTROLLER  *Controller,
  IN  UINT8           Codec,
  OUT HDA_AFG         *Afg
  )
{
  UINT32  SubNodes;
  UINT32  Nid;
  UINT32  End;

  SubNodes = HdaGetParameter (Controller, Codec, 0, HDA_PARAM_SUBORDINATE_NODE_COUNT);
  End      = HDA_SUB_NODE_START (SubNodes) + HDA_SUB_NODE_COUNT (SubNodes);
  for (Nid = HDA_SUB_NODE_START (SubNodes); Nid < End && Nid <= 0x7F; Nid++) {
    if (HDA_FG_TYPE (HdaGetParameter (Controller, Codec, (UINT8)Nid, HDA_PARAM_FUNCTION_GROUP_TYPE)) == HDA_FG_TYPE_AUDIO) {
      Afg->Controller = Controller;
      Afg->Codec      = Codec;
      Afg->Afg        = (UINT8)Nid;
      HdaPowerUp (Controller, Codec, Afg->Afg);
      Afg->InAmpCaps     = HdaGetParameter (Controller, Codec, Afg->Afg, HDA_PARAM_INPUT_AMP_CAPABILITIES);
      Afg->OutAmpCaps    = HdaGetParameter (Controller, Codec, Afg->Afg, HDA_PARAM_OUTPUT_AMP_CAPABILITIES);
      Afg->PcmSizeRates  = HdaGetParameter (Controller, Codec, Afg->Afg, HDA_PARAM_SUPPORTED_PCM_SIZE_RATE);
      Afg->StreamFormats = HdaGetParameter (Controller, Codec, Afg->Afg, HDA_PARAM_SUPPORTED_STREAM_FORMAT);
      return TRUE;
    }
  }

  return FALSE;
}

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
  )
{
  EFI_STATUS       Status;
  UINT8            Codec;
  HDA_AFG          Afg;
  UINT32           SubNodes;
  UINT32           Nid;
  UINT32           End;
  UINT32           PinCaps;
  UINT32           PinConfig;
  HDA_OUTPUT_PATH  Path;

  for (Codec = 0; Codec < HDA_MAX_CODECS; Codec++) {
    if ((CodecMask & (1U << Codec)) == 0) {
      continue;
    }

    DEBUG ((
      DEBUG_INFO,
      "%a: codec %u vendor 0x%08x\n",
      __func__,
      Codec,
      HdaGetParameter (Controller, Codec, 0, HDA_PARAM_VENDOR_ID)
      ));

    if (!HdaFindAfg (Controller, Codec, &Afg)) {
      continue;
    }

    SubNodes = HdaGetParameter (Controller, Codec, Afg.Afg, HDA_PARAM_SUBORDINATE_NODE_COUNT);
    End      = HDA_SUB_NODE_START (SubNodes) + HDA_SUB_NODE_COUNT (SubNodes);
    for (Nid = HDA_SUB_NODE_START (SubNodes); Nid < End && Nid <= 0x7F; Nid++) {
      if (!HdaIsOutputPin (Controller, Codec, (UINT8)Nid, &PinCaps, &PinConfig) ||
          !HdaFindPath (&Afg, (UINT8)Nid, &Path))
      {
        continue;
      }

      Status = HdaAddOutput (&Afg, &Path, PinCaps, PinConfig);
      if (EFI_ERROR (Status)) {
        return Status;
      }
    }
  }

  if (Controller->OutputCount == 0) {
    DEBUG ((DEBUG_ERROR, "%a: no usable output pin\n", __func__));
    return EFI_NOT_FOUND;
  }

  HdaNumberDuplicateNames (Controller);
  return EFI_SUCCESS;
}

/**
  Turn off an output's pin driver and unbind its DAC from the stream.

  @param[in] Output  The output to silence.
  @param[in] Keep    The output being routed. Its DAC stays bound.

**/
STATIC
VOID
HdaDisableOutput (
  IN HDA_OUTPUT  *Output,
  IN HDA_OUTPUT  *Keep
  )
{
  HDA_CONTROLLER  *Controller;
  UINT8           Dac;
  UINT8           KeepDac;

  Controller = Output->Controller;
  Dac        = Output->Path.Nodes[Output->Path.Count - 1].Nid;
  KeepDac    = Keep->Path.Nodes[Keep->Path.Count - 1].Nid;

  HdaVerb12 (Controller, Output->Codec, Output->Path.Nodes[0].Nid, HDA_VERB_SET_PIN_WIDGET_CONTROL, 0, NULL);
  if ((Output->Codec != Keep->Codec) || (Dac != KeepDac)) {
    HdaVerb12 (Controller, Output->Codec, Dac, HDA_VERB_SET_CONVERTER_CONTROL, 0, NULL);
  }
}

/**
  Route the codec to an output. Disable every other output pin, select,
  power and unmute the path of the output, bind its DAC to the stream, and
  apply its volume. Nothing is done if the output is already routed.

  @param[in] Output  The output to route.

**/
VOID
HdaCodecRouteOutput (
  IN HDA_OUTPUT  *Output
  )
{
  HDA_CONTROLLER  *Controller;
  HDA_PATH_NODE   *Node;
  UINT8           Index;
  UINT8           Input;
  UINT8           PinControl;
  UINT8           Dac;

  Controller = Output->Controller;
  if (Controller->RoutedOutput == Output) {
    return;
  }

  for (Index = 0; Index < Controller->OutputCount; Index++) {
    if ((Controller->Outputs[Index] != NULL) && (Controller->Outputs[Index] != Output)) {
      HdaDisableOutput (Controller->Outputs[Index], Output);
    }
  }

  for (Index = 0; Index < Output->Path.Count; Index++) {
    Node = &Output->Path.Nodes[Index];

    if ((Node->Caps & HDA_WIDGET_CAP_POWER_CNTRL) != 0) {
      HdaPowerUp (Controller, Output->Codec, Node->Nid);
    }

    //
    // Select the next widget toward the DAC. A mixer sums all inputs, so
    // unmute the input on the path and mute the others.
    //
    if (Index + 1 < Output->Path.Count) {
      if (Node->Type == HDA_WIDGET_TYPE_AUDIO_MIXER) {
        if (Node->InAmpCaps != 0) {
          for (Input = 0; Input < Node->NumConnections; Input++) {
            HdaSetInputAmp (Controller, Output->Codec, Node, Input, (BOOLEAN)(Input != Node->ConnIndex));
          }
        }
      } else {
        if (Node->NumConnections > 1) {
          HdaVerb12 (Controller, Output->Codec, Node->Nid, HDA_VERB_SET_CONNECTION_SELECT, Node->ConnIndex, NULL);
        }

        //
        // Skip the input amplifier of a pin. It is only used for recording.
        //
        if ((Node->InAmpCaps != 0) && (Node->Type != HDA_WIDGET_TYPE_PIN_COMPLEX)) {
          HdaSetInputAmp (Controller, Output->Codec, Node, Node->ConnIndex, FALSE);
        }
      }
    }

    //
    // We only send amplifier verbs to widgets that have an output amplifier,
    // because their effect on other widgets is undefined (Section 7.3.4.6).
    // Amplifiers other than the one used for volume are set to unity gain.
    //
    if (Node->OutAmpCaps != 0) {
      HdaSetOutputAmp (Controller, Output->Codec, Node, HdaAmpUnityGain (Node->OutAmpCaps), FALSE);
    }
  }

  Node       = &Output->Path.Nodes[0];
  PinControl = HDA_PIN_CTL_OUT_ENABLE;
  if ((HDA_CONFIG_DEFAULT_DEVICE (Output->Path.PinConfig) == HDA_CONFIG_DEVICE_HP_OUT) &&
      ((Output->Path.PinCaps & HDA_PIN_CAP_HEADPHONE_DRIVE) != 0))
  {
    PinControl |= HDA_PIN_CTL_HP_ENABLE;
  }

  HdaVerb12 (Controller, Output->Codec, Node->Nid, HDA_VERB_SET_PIN_WIDGET_CONTROL, PinControl, NULL);
  if ((Output->Path.PinCaps & HDA_PIN_CAP_EAPD) != 0) {
    HdaVerb12 (Controller, Output->Codec, Node->Nid, HDA_VERB_SET_EAPD_BTL_ENABLE, HDA_EAPD_BTL_ENABLE_EAPD, NULL);
  }

  Dac = Output->Path.Nodes[Output->Path.Count - 1].Nid;
  HdaVerb4 (Controller, Output->Codec, Dac, HDA_VERB_SET_CONVERTER_FORMAT, HDA_HW_FORMAT);
  HdaVerb12 (Controller, Output->Codec, Dac, HDA_VERB_SET_CONVERTER_CONTROL, (UINT8)(HDA_STREAM_TAG << 4), NULL);

  Controller->RoutedOutput = Output;
  HdaCodecApplyVolume (Output);

  DEBUG ((DEBUG_INFO, "%a: routed to \"%s\"\n", __func__, Output->Name));
}

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
  )
{
  BOOLEAN        Silent;
  HDA_PATH_NODE  *Node;
  UINT32         Gain;
  UINT32         Volume;

  Volume = Output->Mode.Volume;
  Silent = (BOOLEAN)(Output->Mode.Muted || (Volume == 0));

  if (!Output->HwVolume) {
    //
    // Square the volume so that the steps sound more even.
    //
    Output->SoftwareGain = Silent ? 0 : (Volume * Volume * HDA_SOFTWARE_GAIN_UNITY) / (100 * 100);
    return EFI_SUCCESS;
  }

  Output->SoftwareGain = (Silent && !Output->HwMute) ? 0 : HDA_SOFTWARE_GAIN_UNITY;
  if (Output->Controller->RoutedOutput != Output) {
    return EFI_SUCCESS;
  }

  Node = &Output->Path.Nodes[Output->Path.GainNode];
  Gain = (Volume * HdaAmpUnityGain (Node->OutAmpCaps) + 50) / 100;
  if (EFI_ERROR (HdaSetOutputAmp (Output->Controller, Output->Codec, Node, (UINT8)Gain, Silent))) {
    return EFI_DEVICE_ERROR;
  }

  return EFI_SUCCESS;
}

/**
  Refresh Mode.Presence from the pin's presence detect.

  @param[in] Output  The output.

**/
VOID
HdaCodecUpdatePresence (
  IN HDA_OUTPUT  *Output
  )
{
  UINT32  Response;
  UINT8   Pin;

  Pin = Output->Path.Nodes[0].Nid;
  if (HDA_CONFIG_DEFAULT_CONNECTIVITY (Output->Path.PinConfig) == HDA_CONFIG_CONNECTIVITY_FIXED) {
    Output->Mode.Presence = EDKII_AUDIO_OUTPUT_PRESENCE_CONNECTED;
    return;
  }

  if (!Output->CanSensePresence) {
    Output->Mode.Presence = EDKII_AUDIO_OUTPUT_PRESENCE_UNKNOWN;
    return;
  }

  //
  // Some pins only sense presence when told to (Section 7.3.3.15).
  //
  if ((Output->Path.PinCaps & HDA_PIN_CAP_TRIGGER_REQUIRED) != 0) {
    HdaVerb12 (Output->Controller, Output->Codec, Pin, HDA_VERB_EXECUTE_PIN_SENSE, 0, NULL);
  }

  if (!EFI_ERROR (HdaVerb12 (Output->Controller, Output->Codec, Pin, HDA_VERB_GET_PIN_SENSE, 0, &Response))) {
    Output->Mode.Presence = ((Response & HDA_PIN_SENSE_PRESENCE) != 0) ?
                            EDKII_AUDIO_OUTPUT_PRESENCE_CONNECTED :
                            EDKII_AUDIO_OUTPUT_PRESENCE_DISCONNECTED;
  }
}
