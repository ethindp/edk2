/** USB audio descriptors

This file contains audio descriptors for the USB audio specification for basic audio devices.

Copyright (C) 2021 Ethin Probst.

*/

#include <Uefi.h>

//
// Class-Specific AC Interface Header Descriptor
//
typedef struct {
  UINT8 Length;
  UINT8 DescriptorType;
  UINT8 DescriptorSubtype;
  UINT16 Adc;
  UINT16 TotalLength;
  UINT8 InCollection;
  UINT8* InterfaceNr;
} EFI_USB_AUDIO_DESCRIPTOR_HEADER;

//
// Terminal descriptors
//
typedef struct {
  UINT8 Length;
  UINT8 DescriptorType;
  UINT8 DescriptorSubtype;
  UINT8 TerminalId;
  UINT16 TerminalType;
  UINT8 AssocTerminal;
  UINT8 Channels;
  UINT16 ChannelConfig;
  UINT8 ChannelNames;
  UINT8 Terminal;
} EFI_USB_AUDIO_ITD;

typedef struct {
  UINT8 length;
  UINT8 DescriptorType;
  UINT8 DescriptorSubtype;
  UINT8 TerminalId;
  UINT16 TerminalType;
  UINT8 AssocTerminal;
  UINT8 SourceId;
  UINT8 Terminal;
} EFI_USB_AUDIO_OTD;

