/**

Intel HDA audio register and structure definitions

Copyright (C) 2021 Ethin Probst
*/

#pragma once
#include <Uefi.h>

// For a list of all registers and any associated notes, see
// https://wiki.osdev.org/Intel_High_Definition_Audio
typedef enum {
  HdaGloblCaps = 0x00,
  HdaVerMin = 0x02,
  HdaVerMaj = 0x03,
  HdaOutPayCap = 0x04,
  HdaInPayCap = 0x06,
  HdaGloblCtl = 0x08,
  HdaWakeEn = 0x0C,
  HdaStateStatus = 0x0E,
  HdaGloblStatus = 0x10,
  HdaOutStrmPayCap = 0x18,
  HdaInStrmPayCap = 0x1A,
  HdaIntCtl = 0x20,
  HdaIntStatus = 0x24,
  HdaWallClock = 0x30,
  HdaStrmSync = 0x34,
  HdaCorbLo = 0x40,
  HdaCorbHi = 0x44,
  HdaCorbWritePtr = 0x48,
  HdaCorbReadPtr = 0x4A,
  HdaCorbCtl = 0x4C,
  HdaCorbStatus = 0x4D,
  HdaCorbSize = 0x4E,
  HdaRirbLo = 0x50,
  HdaRirbHi = 0x54,
  HdaRirbWritePtr = 0x58,
  HdaRespIntCnt = 0x5A,
  HdaRirbCtl = 0x5C,
  HdaRirbStatus = 0x5D,
  HdaRirbSize = 0x5E,
  HdaImmCmdOut = 0x60,
  HdaImmRespIn = 0x64,
  HdaImmCmdStatus = 0x68,
  HdaDplLo = 0x70,
  HdaDplHi = 0x74,
  HdaStreamControl = 0x00,
  HdaStreamStatus = 0x03,
  HdaStreamLinkPosInBuf = 0x04,
  HdaStreamCyclicBufferLength = 0x08,
  HdaStreamLastValidIndex = 0x0C,
  HdaStreamFifoSize = 0x10,
  HdaStreamFormat = 0x12,
  HdaStreamBdlLo = 0x18,
  HdaStreamBdlHi = 0x1C,
  HdaRegMax = MAX_UINT64
} HDA_REGISTER;

typedef enum {
  HdaVerbGetParameter = 0xF00,
  HdaVerbGetSelectedInput = 0xF01,
  HdaVerbSetSelectedInput = 0x701,
  HdaVerbGetStreamChannel = 0xF06,
  HdaVerbSetStreamChannel = 0x706,
  HdaVerbGetPinWidgetControl = 0xF07,
  HdaVerbSetPinWidgetControl = 0x707,
  HdaVerbGetVolumeControl = 0xF0F,
  HdaVerbSetVolumeControl = 0x70F,
  HdaVerbGetConfigurationDefault = 0xF1C,
  HdaVerbGetConverterChannelCount = 0xF2D,
  HdaVerbSetConverterChannelCount = 0x72D,
  HdaVerbFunctionReset = 0x7FF,
  HdaVerbSetAmplifierGain = 0x03,
  HdaVerbSetStreamFormat = 0x02,
  HdaVerbSetPowerState = 0x705,
  HdaVerbMax = MAX_UINT16
} HDA_CODEC_VERB;

typedef enum {
  HdaParamVendorDevId,
  HdaParamRevId = 0x02,
  HdaParamNodeCount = 0x04,
  HdaParamFunctionGroupType,
  HdaParamAudioGroupCapabilities = 0x08,
  HdaParamAudioWidgetCapabilities,
  HdaParamSupportedPcmRates,
  HdaParamSupportedFormats,
  HdaParamPinCapabilities,
  HdaParamInputAmplifierCapabilities,
  HdaParamConnectionListLength,
  HdaParamSupportedPowerStates,
  HdaParamProcessingCapabilities,
  HdaParamGpioCount,
  HdaParamOutputAmplifierCapabilities,
  HdaParamVolumeCapabilities,
  HdaParamMax = MAX_UINT8
} HDA_GET_PARAMETER_VERB_PARAMETER;

typedef enum {
  HdaFunctionGroupTypeReserved,
  HdaFunctionGroupTypeAudio,
  HdaFunctionGroupTypeVendorSpecMotem
} HDA_FUNCTION_GROUP_TYPE;

typedef enum {
  HdaWidgetTypeAudioOut,
  HdaWidgetTypeAutioIn,
  HdaWidgetTypeAudioMixer,
  HdaWidgetTypeAudioSelector,
  HdaWidgetTypePinComplex,
  HdaWidgetTypePowerWidget,
  HdaWidgetTypeVolumeKnob,
  HdaWidgetTypeBeepGenerator,
  HdaWidgetTypeVendor = 0xF,
  HdaWidgetTypeMax = MAX_UINT8
} HDA_WIDGET_TYPE;

#pragma pack(push)
#pragma pack(1)
typedef union {
  struct {
    UINT16 Supports64BitAddresses:1;
    UINT16 SerialDataOutputLines:2;
    UINT16 BidirectionalStreams:5;
    UINT16 InputStreams:4;
  UINT16 OutputStreams:4;
  } Bits;
  UINT16 Raw;
} HDA_GLOBAL_CAPABILITIES;

