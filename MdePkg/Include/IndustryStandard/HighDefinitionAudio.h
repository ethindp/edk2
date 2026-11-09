/** @file
  Definitions based on the Intel High Definition Audio Specification.

  Copyright (c) 2026, Ethin Probst. All rights reserved.<BR>
  SPDX-License-Identifier: BSD-2-Clause-Patent

  @par Specification Reference:
  High Definition Audio Specification, Revision 1.0a

**/

#pragma once

//
// PCI subclass of PCI_CLASS_MEDIA for HD Audio controllers (Section 3.1)
//
#define HDA_PCI_SUBCLASS  0x03

//
// Controller register offsets (Section 3.3)
//
#define HDA_REG_GCAP       0x00 ///< Global Capabilities, 16 bits
#define HDA_REG_VMIN       0x02 ///< Minor Version, 8 bits
#define HDA_REG_VMAJ       0x03 ///< Major Version, 8 bits
#define HDA_REG_OUTPAY     0x04 ///< Output Payload Capability, 16 bits
#define HDA_REG_INPAY      0x06 ///< Input Payload Capability, 16 bits
#define HDA_REG_GCTL       0x08 ///< Global Control, 32 bits
#define HDA_REG_WAKEEN     0x0C ///< Wake Enable, 16 bits
#define HDA_REG_STATESTS   0x0E ///< State Change Status, 16 bits
#define HDA_REG_GSTS       0x10 ///< Global Status, 16 bits
#define HDA_REG_INTCTL     0x20 ///< Interrupt Control, 32 bits
#define HDA_REG_INTSTS     0x24 ///< Interrupt Status, 32 bits
#define HDA_REG_WALCLK     0x30 ///< Wall Clock Counter, 32 bits
#define HDA_REG_SSYNC      0x38 ///< Stream Synchronization, 32 bits
#define HDA_REG_CORBLBASE  0x40 ///< CORB Lower Base Address, 32 bits
#define HDA_REG_CORBUBASE  0x44 ///< CORB Upper Base Address, 32 bits
#define HDA_REG_CORBWP     0x48 ///< CORB Write Pointer, 16 bits
#define HDA_REG_CORBRP     0x4A ///< CORB Read Pointer, 16 bits
#define HDA_REG_CORBCTL    0x4C ///< CORB Control, 8 bits
#define HDA_REG_CORBSTS    0x4D ///< CORB Status, 8 bits
#define HDA_REG_CORBSIZE   0x4E ///< CORB Size, 8 bits
#define HDA_REG_RIRBLBASE  0x50 ///< RIRB Lower Base Address, 32 bits
#define HDA_REG_RIRBUBASE  0x54 ///< RIRB Upper Base Address, 32 bits
#define HDA_REG_RIRBWP     0x58 ///< RIRB Write Pointer, 16 bits
#define HDA_REG_RINTCNT    0x5A ///< Response Interrupt Count, 16 bits
#define HDA_REG_RIRBCTL    0x5C ///< RIRB Control, 8 bits
#define HDA_REG_RIRBSTS    0x5D ///< RIRB Status, 8 bits
#define HDA_REG_RIRBSIZE   0x5E ///< RIRB Size, 8 bits
#define HDA_REG_DPLBASE    0x70 ///< DMA Position Lower Base Address, 32 bits
#define HDA_REG_DPUBASE    0x74 ///< DMA Position Upper Base Address, 32 bits

//
// GCAP fields (Section 3.3.2)
//
#define HDA_GCAP_64OK  BIT0
#define HDA_GCAP_NSDO(Gcap)  (((Gcap) >> 1) & 0x3)
#define HDA_GCAP_BSS(Gcap)   (((Gcap) >> 3) & 0x1F)
#define HDA_GCAP_ISS(Gcap)   (((Gcap) >> 8) & 0xF)
#define HDA_GCAP_OSS(Gcap)   (((Gcap) >> 12) & 0xF)

//
// GCTL fields (Section 3.3.7)
//
#define HDA_GCTL_CRST    BIT0  ///< Controller Reset (0 = in reset)
#define HDA_GCTL_FCNTRL  BIT1  ///< Flush Control
#define HDA_GCTL_UNSOL   BIT8  ///< Accept Unsolicited Response Enable

//
// INTCTL fields (Section 3.3.14)
//
#define HDA_INTCTL_GIE  BIT31 ///< Global Interrupt Enable
#define HDA_INTCTL_CIE  BIT30 ///< Controller Interrupt Enable

