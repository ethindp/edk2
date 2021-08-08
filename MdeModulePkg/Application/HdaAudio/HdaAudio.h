/**

Intel HDA audio register and structure definitions

Copyright (C) 2021 Ethin Probst
*/

#pragma once
#include <Uefi.h>

// For a list of all registers and any associated notes, see
// https://wiki.osdev.org/Intel_High_Definition_Audio
typedef enum {
  HdaGcap = 0x00,
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
  HdaStreamControl = 0x80,
  HdaStreamStatus = 0x83,
  HdaStreamLinkPosInBuf = 0x84,
  HdaStreamCyclicBufferLength = 0x88,
  HdaStreamLastValidIndex = 0x8C,
  HdaStreamFifoSize = 0x90,
  HdaStreamFormat = 0x92,
  HdaStreamBdlLo = 0x98,
  HdaStreamBdlHi = 0x9C,
  HdaRegMax = MAX_UINT64
} HDA_REGISTER;

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
  UINT8 raw;
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

typedef union {
  struct {
    UINT32 InterruptOnCompletion:1;
    UINT32 rsvd;
  } Bits;
  UINT32 Raw;
} HDA_BDLE_CONFIG;

typedef struct {
  UINT64 Address;
  UINT32 Length;
  HDA_BDLE_CONFIG Config;
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
    UINT32 Subtag:5;
    UINT32 Tag:6;
  } UnsolicitedResponse;
  UINT32 SolicitedResponse;
} HDA_RIRB_RESPONSE_CONTENTS;

typedef struct {
  HDA_RIRB_RESPONSE_CONTENTS Contents;
  HDA_RIRB_RESP_EX Info;
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

#pragma pack(pop)

typedef struct {
  /// Handle to this HDA controller if required
  EFI_HANDLE Handle;
  /// The pointer to the allocated EFI_PCI_IO_PROTOCOL protocol instance for this device
  PciIo* PciIo;
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
  HDA_CORB_ENTRY* Corb;
  /// Pointer to the RIRB in virtual memory
  HDA_RIRB_RESPONSE* Rirb;
  /// Current CORB write pointer
  UINT8 CorbWritePtr;
  /// Current RIRB read pointer
  UINT8 RirbReadPtr;
  /// List of node IDs
  UINT8 Nodes[15];
} HDA_CONTROLLER;

// HDA vendor ID/device list
// Necessary to find an HDA controller, apparently
// Taken from http://wiki.kolibrios.org/wiki/Intel_High_Definition_Audio
// {vendor ID, device ID}
static UINT16 HDA_DEVICES[][] = {
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

