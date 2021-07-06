/** @file

  Private definitions of the VirtioRng RNG driver

  Copyright (C) 2016, Linaro Ltd.

  SPDX-License-Identifier: BSD-2-Clause-Patent

**/

#pragma once
#include <IndustryStandard/Virtio.h>
#define VIRTIO_SND_SIG SIGNATURE_32 ('V', 'S', 'N', 'D')

// Not all of the fields in this structure are used at the moment
typedef struct {
  UINT32 Signature;
  VIRTIO_DEVICE_PROTOCOL  *VirtIo;
  EFI_EVENT         ExitBoot;
  VRING Rings[4];
  UINT16 QueueSize[4];
  UINT64 BaseShift[4];
  void* RingMap[4];
} VIRTIO_SND_DEV;

typedef struct {
  UINT32 Jacks;
  UINT32 Streams;
} VIRTIO_SND_CONFIG;

typedef enum { 
  /* jack control request types */
  VIRTIO_SND_R_JACK_GET_CONFIG = 1,
  VIRTIO_SND_R_JACK_REMAP,

  /* PCM control request types */
  VIRTIO_SND_R_PCM_GET_CONFIG = 0x0100,
  VIRTIO_SND_R_PCM_SET_PARAMS,
  VIRTIO_SND_R_PCM_PREPARE,
  VIRTIO_SND_R_PCM_RELEASE,
  VIRTIO_SND_R_PCM_START,
  VIRTIO_SND_R_PCM_STOP
} VIRTIO_SND_CONTROL_REQUEST;

typedef enum {
  /* jack event types */
  VIRTIO_SND_EVT_JACK_CONNECTED = 0x1000,
  VIRTIO_SND_EVT_JACK_DISCONNECTED

  /* PCM event types */
  VIRTIO_SND_EVT_PCM_PERIOD_ELAPSED = 0x1100,
  VIRTIO_SND_EVT_PCM_XRUN
} VIRTIO_SND_EVENT_TYPE;

typedef enum {
  /* common status codes */
  VIRTIO_SND_S_OK = 0x8000,
  VIRTIO_SND_S_BAD_MSG,
  VIRTIO_SND_S_NOT_SUPP,
  VIRTIO_SND_S_IO_ERR
} VIRTIO_SND_STATUS;

typedef struct {
  UINT32 Code;
} VIRTIO_SND_HEADER;

typedef struct {
  VIRTIO_SND_HEADER Header;
  UINT32 Data;
} VIRTIO_SND_EVENT;

typedef enum {
  VIRTIO_SND_D_OUTPUT = 0,
  VIRTIO_SND_D_INPUT
} VIRTIO_SND_DATA_DIRECTION;

typedef struct {
  VIRTIO_SND_HEADER Header;
  UINT32 StartId;
  UINT32 Count;
  UINT32 Size;
} VIRTIO_SND_QUERY_INFO_REQUEST;

typedef struct {
  VIRTIO_SND_HEADER Header;
  UINT32 HdaFnNid;
} VIRTIO_SND_QUERY_INFO_RESPONSE_HEADER;

typedef struct {
  VIRTIO_SND_HEADER Header;
  UINT32 JackId;
} VIRTIO_SND_JACK_REQUEST_HEADER;

typedef enum {
  VIRTIO_SND_JACK_F_REMAP = 0
} VIRTIO_SND_JACK_FEATURES;

typedef struct {
  VIRTIO_SND_QUERY_INFO_RESPONSE_HEADER Header;
  UINT32 Features;
  UINT32 HdaFnNid;
  UINT32 HdaRegDefConfig;
  UINT32 HdaRegCaps;
  UINT8 Connected;
  UINT8 Padding[3];
} VIRTIO_SND_JACK_GET_CONFIG_RESPONSE;

typedef struct {
  VIRTIO_SND_JACK_REQUEST_HEADER Header;
  UINT32 Association;
  UINT32 Sequence;
} VIRTIO_SND_JACK_REMAP_REQUEST;

typedef struct {
  VIRTIO_SND_HEADER Header;
  UINT32 StreamId;
} VIRTIO_SND_PCM_REQUEST_HEADER;

/* supported PCM stream features */
typedef enum {
  VIRTIO_SND_PCM_F_SHMEM_HOST = 0,
  VIRTIO_SND_PCM_F_SHMEM_GUEST,
  VIRTIO_SND_PCM_F_MSG_POLLING,
  VIRTIO_SND_PCM_F_EVT_SHMEM_PERIODS,
  VIRTIO_SND_PCM_F_EVT_XRUNS
} VIRTIO_SND_PCM_FEATURES;