//
// CORB fields (Sections 3.3.20 - 3.3.24)
//
#define HDA_CORBRP_RST    BIT15 ///< CORB Read Pointer Reset
#define HDA_CORBCTL_MEIE  BIT0  ///< CORB Memory Error Interrupt Enable
#define HDA_CORBCTL_RUN   BIT1  ///< Enable CORB DMA Engine
#define HDA_CORBSTS_CMEI  BIT0  ///< CORB Memory Error Indication

//
// RIRB fields (Sections 3.3.27 - 3.3.31)
//
#define HDA_RIRBWP_RST       BIT15 ///< RIRB Write Pointer Reset
#define HDA_RIRBCTL_RINTCTL  BIT0  ///< Response Interrupt Control
#define HDA_RIRBCTL_DMAEN    BIT1  ///< RIRB DMA Enable
#define HDA_RIRBCTL_OIC      BIT2  ///< Response Overrun Interrupt Control
#define HDA_RIRBSTS_RINTFL   BIT0  ///< Response Interrupt (write 1 to clear)
#define HDA_RIRBSTS_RIRBOIS  BIT2  ///< Response Overrun Interrupt Status

//
// CORBSIZE / RIRBSIZE fields (Sections 3.3.24, 3.3.31)
//
#define HDA_RINGSIZE_SZCAP_2    BIT4
#define HDA_RINGSIZE_SZCAP_16   BIT5
#define HDA_RINGSIZE_SZCAP_256  BIT6
#define HDA_RINGSIZE_SIZE_MASK  0x3
#define HDA_RINGSIZE_SIZE_2     0x0
#define HDA_RINGSIZE_SIZE_16    0x1
#define HDA_RINGSIZE_SIZE_256   0x2

//
// Stream descriptors (Section 3.3.35 onwards). Input stream descriptors
// come first, then output, then bidirectional.
//
#define HDA_REG_SD_BASE    0x80
#define HDA_REG_SD_STRIDE  0x20
#define HDA_REG_SD(Index)  (HDA_REG_SD_BASE + (Index) * HDA_REG_SD_STRIDE)

#define HDA_REG_SD_CTL       0x00   ///< Control, 24 bits (bytes 0-2)
#define HDA_REG_SD_CTL_STRM  0x02   ///< Byte holding the stream number
#define HDA_REG_SD_STS       0x03   ///< Status, 8 bits
#define HDA_REG_SD_LPIB      0x04   ///< Link Position in Buffer, 32 bits
#define HDA_REG_SD_CBL       0x08   ///< Cyclic Buffer Length, 32 bits
#define HDA_REG_SD_LVI       0x0C   ///< Last Valid Index, 16 bits
#define HDA_REG_SD_FIFOS     0x10   ///< FIFO Size, 16 bits
#define HDA_REG_SD_FMT       0x12   ///< Format, 16 bits
#define HDA_REG_SD_BDPL      0x18   ///< BDL Pointer Lower Base Address, 32 bits
#define HDA_REG_SD_BDPU      0x1C   ///< BDL Pointer Upper Base Address, 32 bits

//
// SDnCTL byte 0 fields
//
#define HDA_SD_CTL_SRST  BIT0 ///< Stream Reset
#define HDA_SD_CTL_RUN   BIT1 ///< Stream Run
#define HDA_SD_CTL_IOCE  BIT2 ///< Interrupt on Completion Enable
#define HDA_SD_CTL_FEIE  BIT3 ///< FIFO Error Interrupt Enable
#define HDA_SD_CTL_DEIE  BIT4 ///< Descriptor Error Interrupt Enable

//
// SDnCTL byte 2 fields
//
#define HDA_SD_CTL_STRM_SHIFT  4    ///< Stream number, bits 23:20
#define HDA_SD_CTL_TP          BIT2 ///< Traffic Priority, bit 18
#define HDA_SD_CTL_DIR         BIT3 ///< Bidirectional direction, bit 19

//
// SDnSTS fields (write 1 to clear)
//
#define HDA_SD_STS_BCIS     BIT2 ///< Buffer Completion Interrupt Status
#define HDA_SD_STS_FIFOE    BIT3 ///< FIFO Error
#define HDA_SD_STS_DESE     BIT4 ///< Descriptor Error
#define HDA_SD_STS_FIFORDY  BIT5 ///< FIFO Ready

///
/// Maximum number of entries in a Buffer Descriptor List (Section 3.6.2).
///
#define HDA_BDL_MAX_ENTRIES  256

///
/// Alignment required for the CORB, the RIRB, a BDL, and the buffers a BDL
/// describes.
///
#define HDA_DMA_ALIGNMENT  128

#pragma pack(1)

