/** USB audio descriptors

This file contains audio descriptors for the USB audio specification for basic audio devices.

Copyright (C) 2021 Ethin Probst.

*/

#include <Uefi.h>

//
// Class-Specific AC Interface Header Descriptor
//
#pragma pack(push)
#pragma pack(1)
typedef struct {
  UINT8 Length;
  UINT8 DescriptorType;
  UINT8 DescriptorSubtype;
  UINT16 Adc;
  UINT16 TotalLength;
  UINT8 InCollection;
  UINT8 InterfaceNr[1];
} EFI_USB_AUDIO_DESCRIPTOR_HEADER;

//
// Terminal descriptors
//
#pragma pack(1)
typedef struct {
  UINT8 Length;
  UINT8 DescriptorType;
  UINT8 DescriptorSubtype;
  UINT8 Id;
} EFI_USB_AUDIO_TERMINAL_DESCRIPTOR_HEADER;

#pragma pack(1)
typedef struct {
  EFI_USB_AUDIO_TERMINAL_DESCRIPTOR_HEADER Header;
  UINT16 TerminalType;
  UINT8 AssocTerminal;
  UINT8 Channels;
  UINT16 ChannelConfig;
  UINT8 ChannelNames;
  UINT8 Terminal;
} EFI_USB_AUDIO_INPUT_TERMINAL_DESCRIPTOR;

#pragma pack(1)
typedef struct {
  EFI_USB_AUDIO_TERMINAL_DESCRIPTOR_HEADER Header;
  UINT16 TerminalType;
  UINT8 AssocTerminal;
  UINT8 SourceId;
  UINT8 Terminal;
} EFI_USB_AUDIO_OUTPUT_TERMINAL_DESCRIPTOR;

typedef enum {
  EfiUsbAudioAcUndefined = 0x00,
  EfiUsbAudioAcHeader,
  EfiUsbAudioAcInputTerminal,
  EfiUsbAudioAcOutputTerminal,
  EfiUsbAudioAcMixerUnit,
  EfiUsbAudioAcSelectorUnit,
  EfiUsbAudioAcFeatureUnit,
  EfiUsbAudioAcProcessingUnit,
  EfiUsbAudioAcExtensionUnit,
  EfiUsbAudioAcMax = 0xFF
} EFI_USB_AUDIO_DESCRIPTOR_SUBTYPE;

typedef enum {
  EfiUsbAudioUsbUndefined = 0x0100,
  EfiUsbAudioUsbStreaming,
  EfiUsbAudioUsbVendorSpecific,
  EfiUsbAudioInUndefined = 0x0200,
  EfiUsbAudioInMicrophone,
  EfiUsbAudioInDesktopMicrophone,
  EfiUsbAudioInPersonalMicrophone,
  EfiUsbAudioInOmniDirectionalMicrophone,
  EfiUsbAudioInMicrophoneArray,
  EfiUsbAudioInProcessingMicrophoneArray,
  EfiUsbAudioOutUndefined = 0x0300,
  EfiUsbAudioOutSpeaker,
  EfiUsbAudioOutHeadphones,
  EfiUsbAudioOutHeadMountedDisplayAudio,
  EfiUsbAudioOutDesktopSpeaker,
  EfiUsbAudioOutRoomSpeaker,
  EfiUsbAudioOutCommunicationSpeaker,
  EfiUsbAudioOutLfeSpeaker,
  EfiUsbAudioBiUndefined = 0x0400,
  EfiUsbAudioBiHandset,
  EfiUsbAudioBiHeadset,
  EfiUsbAudioBiSpeakerphoneNoEcho,
  EfiUsbAudioBiEchoSuppressingSpeakerphone,
  EfiUsbAudioBiEchoCancellingSpeakerphone,
  EfiUsbAudioTelUndefined = 0x0500,
  EfiUsbAudioTelPhoneLine,
  EfiUsbAudioTelTelephone,
  EfiUsbAudioTelDownLinePhone,
  EfiUsbAudioExtUndefined = 0x0600,
  EfiUsbAudioExtAnalogConnector,
  EfiUsbAudioExtDigitalAudioInterface,
  EfiUsbAudioExtLineConnector,
  EfiUsbAudioExtLegacyAudioConnector,
  EfiUsbAudioExtSpdifInterface,
  EfiUsbAudioExt1394DaStream,
  EfiUsbAudioExt1394DvStreamSoundtrack,
  EfiUsbAudioEmbUndefined = 0x0700,
  EfiUsbAudioEmbLevelCalibrationNoiseSource,
  EfiUsbAudioEmbEqualizationNoise,
  EfiUsbAudioEmbCdPlayer,
  EfiUsbAudioEmbDat,
  EfiUsbAudioEmbDcc,
  EfiUsbAudioEmbMiniDisk,
  EfiUsbAudioEmbAnalogTape,
  EfiUsbAudioEmbPhonograph,
  EfiUsbAudioEmbVcrAudio,
  EfiUsbAudioEmbVideoDisk,
  EfiUsbAudioEmbDvdAudio,
  EfiUsbAudioEmbTvTunerAudio,
  EfiUsbAudioEmbSatelliteReceiverAudio,
  EfiUsbAudioEmbCableTunerAudio,
  EfiUsbAudioEmbDssAudio,
  EfiUsbAudioEmbRadioReceiver,
  EfiUsbAudioEmbRadioTransmitter,
  EfiUsbAudioEmbMultiTrackRecorder,
  EfiUsbAudioEmbSynthesizer,
  EfiUsbAudioTermTypeMax = 0xFFFF
} EFI_USB_AUDIO_TERMINAL_TYPE;

#pragma pack(pop)