/* supported PCM sample formats */
typedef enum {
  /* analog formats (width / physical width) */
  VIRTIO_SND_PCM_FMT_IMA_ADPCM = 0,   /*   4 /  4 bits */
  VIRTIO_SND_PCM_FMT_MU_LAW,      /*  8 /  8 bits */
  VIRTIO_SND_PCM_FMT_A_LAW,       /*  8 /  8 bits */
  VIRTIO_SND_PCM_FMT_S8,        /*  8 /  8 bits */
  VIRTIO_SND_PCM_FMT_U8,        /*  8 /  8 bits */
  VIRTIO_SND_PCM_FMT_S16,       /* 16 / 16 bits */
  VIRTIO_SND_PCM_FMT_U16,       /* 16 / 16 bits */
  VIRTIO_SND_PCM_FMT_S18_3,       /* 18 / 24 bits */
  VIRTIO_SND_PCM_FMT_U18_3,       /* 18 / 24 bits */
  VIRTIO_SND_PCM_FMT_S20_3,       /* 20 / 24 bits */
  VIRTIO_SND_PCM_FMT_U20_3,       /* 20 / 24 bits */
  VIRTIO_SND_PCM_FMT_S24_3,       /* 24 / 24 bits */
  VIRTIO_SND_PCM_FMT_U24_3,       /* 24 / 24 bits */
  VIRTIO_SND_PCM_FMT_S20,       /* 20 / 32 bits */
  VIRTIO_SND_PCM_FMT_U20,       /* 20 / 32 bits */
  VIRTIO_SND_PCM_FMT_S24,       /* 24 / 32 bits */
  VIRTIO_SND_PCM_FMT_U24,       /* 24 / 32 bits */
  VIRTIO_SND_PCM_FMT_S32,       /* 32 / 32 bits */
  VIRTIO_SND_PCM_FMT_U32,       /* 32 / 32 bits */
  VIRTIO_SND_PCM_FMT_FLOAT,       /* 32 / 32 bits */
  VIRTIO_SND_PCM_FMT_FLOAT64,     /* 64 / 64 bits */
  /* digital formats (width / physical width) */
  VIRTIO_SND_PCM_FMT_DSD_U8,      /*  8 /  8 bits */
  VIRTIO_SND_PCM_FMT_DSD_U16,     /* 16 / 16 bits */
  VIRTIO_SND_PCM_FMT_DSD_U32,     /* 32 / 32 bits */
  VIRTIO_SND_PCM_FMT_IEC958_SUBFRAME  /* 32 / 32 bits */
} VIRTIO_SND_PCM_FMT;

/* supported PCM frame rates */
typedef enum {
  VIRTIO_SND_PCM_RATE_5512 = 0,
  VIRTIO_SND_PCM_RATE_8000,
  VIRTIO_SND_PCM_RATE_11025,
  VIRTIO_SND_PCM_RATE_16000,
  VIRTIO_SND_PCM_RATE_22050,
  VIRTIO_SND_PCM_RATE_32000,
  VIRTIO_SND_PCM_RATE_44100,
  VIRTIO_SND_PCM_RATE_48000,
  VIRTIO_SND_PCM_RATE_64000,
  VIRTIO_SND_PCM_RATE_88200,
  VIRTIO_SND_PCM_RATE_96000,
  VIRTIO_SND_PCM_RATE_176400,
  VIRTIO_SND_PCM_RATE_192000,
  VIRTIO_SND_PCM_RATE_384000
} VIRTIO_SND_PCM_RATE;

typedef struct {
  VIRTIO_SND_QUERY_INFO_RESPONSE_HEADER header;
  UINT32 features; /* 1 << VIRTIO_SND_PCM_F_XXX */
  UINT64 formats; /* 1 << VIRTIO_SND_PCM_FMT_XXX */
  UINT64 rates; /* 1 << VIRTIO_SND_PCM_RATE_XXX */
  UINT8 direction;
  UINT8 channels_min;
  UINT8 channels_max;
  UINT8 padding[5];
} VIRTIO_SND_PCM_INFO_REQUEST;

typedef struct {
  VIRTIO_SND_PCM_REQUEST_HEADER Header;
  UINT32 BufferBytes;
  UINT32 PeriodBytes;
  UINT32 Features;
  UINT8 Channels;
  UINT8 SampleFormat;
  UINT8 SampleRate;
  UINT8 Padding;
} VIRTIO_SND_PCM_SET_PARAMETERS_REQUEST;