///
/// Buffer Descriptor List Entry (Section 3.6.3, Table 50).
///
typedef struct {
  UINT64    Address;
  UINT32    Length;
  UINT32    Flags;
} HDA_BDL_ENTRY;

#define HDA_BDL_ENTRY_IOC  BIT0 ///< Interrupt on Completion

///
/// Response Input Ring Buffer entry (Section 4.4.2, Figure 22).
///
typedef struct {
  UINT32    Response;
  UINT32    ResponseEx;
} HDA_RIRB_ENTRY;

#pragma pack()

#define HDA_RIRB_RESPONSE_EX_CODEC(ResponseEx)  ((ResponseEx) & 0xF)
#define HDA_RIRB_RESPONSE_EX_UNSOL  BIT4

///
/// Maximum number of codecs on a link (Section 3.3.9, STATESTS).
///
#define HDA_MAX_CODECS  15

//
// Command verb encoding (Section 7.1.2). A verb is either a 12-bit verb
// with an 8-bit payload or a 4-bit verb with a 16-bit payload.
//
#define HDA_VERB_12(Cad, Nid, Verb, Payload) \
  ((((UINT32)(Cad) & 0xF) << 28) | (((UINT32)(Nid) & 0x7F) << 20) | \
   (((UINT32)(Verb) & 0xFFF) << 8) | ((UINT32)(Payload) & 0xFF))

#define HDA_VERB_4(Cad, Nid, Verb, Payload) \
  ((((UINT32)(Cad) & 0xF) << 28) | (((UINT32)(Nid) & 0x7F) << 20) | \
   (((UINT32)(Verb) & 0xF) << 16) | ((UINT32)(Payload) & 0xFFFF))

//
// 12-bit verbs (Section 7.3.3)
//
#define HDA_VERB_GET_PARAMETER              0xF00
#define HDA_VERB_GET_CONNECTION_SELECT      0xF01
#define HDA_VERB_SET_CONNECTION_SELECT      0x701
#define HDA_VERB_GET_CONNECTION_LIST_ENTRY  0xF02
#define HDA_VERB_GET_POWER_STATE            0xF05
#define HDA_VERB_SET_POWER_STATE            0x705
#define HDA_VERB_GET_CONVERTER_CONTROL      0xF06
#define HDA_VERB_SET_CONVERTER_CONTROL      0x706
#define HDA_VERB_GET_PIN_WIDGET_CONTROL     0xF07
#define HDA_VERB_SET_PIN_WIDGET_CONTROL     0x707
#define HDA_VERB_GET_PIN_SENSE              0xF09
#define HDA_VERB_EXECUTE_PIN_SENSE          0x709
#define HDA_VERB_GET_EAPD_BTL_ENABLE        0xF0C
#define HDA_VERB_SET_EAPD_BTL_ENABLE        0x70C
#define HDA_VERB_GET_CONFIGURATION_DEFAULT  0xF1C
#define HDA_VERB_FUNCTION_RESET             0x7FF

//
// 4-bit verbs (Section 7.3.3)
//
#define HDA_VERB_SET_CONVERTER_FORMAT  0x2
#define HDA_VERB_SET_AMP_GAIN_MUTE     0x3
#define HDA_VERB_GET_CONVERTER_FORMAT  0xA
#define HDA_VERB_GET_AMP_GAIN_MUTE     0xB

//
// Parameters for HDA_VERB_GET_PARAMETER (Section 7.3.4)
//
#define HDA_PARAM_VENDOR_ID                 0x00
#define HDA_PARAM_REVISION_ID               0x02
#define HDA_PARAM_SUBORDINATE_NODE_COUNT    0x04
#define HDA_PARAM_FUNCTION_GROUP_TYPE       0x05
#define HDA_PARAM_AUDIO_FG_CAPABILITIES     0x08
#define HDA_PARAM_AUDIO_WIDGET_CAPS         0x09
#define HDA_PARAM_SUPPORTED_PCM_SIZE_RATE   0x0A
#define HDA_PARAM_SUPPORTED_STREAM_FORMAT   0x0B
#define HDA_PARAM_PIN_CAPABILITIES          0x0C
#define HDA_PARAM_INPUT_AMP_CAPABILITIES    0x0D
#define HDA_PARAM_CONNECTION_LIST_LENGTH    0x0E
#define HDA_PARAM_SUPPORTED_POWER_STATES    0x0F
#define HDA_PARAM_PROCESSING_CAPABILITIES   0x10
#define HDA_PARAM_GPIO_COUNT                0x11
#define HDA_PARAM_OUTPUT_AMP_CAPABILITIES   0x12
#define HDA_PARAM_VOLUME_KNOB_CAPABILITIES  0x13