typedef union {
  struct {
    UINT32 ControllerReset:1;
    UINT32 FlushControl:1;
    UINT32 rsvd1:5;
    UINT32 AcceptUnsolicitedResponses:1;
    UINT32 rsvd2:22;
  } Bits;
  UINT32 Raw;
} HDA_GLOBAL_CONTROL;

typedef union {
  struct {
    UINT16 rsvd1:1;
    UINT16 FlushStatus:1;
    UINT16 rsvd2:14;
  } Bits;
  UINT16 Raw;
} HDA_GLOBAL_STATUS;

typedef union {
  struct {
    UINT32 interrupts:30;
    UINT32 ControllerInterrupt:1;
    UINT32 GlobalInterruptStatus:1;
  } Bits;
  UINT32 Raw;
} HDA_INTERRUPT_STATUS;

typedef union {
  struct {
    UINT32 StreamInterrupts:30;
    UINT32 StreamInterruptEnable:1;
    UINT32 GlobalInterruptEnable:1;
  } Bits;
  UINT32 Raw;
} HDA_INTERRUPT_CONTROL;

typedef union {
  struct {
    UINT16 ReadPtr:8;
    UINT16 rsvd:7;
    UINT16 Reset:1;
  } Bits;
  UINT16 Raw;
} HDA_CORB_RDPTR;

typedef union {
  struct {
    UINT8 MemErrIntEnable:1;
    UINT8 Run:1;
    UINT8 Rsvd:6;
  } Bits;
  UINT8 Raw;
} HDA_CORB_CONTROL;

typedef union {
  struct {
    UINT8 MemErr:1;
  UINT8 rsvd:7;
} Bits;
UINT8 Raw;
} HDA_CORB_STATUS;

typedef union {
  struct {
    UINT16 WritePtr:8;
    UINT16 rsvd:8;
  } Bits;
  UINT16 Raw;
} HDA_CORB_WRPTR;

typedef union {
  struct {
    UINT8 Size:2;
    UINT8 rsvd:2;
    UINT8 Cap:4;
  } Bits;
  UINT8 Raw;
} HDA_CORB_SIZE;

typedef union {
  struct {
    UINT16 WritePtr:8;
    UINT16 rsvd:8;
    UINT16 Reset:1;
  } Bits;
  UINT16 Raw;
} HDA_RIRB_WRPTR;

typedef union {
  struct {
    UINT16 Count:8;
    UINT16 rsvd:8;
  } Bits;
  UINT16 Raw;
} HDA_RESP_INT_CNT;

typedef union {
  struct {
    UINT8 ResponseInterrupt:1;
    UINT8 DmaRun:1;
    UINT8 OverrunInterrupt:1;
    UINT8 rsvd:5;
  } Bits;
  UINT8 Raw;
} HDA_RIRB_CONTROL;

typedef union {
  struct {
    UINT8 response:1;
    UINT8 rsvd:1;
    UINT8 Overrun:1;
    UINT8 rsvd2:5;
  } Bits;
  UINT8 Raw;
} HDA_RIRB_STATUS;

typedef union {
  struct {
    UINT8 Size:2;
    UINT8 rsvd:2;
    UINT8 Cap:4;
  } Bits;
  UINT8 Raw;
} HDA_RIRB_SIZE;

typedef struct {
  UINT32 Position;
  UINT32 rsvd;
} HDA_STREAM_DMA_POSITION;

typedef struct {
  UINT64 Address;
  UINT32 Length;
  UINT32 Config;
} HDA_BUFFER_DESCRIPTOR_LIST_ENTRY;

typedef union {
  struct {
    UINT32 Data:8;
    UINT32 Command:11;
    UINT32 NodeId:8;
    UINT32 CodecAddress:4;
  } Bits;
  UINT32 Raw;
} HDA_CORB_ENTRY;

typedef union {
  struct {
    UINT32 SdoLine:4;
    UINT32 Solicited:1;
    UINT32 rsvd:27;
  } Bits;
  UINT32 Raw;
} HDA_RIRB_RESP_EX;

typedef union {
  struct {
    UINT32 Response:21;
    UINT32 SubTag:5;
    UINT32 Tag:6;
  };
  UINT32 Raw;
} HDA_RIRB_RESPONSE_CONTENTS;

typedef union {
  struct {
    HDA_RIRB_RESPONSE_CONTENTS Contents;
    HDA_RIRB_RESP_EX Info;
  } Response;
  UINT64 Raw;
} HDA_RIRB_RESPONSE;

typedef union {
  struct {
    UINT16 Channels:4;
    UINT16 BitsPerSample:3;
    UINT16 rsvd:1;
    UINT16 Divisor:3;
    UINT16 Multiplier:3;
    UINT16 BaseRate:1;
    UINT16 IsNonPcm:1;
  } Pcm;
  UINT16 Raw;
} HDA_STREAM_FORMAT_DESCRIPTOR;

