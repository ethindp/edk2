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
  HdaRegMax = 0xFFFFFFFFFFFFFFFF // Force to UINT64
} HDA_REGISTER;

#pragma pack(push)
#pragma pack(1)
typedef union {
  struct {
    ///
    /// If 1, this controller supports 64-bit addresses. If 0, only 32-bit addresses are supported
    ///
    UINT16 Supports64BitAddresses:1;
    ///
    /// The number of serial data output (SDO) lines
    /// supported by the controller. There may be
    /// up to 4 SDOs supported.
    ///
    UINT16 SerialDataOutputLines:2;
    ///
    /// The number of bidirectional streams that are supported
    /// by this controller. There may be up to 30 streams supported.
    ///
    UINT16 BidirectionalStreams:5;
    ///
    /// The number of input streams supported
    /// by this controller. There may be up to 15 input streams supported.
    ///
    UINT16 InputStreams:4;
    ///
    /// The number of output streams supported
    /// by this controller. There may be up to 15 output streams supported.
    ///
  UINT16 OutputStreams:4;
  } Bits;
  UINT16 Raw;
} HDA_GLOBAL_CAPABILITIES;

#pragma pack(1)
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

#pragma pack(1)
typedef union {
  struct {
    UINT16 rsvd1:1;
    UINT16 FlushStatus:1;
    UINT16 rsvd2:14;
  } Bits;
  UINT16 Raw;
} HDA_GLOBAL_STATUS;

#pragma pack(1)
typedef union {
  struct {
    UINT32 interrupts:30;
    UINT32 ControllerInterrupt:1;
    UINT32 GlobalInterruptStatus:1;
  } Bits;
  UINT32 Raw;
} HDA_INTERRUPT_STATUS;

#pragma pack(1)
typedef union {
  struct {
    UINT32 StreamInterrupts:30;
    UINT32 StreamInterruptEnable:1;
    UINT32 GlobalInterruptEnable:1;
  } Bits;
  UINT32 Raw;
} HDA_INTERRUPT_CONTROL;

#pragma pack(1)
typedef union {
  struct {
    UINT16 ReadPtr:8;
    UINT16 rsvd:7;
    UINT16 Reset:1;
  } Bits;
  UINT16 Raw;
} HDA_CORB_RDPTR;

#pragma pack(1)
typedef union {
  struct {
    UINT8 MemErrIntEnable:1;
    UINT8 Run:1;
    UINT8 Rsvd:6;
  } Bits;
  UINT8 raw;
} HDA_CORB_CONTROL;

#pragma pack(1)
typedef union {
  struct {
    UINT8 MemErr:1;
  UINT8 rsvd:7;
} Bits;
UINT8 Raw;
} HDA_CORB_STATUS;

#pragma pack(1)
typedef union {
  struct {
    UINT16 WritePtr:8;
    UINT16 rsvd:8;
  } Bits;
  UINT16 Raw;
} HDA_CORB_WRPTR;

#pragma pack(1)
typedef union {
  struct {
    UINT8 Size:2;
    UINT8 rsvd:2;
    UINT8 Cap:4;
  } Bits;
  UINT8 Raw;
} HDA_CORB_SIZE;

#pragma pack(1)
typedef union {
  struct {
    UINT16 WritePtr:8;
    UINT16 rsvd:8;
    UINT16 Reset:1;
  } Bits;
  UINT16 Raw;
} HDA_RIRB_WRPTR;

#pragma pack(1)
typedef union {
  struct {
    UINT16 Count:8;
    UINT16 rsvd:8;
  } Bits;
  UINT16 Raw;
} HDA_RESP_INT_CNT;

#pragma pack(1)
typedef union {
  struct {
    UINT8 ResponseInterrupt:1;
    UINT8 DmaRun:1;
    UINT8 OverrunInterrupt:1;
    UINT8 rsvd:5;
  } Bits;
  UINT8 Raw;
} HDA_RIRB_CONTROL;

#pragma pack(1)
typedef union {
  struct {
    UINT8 response:1;
    UINT8 rsvd:1;
    UINT8 Overrun:1;
    UINT8 rsvd2:5;
  } Bits;
  UINT8 Raw;
} HDA_RIRB_STATUS;

#pragma pack(1)
typedef union {
  struct {
    UINT8 Size:2;
    UINT8 rsvd:2;
    UINT8 Cap:4;
  } Bits;
  UINT8 Raw;
} HDA_RIRB_SIZE;

#pragma pack(1)
typedef struct {
  UINT32 Position;
  UINT32 rsvd;
} HDA_STREAM_DMA_POSITION;

#pragma pack(1)
typedef union {
  struct {
    UINT32 InterruptOnCompletion:1;
    UINT32 rsvd;
  } Bits;
  UINT32 Raw;
} HDA_BDLE_CONFIG;

#pragma pack(1)
typedef struct {
  UINT64 Address;
  UINT32 Length;
  HDA_BDLE_CONFIG Config;
} HDA_BUFFER_DESCRIPTOR_LIST_ENTRY;

#pragma pack(1)
typedef union {
  struct {
    UINT32 Data:8;
    UINT32 Command:11;
    UINT32 NodeId:8;
    UINT32 CodecAddress:4;
  } Bits;
  UINT32 Raw;
} HDA_CORB_ENTRY;

#pragma pack(1)
typedef union {
  struct {
    UINT32 SdoLine:4;
    UINT32 Solicited:1;
    UINT32 rsvd:27;
  } Bits;
  UINT32 Raw;
} HDA_RIRB_RESP_EX;

#pragma pack(1)
typedef union {
  struct {
    UINT32 Response:21;
    UINT32 Subtag:5;
    UINT32 Tag:6;
  } UnsolicitedResponse;
  UINT32 SolicitedResponse;
} HDA_RIRB_RESPONSE_CONTENTS;

#pragma pack(1)
typedef struct {
  HDA_RIRB_RESPONSE_CONTENTS Contents;
  HDA_RIRB_RESP_EX Info;
} HDA_RIRB_RESPONSE;

#pragma pack(1)
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