//
// Subordinate Node Count response (Section 7.3.4.3)
//
#define HDA_SUB_NODE_START(Response)  (((Response) >> 16) & 0xFF)
#define HDA_SUB_NODE_COUNT(Response)  ((Response) & 0xFF)

//
// Function Group Type response (Section 7.3.4.4)
//
#define HDA_FG_TYPE(Response)  ((Response) & 0xFF)
#define HDA_FG_TYPE_AUDIO  0x01
#define HDA_FG_TYPE_MODEM  0x02

//
// Audio Widget Capabilities response (Section 7.3.4.6)
//
#define HDA_WIDGET_CAP_STEREO        BIT0
#define HDA_WIDGET_CAP_IN_AMP        BIT1
#define HDA_WIDGET_CAP_OUT_AMP       BIT2
#define HDA_WIDGET_CAP_AMP_OVERRIDE  BIT3
#define HDA_WIDGET_CAP_FMT_OVERRIDE  BIT4
#define HDA_WIDGET_CAP_CONN_LIST     BIT8
#define HDA_WIDGET_CAP_DIGITAL       BIT9
#define HDA_WIDGET_CAP_POWER_CNTRL   BIT10
#define HDA_WIDGET_TYPE(Caps)  (((Caps) >> 20) & 0xF)

#define HDA_WIDGET_TYPE_AUDIO_OUTPUT    0x0
#define HDA_WIDGET_TYPE_AUDIO_INPUT     0x1
#define HDA_WIDGET_TYPE_AUDIO_MIXER     0x2
#define HDA_WIDGET_TYPE_AUDIO_SELECTOR  0x3
#define HDA_WIDGET_TYPE_PIN_COMPLEX     0x4
#define HDA_WIDGET_TYPE_POWER           0x5
#define HDA_WIDGET_TYPE_VOLUME_KNOB     0x6
#define HDA_WIDGET_TYPE_BEEP            0x7
#define HDA_WIDGET_TYPE_VENDOR          0xF

//
// Supported PCM Size, Rates response (Section 7.3.4.7)
//
#define HDA_PCM_RATE_44K1  BIT5
#define HDA_PCM_RATE_48K   BIT6
#define HDA_PCM_SIZE_16    BIT17

//
// Supported Stream Formats response (Section 7.3.4.8)
//
#define HDA_STREAM_FORMAT_PCM  BIT0

//
// Pin Capabilities response (Section 7.3.4.9)
//
#define HDA_PIN_CAP_TRIGGER_REQUIRED  BIT1
#define HDA_PIN_CAP_PRESENCE_DETECT   BIT2
#define HDA_PIN_CAP_HEADPHONE_DRIVE   BIT3
#define HDA_PIN_CAP_OUTPUT            BIT4
#define HDA_PIN_CAP_INPUT             BIT5
#define HDA_PIN_CAP_EAPD              BIT16

//
// Amplifier Capabilities response (Section 7.3.4.10)
//
#define HDA_AMP_CAP_OFFSET(Caps)     ((Caps) & 0x7F)
#define HDA_AMP_CAP_NUM_STEPS(Caps)  (((Caps) >> 8) & 0x7F)
#define HDA_AMP_CAP_STEP_SIZE(Caps)  (((Caps) >> 16) & 0x7F)
#define HDA_AMP_CAP_MUTE  BIT31

//
// Connection List Length response (Section 7.3.4.11)
//
#define HDA_CONN_LIST_LENGTH(Response)  ((Response) & 0x7F)
#define HDA_CONN_LIST_LONG_FORM  BIT7

//
// Set Amplifier Gain/Mute payload (Section 7.3.3.7)
//
#define HDA_AMP_SET_OUTPUT       BIT15
#define HDA_AMP_SET_INPUT        BIT14
#define HDA_AMP_SET_LEFT         BIT13
#define HDA_AMP_SET_RIGHT        BIT12
#define HDA_AMP_SET_INDEX_SHIFT  8
#define HDA_AMP_SET_MUTE         BIT7
#define HDA_AMP_SET_GAIN_MASK    0x7F

//
// Power states (Section 7.3.3.10)
//
#define HDA_POWER_STATE_D0  0x0
#define HDA_POWER_STATE_D3  0x3

//
// Pin Widget Control payload (Section 7.3.3.13)
//
#define HDA_PIN_CTL_IN_ENABLE   BIT5
#define HDA_PIN_CTL_OUT_ENABLE  BIT6
#define HDA_PIN_CTL_HP_ENABLE   BIT7