typedef struct {
  UINT32 VendorDeviceId;
  UINT32 RevisionId;
  UINT32 NodeCount;
  UINT32 FunctionGroupType;
  UINT32 AudioGroupCaps;
  UINT32 AudioWidgetCaps;
  UINT32 SupPcmRates;
  UINT32 SupFmts;
  UINT32 PinCaps;
  UINT32 InAmpCaps;
  UINT32 ConnListLen;
  UINT32 SupPowerStates;
  UINT32 ProcCaps;
  UINT32 GpioCount;
  UINT32 OutAmpCaps;
  UINT32 VolCaps;
} HDA_NODE;

#pragma pack(pop)

typedef struct {
  /// Handle to this HDA controller if required
  EFI_HANDLE Handle;
  /// The pointer to the allocated EFI_PCI_IO_PROTOCOL protocol instance for this device
  EFI_PCI_IO_PROTOCOL* PciIo;
  /// Size of the CORB in bytes
  UINTN CorbBytes;
  /// Size of the RIRB in bytes
  UINTN RirbBytes;
  /// The total number of CORB entries that can be written to the CORB
  UINT8 MaxCorbEntries;
  /// The total number of RIRB entries that can be read from the RIRB
  UINT8 MaxRirbEntries;
  /// Implementation-defined mapping handle for the CORB
  VOID* CorbMapping;
  /// Implementation-defined mapping handle for the RIRB
  VOID* RirbMapping;
  /// Physical address to the CORB
  EFI_PHYSICAL_ADDRESS CorbPhysAddress;
  /// Physical address to the RIRB
  EFI_PHYSICAL_ADDRESS RirbPhysAddress;
  /// Pointer to the CORB in virtual memory
  UINT32* Corb;
  /// Pointer to the RIRB in virtual memory
  UINT64* Rirb;
  /// BDL mappings and lists
  UINTN InputBdlBytes[16];
  UINTN OutputBdlBytes[16];
  UINTN BidirectionalBdlBytes[16];
  EFI_PHYSICAL_ADDRESS InputBdlPhysAddress[16];
  EFI_PHYSICAL_ADDRESS OutputBdlPhysAddress[16];
  EFI_PHYSICAL_ADDRESS BidirectionalBdlPhysAddress[16];
  VOID* InputBdlMapping[16];
  VOID* OutputBdlMapping[16];
  VOID* BidirectionalBdlMapping[16];
  HDA_BUFFER_DESCRIPTOR_LIST_ENTRY InputBdl[256];
  HDA_BUFFER_DESCRIPTOR_LIST_ENTRY OutputBdl[256];
  HDA_BUFFER_DESCRIPTOR_LIST_ENTRY BidirectionalBdl[256];
  /// Stream DMA positions
  UINTN StreamPositionsBytes;
  VOID* StreamPositionsMapping;
  EFI_PHYSICAL_ADDRESS StreamPositionsPhysAddress;
  HDA_STREAM_DMA_POSITION* StreamPositions;
  /// HDA nodes (there can be up to 4096)
  HDA_NODE Nodes[16][16];
  /// Current CORB write pointer
  UINT8 CorbWritePtr;
  /// Current RIRB read pointer
  UINT8 RirbReadPtr;
  /// List of nodes that are present
  UINT16 DetectedNodes;
} HDA_CONTROLLER;

// HDA vendor ID/device list
// Necessary to find an HDA controller, apparently
// Taken from http://wiki.kolibrios.org/wiki/Intel_High_Definition_Audio
// {vendor ID, device ID}
static UINT16 HDA_DEVICES[42][2] = {
  {0x8086, 0x2668},
  {0x8086, 0x269A},
  {0x8086, 0x27D8},
  {0x8086, 0x284B},
  {0x8086, 0x2911},
  {0x8086, 0x293E},
  {0x8086, 0x293F},
  {0x8086, 0x3A3E},
  {0x8086, 0x3A6E},
  {0x8086, 0x3B56},
  {0x8086, 0x3B57},
  {0x8086, 0x811B},
  {0x8086, 0x1C20},
  {0x10DE, 0x026C},
  {0x10DE, 0x0371},
  {0x10DE, 0x03E4},
  {0x10DE, 0x03F0},
  {0x10DE, 0x044A},
  {0x10DE, 0x044B},
  {0x10DE, 0x055C},
  {0x10DE, 0x055D},
  {0x10DE, 0x0774},
  {0x10DE, 0x0775},
  {0x10DE, 0x0776},
  {0x10DE, 0x0777},
  {0x10DE, 0x07FC},
  {0x10DE, 0x07FD},
  {0x10DE, 0x0AC0},
  {0x10DE, 0x0AC1},
  {0x10DE, 0x0AC2},
  {0x10DE, 0x0AC3},
  {0x10DE, 0x0D94},
  {0x10DE, 0x0D95},
  {0x10DE, 0x0D96},
  {0x10DE, 0x0D97},
  {0x1002, 0x437B},
  {0x1002, 0x4383},
  {0x1106, 0x3288},
  {0x1039, 0x7502},
  {0x10B9, 0x5461},
  {0x6549, 0x1200},
  {0x17F3, 0x3010}
};