//
// Pin Sense response (Section 7.3.3.15)
//
#define HDA_PIN_SENSE_PRESENCE  BIT31

//
// EAPD/BTL Enable payload (Section 7.3.3.16)
//
#define HDA_EAPD_BTL_ENABLE_EAPD  BIT1

//
// Configuration Default response (Section 7.3.3.31)
//
#define HDA_CONFIG_DEFAULT_SEQUENCE(Config)      ((Config) & 0xF)
#define HDA_CONFIG_DEFAULT_ASSOCIATION(Config)   (((Config) >> 4) & 0xF)
#define HDA_CONFIG_DEFAULT_MISC(Config)          (((Config) >> 8) & 0xF)
#define HDA_CONFIG_DEFAULT_DEVICE(Config)        (((Config) >> 20) & 0xF)
#define HDA_CONFIG_DEFAULT_GEOMETRIC(Config)     (((Config) >> 24) & 0xF)
#define HDA_CONFIG_DEFAULT_GROSS(Config)         (((Config) >> 28) & 0x3)
#define HDA_CONFIG_DEFAULT_CONNECTIVITY(Config)  (((Config) >> 30) & 0x3)

///
/// Jack Detect Override. The jack has no presence detect circuitry even if
/// the pin capabilities report presence detect (Table 114).
///
#define HDA_CONFIG_MISC_NO_PRESENCE_DETECT  BIT0

//
// Location, gross (Table 110, bits 5:4)
//
#define HDA_CONFIG_GROSS_EXTERNAL  0x0
#define HDA_CONFIG_GROSS_INTERNAL  0x1
#define HDA_CONFIG_GROSS_SEPARATE  0x2
#define HDA_CONFIG_GROSS_OTHER     0x3

//
// Location, geometric (Table 110, bits 3:0)
//
#define HDA_CONFIG_GEOMETRIC_NA      0x0
#define HDA_CONFIG_GEOMETRIC_REAR    0x1
#define HDA_CONFIG_GEOMETRIC_FRONT   0x2
#define HDA_CONFIG_GEOMETRIC_LEFT    0x3
#define HDA_CONFIG_GEOMETRIC_RIGHT   0x4
#define HDA_CONFIG_GEOMETRIC_TOP     0x5
#define HDA_CONFIG_GEOMETRIC_BOTTOM  0x6

#define HDA_CONFIG_CONNECTIVITY_JACK   0x0
#define HDA_CONFIG_CONNECTIVITY_NONE   0x1
#define HDA_CONFIG_CONNECTIVITY_FIXED  0x2
#define HDA_CONFIG_CONNECTIVITY_BOTH   0x3

#define HDA_CONFIG_DEVICE_LINE_OUT           0x0
#define HDA_CONFIG_DEVICE_SPEAKER            0x1
#define HDA_CONFIG_DEVICE_HP_OUT             0x2
#define HDA_CONFIG_DEVICE_CD                 0x3
#define HDA_CONFIG_DEVICE_SPDIF_OUT          0x4
#define HDA_CONFIG_DEVICE_DIGITAL_OTHER_OUT  0x5
#define HDA_CONFIG_DEVICE_MODEM_LINE         0x6
#define HDA_CONFIG_DEVICE_MODEM_HANDSET      0x7
#define HDA_CONFIG_DEVICE_LINE_IN            0x8
#define HDA_CONFIG_DEVICE_AUX                0x9
#define HDA_CONFIG_DEVICE_MIC_IN             0xA
#define HDA_CONFIG_DEVICE_TELEPHONY          0xB
#define HDA_CONFIG_DEVICE_SPDIF_IN           0xC
#define HDA_CONFIG_DEVICE_DIGITAL_OTHER_IN   0xD
#define HDA_CONFIG_DEVICE_OTHER              0xF

//
// Stream format, shared by SDnFMT and the converter format verb
// (Section 3.7.1)
//
#define HDA_FMT_TYPE_NON_PCM  BIT15
#define HDA_FMT_BASE_44K1     BIT14
#define HDA_FMT_MULT_SHIFT    11
#define HDA_FMT_DIV_SHIFT     8
#define HDA_FMT_BITS_SHIFT    4
#define HDA_FMT_BITS_8        0x0
#define HDA_FMT_BITS_16       0x1
#define HDA_FMT_BITS_20       0x2
#define HDA_FMT_BITS_24       0x3
#define HDA_FMT_BITS_32       0x4
#define HDA_FMT_CHANNELS(Count)  (((Count) - 1) & 0xF)
