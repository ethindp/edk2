/** @file
  This is the main routine for initializing the Graphics Console support routines.

Copyright (c) 2006 - 2008 Intel Corporation. <BR>
All rights reserved. This program and the accompanying materials
are licensed and made available under the terms and conditions of the BSD License
which accompanies this distribution.  The full text of the license may be found at
http://opensource.org/licenses/bsd-license.php

THE PROGRAM IS DISTRIBUTED UNDER THE BSD LICENSE ON AN "AS IS" BASIS,
WITHOUT WARRANTIES OR REPRESENTATIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED.

**/

#include "GraphicsConsole.h"


//
// Graphics Console Devcie Private Data template
//
GRAPHICS_CONSOLE_DEV    mGraphicsConsoleDevTemplate = {
  GRAPHICS_CONSOLE_DEV_SIGNATURE,
  (EFI_GRAPHICS_OUTPUT_PROTOCOL *) NULL,
  (EFI_UGA_DRAW_PROTOCOL *) NULL,
  {
    GraphicsConsoleConOutReset,
    GraphicsConsoleConOutOutputString,
    GraphicsConsoleConOutTestString,
    GraphicsConsoleConOutQueryMode,
    GraphicsConsoleConOutSetMode,
    GraphicsConsoleConOutSetAttribute,
    GraphicsConsoleConOutClearScreen,
    GraphicsConsoleConOutSetCursorPosition,
    GraphicsConsoleConOutEnableCursor,
    (EFI_SIMPLE_TEXT_OUTPUT_MODE *) NULL
  },
  {
    0,
    0,
    EFI_TEXT_ATTR(EFI_LIGHTGRAY, EFI_BLACK),
    0,
    0,
    TRUE
  },
  {
    { 80, 25, 0, 0, 0, 0 },  // Mode 0
    { 80, 50, 0, 0, 0, 0 },  // Mode 1
    { 100,31, 0, 0, 0, 0 },  // Mode 2
    {  0,  0, 0, 0, 0, 0 }   // Mode 3
  },
  (EFI_GRAPHICS_OUTPUT_BLT_PIXEL *) NULL,
  (EFI_HII_HANDLE ) 0
};

EFI_HII_DATABASE_PROTOCOL   *mHiiDatabase;
EFI_HII_FONT_PROTOCOL       *mHiiFont;
BOOLEAN                     mFirstAccessFlag = TRUE;

EFI_GUID             mFontPackageListGuid = {0xf5f219d3, 0x7006, 0x4648, {0xac, 0x8d, 0xd6, 0x1d, 0xfb, 0x7b, 0xc6, 0xad}};

CHAR16               mCrLfString[3] = { CHAR_CARRIAGE_RETURN, CHAR_LINEFEED, CHAR_NULL };

EFI_GRAPHICS_OUTPUT_BLT_PIXEL        mEfiColors[16] = {
  //
  // B     G     R
  //
  {0x00, 0x00, 0x00, 0x00},  // BLACK
  {0x98, 0x00, 0x00, 0x00},  // BLUE
  {0x00, 0x98, 0x00, 0x00},  // GREEN
  {0x98, 0x98, 0x00, 0x00},  // CYAN
  {0x00, 0x00, 0x98, 0x00},  // RED
  {0x98, 0x00, 0x98, 0x00},  // MAGENTA
  {0x00, 0x98, 0x98, 0x00},  // BROWN
  {0x98, 0x98, 0x98, 0x00},  // LIGHTGRAY
  {0x30, 0x30, 0x30, 0x00},  // DARKGRAY - BRIGHT BLACK
  {0xff, 0x00, 0x00, 0x00},  // LIGHTBLUE - ?
  {0x00, 0xff, 0x00, 0x00},  // LIGHTGREEN - ?
  {0xff, 0xff, 0x00, 0x00},  // LIGHTCYAN
  {0x00, 0x00, 0xff, 0x00},  // LIGHTRED
  {0xff, 0x00, 0xff, 0x00},  // LIGHTMAGENTA
  {0x00, 0xff, 0xff, 0x00},  // LIGHTBROWN
  {0xff, 0xff, 0xff, 0x00}  // WHITE
};

EFI_NARROW_GLYPH     mCursorGlyph = {
  0x0000,
  0x00,
  { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF }
};

CHAR16       SpaceStr[] = { NARROW_CHAR, ' ', 0 };

EFI_DRIVER_BINDING_PROTOCOL gGraphicsConsoleDriverBinding = {
  GraphicsConsoleControllerDriverSupported,
  GraphicsConsoleControllerDriverStart,
  GraphicsConsoleControllerDriverStop,
  0xa,
  NULL,
  NULL
};

/**
  Gets Graphics Console devcie's foreground color and background color.

  @param  This                  Protocol instance pointer.
  @param  Foreground            Returned text foreground color.
  @param  Background            Returned text background color.

  @retval EFI_SUCCESS           It returned always.

**/
EFI_STATUS
GetTextColors (
  IN  EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL  *This,
  OUT EFI_GRAPHICS_OUTPUT_BLT_PIXEL    *Foreground,
  OUT EFI_GRAPHICS_OUTPUT_BLT_PIXEL    *Background
  );

/**
  Draw Unicode string on the Graphice Console device's screen.

  @param  This                  Protocol instance pointer.
  @param  UnicodeWeight         One Unicode string to be displayed.
  @param  Count                 The count of Unicode string.

  @retval EFI_OUT_OF_RESOURCES  If no memory resource to use.
  @retval EFI_UNSUPPORTED       If no Graphics Output protocol and UGA Draw
                                protocol exist.
  @retval EFI_SUCCESS           Drawing Unicode string implemented successfully.

**/
EFI_STATUS
DrawUnicodeWeightAtCursorN (
  IN  EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL  *This,
  IN  CHAR16                           *UnicodeWeight,
  IN  UINTN                            Count
  );

/**
  Erase the cursor on the screen.

  @param  This                  Protocol instance pointer.

  @retval EFI_SUCCESS           The cursor is erased successfully.

**/
EFI_STATUS
EraseCursor (
  IN  EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL  *This
  );

/**
  Check if the current specific mode supported the user defined resolution
  for the Graphice Console devcie based on Graphics Output Protocol.

  If yes, set the graphic devcice's current mode to this specific mode.
  
  @param  GraphicsOutput        Graphics Output Protocol instance pointer.
  @param  HorizontalResolution  User defined horizontal resolution
  @param  VerticalResolution    User defined vertical resolution.
  @param  CurrentModeNumber     Current specific mode to be check.

  @retval EFI_SUCCESS       The mode is supported.
  @retval EFI_UNSUPPORTED   The specific mode is out of range of graphics 
                            devcie supported.
  @retval other             The specific mode does not support user defined 
                            resolution or failed to set the current mode to the 
                            specific mode on graphics device.

**/
EFI_STATUS
CheckModeSupported (
  EFI_GRAPHICS_OUTPUT_PROTOCOL  *GraphicsOutput,
  IN  UINT32  HorizontalResolution,
  IN  UINT32  VerticalResolution,
  OUT UINT32  *CurrentModeNumber
  );


/**
  Test to see if Graphics Console could be supported on the Controller.

  Graphics Console could be supported if Graphics Output Protocol or UGA Draw
  Protocol exists on the Controller. (UGA Draw Protocol could be skipped
  if PcdUgaConsumeSupport is set to FALSE.)

  @param  This                Protocol instance pointer.
  @param  Controller          Handle of device to test.
  @param  RemainingDevicePath Optional parameter use to pick a specific child
                              device to start.

  @retval EFI_SUCCESS         This driver supports this device.
  @retval other               This driver does not support this device.

**/
EFI_STATUS
EFIAPI
GraphicsConsoleControllerDriverSupported (
  IN EFI_DRIVER_BINDING_PROTOCOL    *This,
  IN EFI_HANDLE                     Controller,
  IN EFI_DEVICE_PATH_PROTOCOL       *RemainingDevicePath
  )
{
  EFI_STATUS                   Status;
  EFI_GRAPHICS_OUTPUT_PROTOCOL *GraphicsOutput;
  EFI_UGA_DRAW_PROTOCOL        *UgaDraw;
  EFI_DEVICE_PATH_PROTOCOL     *DevicePath;

  GraphicsOutput = NULL;
  UgaDraw        = NULL;
  //
  // Open the IO Abstraction(s) needed to perform the supported test
  //
  Status = gBS->OpenProtocol (
                  Controller,
                  &gEfiGraphicsOutputProtocolGuid,
                  (VOID **) &GraphicsOutput,
                  This->DriverBindingHandle,
                  Controller,
                  EFI_OPEN_PROTOCOL_BY_DRIVER
                  );

  if (EFI_ERROR (Status) && FeaturePcdGet (PcdUgaConsumeSupport)) {
    //
    // Open Graphics Output Protocol failed, try to open UGA Draw Protocol
    //
    Status = gBS->OpenProtocol (
                    Controller,
                    &gEfiUgaDrawProtocolGuid,
                    (VOID **) &UgaDraw,
                    This->DriverBindingHandle,
                    Controller,
                    EFI_OPEN_PROTOCOL_BY_DRIVER
                    );
  }
  if (EFI_ERROR (Status)) {
    return Status;
  }

  //
  // We need to ensure that we do not layer on top of a virtual handle.
  // We need to ensure that the handles produced by the conspliter do not
  // get used.
  //
  Status = gBS->OpenProtocol (
                  Controller,
                  &gEfiDevicePathProtocolGuid,
                  (VOID **) &DevicePath,
                  This->DriverBindingHandle,
                  Controller,
                  EFI_OPEN_PROTOCOL_BY_DRIVER
                  );
  if (!EFI_ERROR (Status)) {
    gBS->CloseProtocol (
          Controller,
          &gEfiDevicePathProtocolGuid,
          This->DriverBindingHandle,
          Controller
          );
  } else {
    goto Error;
  }

  //
  // Does Hii Exist?  If not, we aren't ready to run
  //
  Status = EfiLocateHiiProtocol ();

  //
  // Close the I/O Abstraction(s) used to perform the supported test
  //
Error:
  if (GraphicsOutput != NULL) {
    gBS->CloseProtocol (
          Controller,
          &gEfiGraphicsOutputProtocolGuid,
          This->DriverBindingHandle,
          Controller
          );
  } else if (FeaturePcdGet (PcdUgaConsumeSupport)) {
    gBS->CloseProtocol (
          Controller,
          &gEfiUgaDrawProtocolGuid,
          This->DriverBindingHandle,
          Controller
          );
  }
  return Status;
}


/**
  Start this driver on Controller by opening Graphics Output protocol or 
  UGA Draw protocol, and installing Simple Text Out protocol on Controller.
  (UGA Draw protocol could be shkipped if PcdUgaConsumeSupport is set to FALSE.)
  
  @param  This                 Protocol instance pointer.
  @param  Controller           Handle of device to bind driver to
  @param  RemainingDevicePath  Optional parameter use to pick a specific child
                               device to start.

  @retval EFI_SUCCESS          This driver is added to Controller.
  @retval other                This driver does not support this device.

**/
EFI_STATUS
EFIAPI
GraphicsConsoleControllerDriverStart (
  IN EFI_DRIVER_BINDING_PROTOCOL    *This,
  IN EFI_HANDLE                     Controller,
  IN EFI_DEVICE_PATH_PROTOCOL       *RemainingDevicePath
  )
{
  EFI_STATUS                           Status;
  GRAPHICS_CONSOLE_DEV                 *Private;
  UINTN                                NarrowFontSize;
  UINT32                               HorizontalResolution;
  UINT32                               VerticalResolution;
  UINT32                               ColorDepth;
  UINT32                               RefreshRate;
  UINTN                                MaxMode;
  UINTN                                Columns;
  UINTN                                Rows;
  UINT32                               ModeNumber;
  EFI_HII_SIMPLE_FONT_PACKAGE_HDR      *SimplifiedFont;
  UINTN                                PackageLength;
  EFI_HII_PACKAGE_LIST_HEADER          *PackageList;
  UINT8                                *Package;
  UINT8                                *Location;

  ModeNumber = 0;

  //
  // Initialize the Graphics Console device instance
  //
  Private = AllocateCopyPool (
              sizeof (GRAPHICS_CONSOLE_DEV),
              &mGraphicsConsoleDevTemplate
              );
  if (Private == NULL) {
    return EFI_OUT_OF_RESOURCES;
  }

  Private->SimpleTextOutput.Mode = &(Private->SimpleTextOutputMode);

  Status = gBS->OpenProtocol (
                  Controller,
                  &gEfiGraphicsOutputProtocolGuid,
                  (VOID **) &Private->GraphicsOutput,
                  This->DriverBindingHandle,
                  Controller,
                  EFI_OPEN_PROTOCOL_BY_DRIVER
                  );

  if (EFI_ERROR(Status) && FeaturePcdGet (PcdUgaConsumeSupport)) {
    Status = gBS->OpenProtocol (
                    Controller,
                    &gEfiUgaDrawProtocolGuid,
                    (VOID **) &Private->UgaDraw,
                    This->DriverBindingHandle,
                    Controller,
                    EFI_OPEN_PROTOCOL_BY_DRIVER
                    );
  }

  if (EFI_ERROR (Status)) {
    goto Error;
  }

  NarrowFontSize  = ReturnNarrowFontSize ();

  if (mFirstAccessFlag) {
    //
    // Add 4 bytes to the header for entire length for HiiLibPreparePackageList use only.
    // Looks ugly. Might be updated when font tool is ready.
    //
    PackageLength   = sizeof (EFI_HII_SIMPLE_FONT_PACKAGE_HDR) + NarrowFontSize + 4;
    Package = AllocateZeroPool (PackageLength);
    if (Package == NULL) {
      return EFI_OUT_OF_RESOURCES;
    }
    CopyMem (Package, &PackageLength, 4);
    SimplifiedFont = (EFI_HII_SIMPLE_FONT_PACKAGE_HDR*) (Package + 4);
    SimplifiedFont->Header.Length        = (UINT32) (PackageLength - 4);
    SimplifiedFont->Header.Type          = EFI_HII_PACKAGE_SIMPLE_FONTS;
    SimplifiedFont->NumberOfNarrowGlyphs = (UINT16) (NarrowFontSize / sizeof (EFI_NARROW_GLYPH));

    Location = (UINT8 *) (&SimplifiedFont->NumberOfWideGlyphs + 1);
    CopyMem (Location, gUsStdNarrowGlyphData, NarrowFontSize);

    //
    // Add this simplified font package to a package list then install it.
    //
    PackageList = HiiLibPreparePackageList (1, &mFontPackageListGuid, Package);
    Status = mHiiDatabase->NewPackageList (mHiiDatabase, PackageList, NULL, &(Private->HiiHandle));
    ASSERT_EFI_ERROR (Status);
    FreePool (PackageList);
    FreePool (Package);

    mFirstAccessFlag = FALSE;
  }
  //
  // If the current mode information can not be retrieved, then attemp to set the default mode
  // of 800x600, 32 bit colot, 60 Hz refresh.
  //
  HorizontalResolution  = 800;
  VerticalResolution    = 600;

  if (Private->GraphicsOutput != NULL) {
    //
    // The console is build on top of Graphics Output Protocol, find the mode number
    // for the user-defined mode; if there are multiple video devices,
    // graphic console driver will set all the video devices to the same mode.
    //
    Status = CheckModeSupported (
                 Private->GraphicsOutput,
                 CURRENT_HORIZONTAL_RESOLUTION,
                 CURRENT_VERTICAL_RESOLUTION,
                 &ModeNumber
                 );
    if (!EFI_ERROR(Status)) {
      //
      // Update default mode to current mode
      //
      HorizontalResolution = CURRENT_HORIZONTAL_RESOLUTION;
      VerticalResolution   = CURRENT_VERTICAL_RESOLUTION;
    } else {
      //
      // if not supporting current mode, try 800x600 which is required by UEFI/EFI spec
      //
      Status = CheckModeSupported (
                   Private->GraphicsOutput,
                   800,
                   600,
                   &ModeNumber
                   );
    }

    if (EFI_ERROR (Status) || (ModeNumber == Private->GraphicsOutput->Mode->MaxMode)) {
      //
      // Set default mode failed or device don't support default mode, then get the current mode information
      //
      HorizontalResolution = Private->GraphicsOutput->Mode->Info->HorizontalResolution;
      VerticalResolution = Private->GraphicsOutput->Mode->Info->VerticalResolution;
      ModeNumber = Private->GraphicsOutput->Mode->Mode;
    }
  } else if (FeaturePcdGet (PcdUgaConsumeSupport)) {
    //
    // At first try to set user-defined resolution
    //
    ColorDepth            = 32;
    RefreshRate           = 60;
    Status = Private->UgaDraw->SetMode (
                                Private->UgaDraw,
                                CURRENT_HORIZONTAL_RESOLUTION,
                                CURRENT_VERTICAL_RESOLUTION,
                                ColorDepth,
                                RefreshRate
                                );
    if (!EFI_ERROR (Status)) {
      HorizontalResolution = CURRENT_HORIZONTAL_RESOLUTION;
      VerticalResolution   = CURRENT_VERTICAL_RESOLUTION;
    } else if (FeaturePcdGet (PcdUgaConsumeSupport)) {
      //
      // Try to set 800*600 which is required by UEFI/EFI spec
      //
      Status = Private->UgaDraw->SetMode (
                                  Private->UgaDraw,
                                  HorizontalResolution,
                                  VerticalResolution,
                                  ColorDepth,
                                  RefreshRate
                                  );
      if (EFI_ERROR (Status)) {
        Status = Private->UgaDraw->GetMode (
                                    Private->UgaDraw,
                                    &HorizontalResolution,
                                    &VerticalResolution,
                                    &ColorDepth,
                                    &RefreshRate
                                    );
        if (EFI_ERROR (Status)) {
          goto Error;
        }
      }
    } else {
      Status = EFI_UNSUPPORTED;
      goto Error;
    }
  }

  //
  // Compute the maximum number of text Rows and Columns that this current graphics mode can support
  //
  Columns = HorizontalResolution / EFI_GLYPH_WIDTH;
  Rows    = VerticalResolution / EFI_GLYPH_HEIGHT;

  //
  // See if the mode is too small to support the required 80x25 text mode
  //
  if (Columns < 80 || Rows < 25) {
    goto Error;
  }
  //
  // Add Mode #0 that must be 80x25
  //
  MaxMode = 0;
  Private->ModeData[MaxMode].GopWidth      = HorizontalResolution;
  Private->ModeData[MaxMode].GopHeight     = VerticalResolution;
  Private->ModeData[MaxMode].GopModeNumber = ModeNumber;
  Private->ModeData[MaxMode].DeltaX        = (HorizontalResolution - (80 * EFI_GLYPH_WIDTH)) >> 1;
  Private->ModeData[MaxMode].DeltaY        = (VerticalResolution - (25 * EFI_GLYPH_HEIGHT)) >> 1;
  MaxMode++;

  //
  // If it is possible to support Mode #1 - 80x50, than add it as an active mode
  //
  if (Rows >= 50) {
    Private->ModeData[MaxMode].GopWidth      = HorizontalResolution;
    Private->ModeData[MaxMode].GopHeight     = VerticalResolution;
    Private->ModeData[MaxMode].GopModeNumber = ModeNumber;
    Private->ModeData[MaxMode].DeltaX        = (HorizontalResolution - (80 * EFI_GLYPH_WIDTH)) >> 1;
    Private->ModeData[MaxMode].DeltaY        = (VerticalResolution - (50 * EFI_GLYPH_HEIGHT)) >> 1;
    MaxMode++;
  }

  //
  // If it is not to support Mode #1 - 80x50, then skip it
  //
  if (MaxMode < 2) {
    Private->ModeData[MaxMode].Columns       = 0;
    Private->ModeData[MaxMode].Rows          = 0;
    Private->ModeData[MaxMode].GopWidth      = HorizontalResolution;
    Private->ModeData[MaxMode].GopHeight     = VerticalResolution;
    Private->ModeData[MaxMode].GopModeNumber = ModeNumber;
    Private->ModeData[MaxMode].DeltaX        = 0;
    Private->ModeData[MaxMode].DeltaY        = 0;
    MaxMode++;
  }

  //
  // Add Mode #2 that must be 100x31 (graphic mode >= 800x600)
  //
  if (Columns >= 100 && Rows >= 31) {
    Private->ModeData[MaxMode].GopWidth      = HorizontalResolution;
    Private->ModeData[MaxMode].GopHeight     = VerticalResolution;
    Private->ModeData[MaxMode].GopModeNumber = ModeNumber;
    Private->ModeData[MaxMode].DeltaX        = (HorizontalResolution - (100 * EFI_GLYPH_WIDTH)) >> 1;
    Private->ModeData[MaxMode].DeltaY        = (VerticalResolution - (31 * EFI_GLYPH_HEIGHT)) >> 1;
    MaxMode++;
  }

  //
  // Add Mode #3 that uses the entire display for user-defined mode
  //
  if (HorizontalResolution > 800 && VerticalResolution > 600) {
    Private->ModeData[MaxMode].Columns       = HorizontalResolution/EFI_GLYPH_WIDTH;
    Private->ModeData[MaxMode].Rows          = VerticalResolution/EFI_GLYPH_HEIGHT;
    Private->ModeData[MaxMode].GopWidth      = HorizontalResolution;
    Private->ModeData[MaxMode].GopHeight     = VerticalResolution;
    Private->ModeData[MaxMode].GopModeNumber = ModeNumber;
    Private->ModeData[MaxMode].DeltaX        = (HorizontalResolution % EFI_GLYPH_WIDTH) >> 1;
    Private->ModeData[MaxMode].DeltaY        = (VerticalResolution % EFI_GLYPH_HEIGHT) >> 1;
    MaxMode++;
  }

  //
  // Update the maximum number of modes
  //
  Private->SimpleTextOutputMode.MaxMode = (INT32) MaxMode;

  //
  // Determine the number of text modes that this protocol can support
  //
  Status = GraphicsConsoleConOutSetMode (&Private->SimpleTextOutput, 0);
  if (EFI_ERROR (Status)) {
    goto Error;
  }

  DEBUG_CODE_BEGIN ();
    GraphicsConsoleConOutOutputString (&Private->SimpleTextOutput, (CHAR16 *)L"Graphics Console Started\n\r");
  DEBUG_CODE_END ();

  //
  // Install protocol interfaces for the Graphics Console device.
  //
  Status = gBS->InstallMultipleProtocolInterfaces (
                  &Controller,
                  &gEfiSimpleTextOutProtocolGuid,
                  &Private->SimpleTextOutput,
                  NULL
                  );

Error:
  if (EFI_ERROR (Status)) {
    //
    // Close the GOP and UGA Draw Protocol
    //
    if (Private->GraphicsOutput != NULL) {
      gBS->CloseProtocol (
            Controller,
            &gEfiGraphicsOutputProtocolGuid,
            This->DriverBindingHandle,
            Controller
            );
    } else if (FeaturePcdGet (PcdUgaConsumeSupport)) {
      gBS->CloseProtocol (
            Controller,
            &gEfiUgaDrawProtocolGuid,
            This->DriverBindingHandle,
            Controller
            );
    }

    //
    // Free private data
    //
    if (Private != NULL) {
      if (Private->LineBuffer != NULL) {
        FreePool (Private->LineBuffer);
      }
      FreePool (Private);
    }
  }

  return Status;
}

/**
  Stop this driver on Controller by removing Simple Text Out protocol 
  and closing the Graphics Output Protocol or UGA Draw protocol on Controller.
  (UGA Draw protocol could be shkipped if PcdUgaConsumeSupport is set to FALSE.)
  

  @param  This              Protocol instance pointer.
  @param  Controller        Handle of device to stop driver on
  @param  NumberOfChildren  Number of Handles in ChildHandleBuffer. If number of
                            children is zero stop the entire bus driver.
  @param  ChildHandleBuffer List of Child Handles to Stop.

  @retval EFI_SUCCESS       This driver is removed Controller.
  @retval EFI_NOT_STARTED   Simple Text Out protocol could not be found the 
                            Controller.
  @retval other             This driver was not removed from this device.

**/
EFI_STATUS
EFIAPI
GraphicsConsoleControllerDriverStop (
  IN  EFI_DRIVER_BINDING_PROTOCOL   *This,
  IN  EFI_HANDLE                    Controller,
  IN  UINTN                         NumberOfChildren,
  IN  EFI_HANDLE                    *ChildHandleBuffer
  )
{
  EFI_STATUS                       Status;
  EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL  *SimpleTextOutput;
  GRAPHICS_CONSOLE_DEV             *Private;

  Status = gBS->OpenProtocol (
                  Controller,
                  &gEfiSimpleTextOutProtocolGuid,
                  (VOID **) &SimpleTextOutput,
                  This->DriverBindingHandle,
                  Controller,
                  EFI_OPEN_PROTOCOL_GET_PROTOCOL
                  );
  if (EFI_ERROR (Status)) {
    return EFI_NOT_STARTED;
  }

  Private = GRAPHICS_CONSOLE_CON_OUT_DEV_FROM_THIS (SimpleTextOutput);

  Status = gBS->UninstallProtocolInterface (
                  Controller,
                  &gEfiSimpleTextOutProtocolGuid,
                  &Private->SimpleTextOutput
                  );

  if (!EFI_ERROR (Status)) {
    //
    // Close the GOP or UGA IO Protocol
    //
    if (Private->GraphicsOutput != NULL) {
      gBS->CloseProtocol (
            Controller,
            &gEfiGraphicsOutputProtocolGuid,
            This->DriverBindingHandle,
            Controller
            );
    } else if (FeaturePcdGet (PcdUgaConsumeSupport)) {
      gBS->CloseProtocol (
            Controller,
            &gEfiUgaDrawProtocolGuid,
            This->DriverBindingHandle,
            Controller
            );
    }

    //
    // Remove the font pack
    //
    if (Private->HiiHandle != NULL) {
      HiiLibRemovePackages (Private->HiiHandle);
      mFirstAccessFlag = TRUE;
    }

    //
    // Free our instance data
    //
    if (Private != NULL) {
      FreePool (Private->LineBuffer);
      FreePool (Private);
    }
  }

  return Status;
}

/**
  Check if the current specific mode supported the user defined resolution
  for the Graphice Console devcie based on Graphics Output Protocol.

  If yes, set the graphic devcice's current mode to this specific mode.
  
  @param  GraphicsOutput        Graphics Output Protocol instance pointer.
  @param  HorizontalResolution  User defined horizontal resolution
  @param  VerticalResolution    User defined vertical resolution.
  @param  CurrentModeNumber     Current specific mode to be check.

  @retval EFI_SUCCESS       The mode is supported.
  @retval EFI_UNSUPPORTED   The specific mode is out of range of graphics 
                            devcie supported.
  @retval other             The specific mode does not support user defined 
                            resolution or failed to set the current mode to the 
                            specific mode on graphics device.

**/
EFI_STATUS
CheckModeSupported (
  EFI_GRAPHICS_OUTPUT_PROTOCOL  *GraphicsOutput,
  IN  UINT32                    HorizontalResolution,
  IN  UINT32                    VerticalResolution,
  OUT UINT32                    *CurrentModeNumber
  )
{
  UINT32     ModeNumber;
  EFI_STATUS Status;
  UINTN      SizeOfInfo;
  EFI_GRAPHICS_OUTPUT_MODE_INFORMATION *Info;

  Status = EFI_SUCCESS;

  for (ModeNumber = 0; ModeNumber < GraphicsOutput->Mode->MaxMode; ModeNumber++) {
    Status = GraphicsOutput->QueryMode (
                       GraphicsOutput,
                       ModeNumber,
                       &SizeOfInfo,
                       &Info
                       );
    if (!EFI_ERROR (Status)) {
      if ((Info->HorizontalResolution == HorizontalResolution) &&
          (Info->VerticalResolution == VerticalResolution)) {
        Status = GraphicsOutput->SetMode (GraphicsOutput, ModeNumber);
        if (!EFI_ERROR (Status)) {
          gBS->FreePool (Info);
          break;
        }
      }
      gBS->FreePool (Info);
    }
  }

  if (ModeNumber == GraphicsOutput->Mode->MaxMode) {
    Status = EFI_UNSUPPORTED;
  }

  *CurrentModeNumber = ModeNumber;
  return Status;
}


/**
  Locate HII Database protocol and HII Font protocol.

  @retval  EFI_SUCCESS     HII Database protocol and HII Font protocol 
                           are located successfully.
  @return  other           Failed to locate HII Database protocol or 
                           HII Font protocol.

**/
EFI_STATUS
EfiLocateHiiProtocol (
  VOID
  )
{
  EFI_HANDLE  Handle;
  UINTN       Size;
  EFI_STATUS  Status;

  //
  // There should only be one - so buffer size is this
  //
  Size = sizeof (EFI_HANDLE);

  Status = gBS->LocateHandle (
                  ByProtocol,
                  &gEfiHiiDatabaseProtocolGuid,
                  NULL,
                  &Size,
                  (VOID **) &Handle
                  );

  if (EFI_ERROR (Status)) {
    return Status;
  }

  Status = gBS->HandleProtocol (
                  Handle,
                  &gEfiHiiDatabaseProtocolGuid,
                  (VOID **) &mHiiDatabase
                  );

  if (EFI_ERROR (Status)) {
    return Status;
  }

  Status = gBS->HandleProtocol (
                  Handle,
                  &gEfiHiiFontProtocolGuid,
                  (VOID **) &mHiiFont
                  );
  return Status;
}

//
// Body of the STO functions
//

/**
  Reset the text output device hardware and optionaly run diagnostics.
  
  Implements SIMPLE_TEXT_OUTPUT.Reset().
  If ExtendeVerification is TRUE, then perform dependent Graphics Console
  device reset, and set display mode to mode 0.
  If ExtendedVerification is FALSE, only set display mode to mode 0.

  @param  This                  Protocol instance pointer.
  @param  ExtendedVerification  Indicates that the driver may perform a more
                                exhaustive verification operation of the device
                                during reset.

  @retval EFI_SUCCESS          The text output device was reset.
  @retval EFI_DEVICE_ERROR     The text output device is not functioning correctly and
                               could not be reset.

**/
EFI_STATUS
EFIAPI
GraphicsConsoleConOutReset (
  IN  EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL  *This,
  IN  BOOLEAN                          ExtendedVerification
  )
{
  This->SetAttribute (This, EFI_TEXT_ATTR (This->Mode->Attribute & 0x0F, EFI_BACKGROUND_BLACK));
  return This->SetMode (This, 0);
}


/**
  Write a Unicode string to the output device.

  Implements SIMPLE_TEXT_OUTPUT.OutputString(). 
  The Unicode string will be converted to Glyphs and will be
  sent to the Graphics Console.

  @param  This                    Protocol instance pointer.
  @param  WString                 The NULL-terminated Unicode string to be displayed
                                  on the output device(s). All output devices must
                                  also support the Unicode drawing defined in this file.

  @retval EFI_SUCCESS             The string was output to the device.
  @retval EFI_DEVICE_ERROR        The device reported an error while attempting to output
                                  the text.
  @retval EFI_UNSUPPORTED         The output device's mode is not currently in a
                                  defined text mode.
  @retval EFI_WARN_UNKNOWN_GLYPH  This warning code indicates that some of the
                                  characters in the Unicode string could not be
                                  rendered and were skipped.

**/
EFI_STATUS
EFIAPI
GraphicsConsoleConOutOutputString (
  IN  EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL  *This,
  IN  CHAR16                           *WString
  )
{
  GRAPHICS_CONSOLE_DEV  *Private;
  EFI_GRAPHICS_OUTPUT_PROTOCOL   *GraphicsOutput;
  EFI_UGA_DRAW_PROTOCOL *UgaDraw;
  INTN                  Mode;
  UINTN                 MaxColumn;
  UINTN                 MaxRow;
  UINTN                 Width;
  UINTN                 Height;
  UINTN                 Delta;
  EFI_STATUS            Status;
  BOOLEAN               Warning;
  EFI_GRAPHICS_OUTPUT_BLT_PIXEL  Foreground;
  EFI_GRAPHICS_OUTPUT_BLT_PIXEL  Background;
  UINTN                 DeltaX;
  UINTN                 DeltaY;
  UINTN                 Count;
  UINTN                 Index;
  INT32                 OriginAttribute;
  EFI_TPL               OldTpl;

  Status = EFI_SUCCESS;

  OldTpl = gBS->RaiseTPL (TPL_NOTIFY);
  //
  // Current mode
  //
  Mode      = This->Mode->Mode;
  Private   = GRAPHICS_CONSOLE_CON_OUT_DEV_FROM_THIS (This);
  GraphicsOutput = Private->GraphicsOutput;
  UgaDraw   = Private->UgaDraw;

  MaxColumn = Private->ModeData[Mode].Columns;
  MaxRow    = Private->ModeData[Mode].Rows;
  DeltaX    = Private->ModeData[Mode].DeltaX;
  DeltaY    = Private->ModeData[Mode].DeltaY;
  Width     = MaxColumn * EFI_GLYPH_WIDTH;
  Height    = (MaxRow - 1) * EFI_GLYPH_HEIGHT;
  Delta     = Width * sizeof (EFI_GRAPHICS_OUTPUT_BLT_PIXEL);

  //
  // The Attributes won't change when during the time OutputString is called
  //
  GetTextColors (This, &Foreground, &Background);

  EraseCursor (This);

  Warning = FALSE;

  //
  // Backup attribute
  //
  OriginAttribute = This->Mode->Attribute;

  while (*WString != L'\0') {

    if (*WString == CHAR_BACKSPACE) {
      //
      // If the cursor is at the left edge of the display, then move the cursor
      // one row up.
      //
      if (This->Mode->CursorColumn == 0 && This->Mode->CursorRow > 0) {
        This->Mode->CursorRow--;
        This->Mode->CursorColumn = (INT32) (MaxColumn - 1);
        This->OutputString (This, SpaceStr);
        EraseCursor (This);
        This->Mode->CursorRow--;
        This->Mode->CursorColumn = (INT32) (MaxColumn - 1);
      } else if (This->Mode->CursorColumn > 0) {
        //
        // If the cursor is not at the left edge of the display, then move the cursor
        // left one column.
        //
        This->Mode->CursorColumn--;
        This->OutputString (This, SpaceStr);
        EraseCursor (This);
        This->Mode->CursorColumn--;
      }

      WString++;

    } else if (*WString == CHAR_LINEFEED) {
      //
      // If the cursor is at the bottom of the display, then scroll the display one
      // row, and do not update the cursor position. Otherwise, move the cursor
      // down one row.
      //
      if (This->Mode->CursorRow == (INT32) (MaxRow - 1)) {
        if (GraphicsOutput != NULL) {
          //
          // Scroll Screen Up One Row
          //
          GraphicsOutput->Blt (
                    GraphicsOutput,
                    NULL,
                    EfiBltVideoToVideo,
                    DeltaX,
                    DeltaY + EFI_GLYPH_HEIGHT,
                    DeltaX,
                    DeltaY,
                    Width,
                    Height,
                    Delta
                    );

          //
          // Print Blank Line at last line
          //
          GraphicsOutput->Blt (
                    GraphicsOutput,
                    &Background,
                    EfiBltVideoFill,
                    0,
                    0,
                    DeltaX,
                    DeltaY + Height,
                    Width,
                    EFI_GLYPH_HEIGHT,
                    Delta
                    );
        } else if (FeaturePcdGet (PcdUgaConsumeSupport)) {
          //
          // Scroll Screen Up One Row
          //
          UgaDraw->Blt (
                    UgaDraw,
                    NULL,
                    EfiUgaVideoToVideo,
                    DeltaX,
                    DeltaY + EFI_GLYPH_HEIGHT,
                    DeltaX,
                    DeltaY,
                    Width,
                    Height,
                    Delta
                    );

          //
          // Print Blank Line at last line
          //
          UgaDraw->Blt (
                    UgaDraw,
                    (EFI_UGA_PIXEL *) (UINTN) &Background,
                    EfiUgaVideoFill,
                    0,
                    0,
                    DeltaX,
                    DeltaY + Height,
                    Width,
                    EFI_GLYPH_HEIGHT,
                    Delta
                    );
        }
      } else {
        This->Mode->CursorRow++;
      }

      WString++;

    } else if (*WString == CHAR_CARRIAGE_RETURN) {
      //
      // Move the cursor to the beginning of the current row.
      //
      This->Mode->CursorColumn = 0;
      WString++;

    } else if (*WString == WIDE_CHAR) {

      This->Mode->Attribute |= EFI_WIDE_ATTRIBUTE;
      WString++;

    } else if (*WString == NARROW_CHAR) {

      This->Mode->Attribute &= (~ (UINT32) EFI_WIDE_ATTRIBUTE);
      WString++;

    } else {
      //
      // Print the character at the current cursor position and move the cursor
      // right one column. If this moves the cursor past the right edge of the
      // display, then the line should wrap to the beginning of the next line. This
      // is equivalent to inserting a CR and an LF. Note that if the cursor is at the
      // bottom of the display, and the line wraps, then the display will be scrolled
      // one line.
      // If wide char is going to be displayed, need to display one character at a time
      // Or, need to know the display length of a certain string.
      //
      // Index is used to determine how many character width units (wide = 2, narrow = 1)
      // Count is used to determine how many characters are used regardless of their attributes
      //
      for (Count = 0, Index = 0; (This->Mode->CursorColumn + Index) < MaxColumn; Count++, Index++) {
        if (WString[Count] == CHAR_NULL) {
          break;
        }

        if (WString[Count] == CHAR_BACKSPACE) {
          break;
        }

        if (WString[Count] == CHAR_LINEFEED) {
          break;
        }

        if (WString[Count] == CHAR_CARRIAGE_RETURN) {
          break;
        }

        if (WString[Count] == WIDE_CHAR) {
          break;
        }

        if (WString[Count] == NARROW_CHAR) {
          break;
        }
        //
        // Is the wide attribute on?
        //
        if (This->Mode->Attribute & EFI_WIDE_ATTRIBUTE) {
          //
          // If wide, add one more width unit than normal since we are going to increment at the end of the for loop
          //
          Index++;
          //
          // This is the end-case where if we are at column 79 and about to print a wide character
          // We should prevent this from happening because we will wrap inappropriately.  We should
          // not print this character until the next line.
          //
          if ((This->Mode->CursorColumn + Index + 1) > MaxColumn) {
            Index++;
            break;
          }
        }
      }

      Status = DrawUnicodeWeightAtCursorN (This, WString, Count);
      if (EFI_ERROR (Status)) {
        Warning = TRUE;
      }
      //
      // At the end of line, output carriage return and line feed
      //
      WString += Count;
      This->Mode->CursorColumn += (INT32) Index;
      if (This->Mode->CursorColumn > (INT32) MaxColumn) {
        This->Mode->CursorColumn -= 2;
        This->OutputString (This, SpaceStr);
      }

      if (This->Mode->CursorColumn >= (INT32) MaxColumn) {
        EraseCursor (This);
        This->OutputString (This, mCrLfString);
        EraseCursor (This);
      }
    }
  }

  This->Mode->Attribute = OriginAttribute;

  EraseCursor (This);

  if (Warning) {
    Status = EFI_WARN_UNKNOWN_GLYPH;
  }

  gBS->RestoreTPL (OldTpl);
  return Status;

}

/**
  Verifies that all characters in a Unicode string can be output to the 
  target device.

  Implements SIMPLE_TEXT_OUTPUT.QueryMode().
  If one of the characters in the *Wstring is neither valid valid Unicode
  drawing characters, not ASCII code, then this function will return
  EFI_UNSUPPORTED

  @param  This    Protocol instance pointer.
  @param  WString The NULL-terminated Unicode string to be examined for the output
                  device(s).

  @retval EFI_SUCCESS      The device(s) are capable of rendering the output string.
  @retval EFI_UNSUPPORTED  Some of the characters in the Unicode string cannot be
                           rendered by one or more of the output devices mapped
                           by the EFI handle.

**/
EFI_STATUS
EFIAPI
GraphicsConsoleConOutTestString (
  IN  EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL  *This,
  IN  CHAR16                           *WString
  )
{
  EFI_STATUS            Status;
  UINT16                Count;

  EFI_IMAGE_OUTPUT      *Blt;

  Blt   = NULL;
  Count = 0;

  while (WString[Count] != 0) {
    Status = mHiiFont->GetGlyph (
                         mHiiFont,
                         WString[Count],
                         NULL,
                         &Blt,
                         NULL
                         );
    if (Blt != NULL) {
      FreePool (Blt);
      Blt = NULL;
    }
    Count++;

    if (EFI_ERROR (Status)) {
      return EFI_UNSUPPORTED;
    }
  }

  return EFI_SUCCESS;
}


/**
  Returns information for an available text mode that the output device(s)
  supports

  Implements SIMPLE_TEXT_OUTPUT.QueryMode().
  It returnes information for an available text mode that the Graphics Console supports.
  In this driver,we only support text mode 80x25, which is defined as mode 0.

  @param  This                  Protocol instance pointer.
  @param  ModeNumber            The mode number to return information on.
  @param  Columns               The returned columns of the requested mode.
  @param  Rows                  The returned rows of the requested mode.

  @retval EFI_SUCCESS           The requested mode information is returned.
  @retval EFI_UNSUPPORTED       The mode number is not valid.

**/
EFI_STATUS
EFIAPI
GraphicsConsoleConOutQueryMode (
  IN  EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL  *This,
  IN  UINTN                            ModeNumber,
  OUT UINTN                            *Columns,
  OUT UINTN                            *Rows
  )
{
  GRAPHICS_CONSOLE_DEV  *Private;
  EFI_STATUS            Status;
  EFI_TPL               OldTpl;

  if (ModeNumber >= (UINTN) This->Mode->MaxMode) {
    return EFI_UNSUPPORTED;
  }

  OldTpl = gBS->RaiseTPL (TPL_NOTIFY);
  Status = EFI_SUCCESS;

  Private   = GRAPHICS_CONSOLE_CON_OUT_DEV_FROM_THIS (This);

  *Columns  = Private->ModeData[ModeNumber].Columns;
  *Rows     = Private->ModeData[ModeNumber].Rows;

  if (*Columns <= 0 && *Rows <= 0) {
    Status = EFI_UNSUPPORTED;
    goto Done;

  }

Done:
  gBS->RestoreTPL (OldTpl);
  return Status;
}


/**
  Sets the output device(s) to a specified mode.
  
  Implements SIMPLE_TEXT_OUTPUT.SetMode().
  Set the Graphics Console to a specified mode. In this driver, we only support mode 0.

  @param  This                  Protocol instance pointer.
  @param  ModeNumber            The text mode to set.

  @retval EFI_SUCCESS           The requested text mode is set.
  @retval EFI_DEVICE_ERROR      The requested text mode cannot be set because of 
                                Graphics Console device error.
  @retval EFI_UNSUPPORTED       The text mode number is not valid.

**/
EFI_STATUS
EFIAPI
GraphicsConsoleConOutSetMode (
  IN  EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL  *This,
  IN  UINTN                            ModeNumber
  )
{
  EFI_STATUS                      Status;
  GRAPHICS_CONSOLE_DEV            *Private;
  GRAPHICS_CONSOLE_MODE_DATA      *ModeData;
  EFI_GRAPHICS_OUTPUT_BLT_PIXEL   *NewLineBuffer;
  UINT32                          HorizontalResolution;
  UINT32                          VerticalResolution;
  EFI_GRAPHICS_OUTPUT_PROTOCOL    *GraphicsOutput;
  EFI_UGA_DRAW_PROTOCOL           *UgaDraw;
  UINT32                          ColorDepth;
  UINT32                          RefreshRate;
  EFI_TPL                         OldTpl;

  OldTpl = gBS->RaiseTPL (TPL_NOTIFY);

  Private   = GRAPHICS_CONSOLE_CON_OUT_DEV_FROM_THIS (This);
  GraphicsOutput = Private->GraphicsOutput;
  UgaDraw   = Private->UgaDraw;
  ModeData  = &(Private->ModeData[ModeNumber]);

  if (ModeData->Columns <= 0 && ModeData->Rows <= 0) {
    Status = EFI_UNSUPPORTED;
    goto Done;
  }

  //
  // Make sure the requested mode number is supported
  //
  if (ModeNumber >= (UINTN) This->Mode->MaxMode) {
    Status = EFI_UNSUPPORTED;
    goto Done;
  }

  if (ModeData->Columns <= 0 && ModeData->Rows <= 0) {
    Status = EFI_UNSUPPORTED;
    goto Done;
  }
  //
  // Attempt to allocate a line buffer for the requested mode number
  //
  NewLineBuffer = AllocatePool (sizeof (EFI_GRAPHICS_OUTPUT_BLT_PIXEL) * ModeData->Columns * EFI_GLYPH_WIDTH * EFI_GLYPH_HEIGHT);

  if (NewLineBuffer == NULL) {
    //
    // The new line buffer could not be allocated, so return an error.
    // No changes to the state of the current console have been made, so the current console is still valid
    //
    Status = EFI_OUT_OF_RESOURCES;
    goto Done;
  }
  //
  // If the mode has been set at least one other time, then LineBuffer will not be NULL
  //
  if (Private->LineBuffer != NULL) {
    //
    // Clear the current text window on the current graphics console
    //
    This->ClearScreen (This);

    //
    // If the new mode is the same as the old mode, then just return EFI_SUCCESS
    //
    if ((INT32) ModeNumber == This->Mode->Mode) {
      FreePool (NewLineBuffer);
      Status = EFI_SUCCESS;
      goto Done;
    }
    //
    // Otherwise, the size of the text console and/or the GOP/UGA mode will be changed,
    // so erase the cursor, and free the LineBuffer for the current mode
    //
    EraseCursor (This);

    FreePool (Private->LineBuffer);
  }
  //
  // Assign the current line buffer to the newly allocated line buffer
  //
  Private->LineBuffer = NewLineBuffer;

  if (GraphicsOutput != NULL) {
    if (ModeData->GopModeNumber != GraphicsOutput->Mode->Mode) {
      //
      // Either no graphics mode is currently set, or it is set to the wrong resolution, so set the new grapghics mode
      //
      Status = GraphicsOutput->SetMode (GraphicsOutput, ModeData->GopModeNumber);
      if (EFI_ERROR (Status)) {
        //
        // The mode set operation failed
        //
        goto Done;
      }
    } else {
      //
      // The current graphics mode is correct, so simply clear the entire display
      //
      Status = GraphicsOutput->Blt (
                          GraphicsOutput,
                          &mEfiColors[0],
                          EfiBltVideoFill,
                          0,
                          0,
                          0,
                          0,
                          ModeData->GopWidth,
                          ModeData->GopHeight,
                          0
                          );
    }
  } else if (FeaturePcdGet (PcdUgaConsumeSupport)) {
    //
    // Get the current UGA Draw mode information
    //
    Status = UgaDraw->GetMode (
                        UgaDraw,
                        &HorizontalResolution,
                        &VerticalResolution,
                        &ColorDepth,
                        &RefreshRate
                        );
    if (EFI_ERROR (Status) || HorizontalResolution != ModeData->GopWidth || VerticalResolution != ModeData->GopHeight) {
      //
      // Either no graphics mode is currently set, or it is set to the wrong resolution, so set the new grapghics mode
      //
      Status = UgaDraw->SetMode (
                          UgaDraw,
                          ModeData->GopWidth,
                          ModeData->GopHeight,
                          32,
                          60
                          );
      if (EFI_ERROR (Status)) {
        //
        // The mode set operation failed
        //
        goto Done;
      }
    } else {
      //
      // The current graphics mode is correct, so simply clear the entire display
      //
      Status = UgaDraw->Blt (
                          UgaDraw,
                          (EFI_UGA_PIXEL *) (UINTN) &mEfiColors[0],
                          EfiUgaVideoFill,
                          0,
                          0,
                          0,
                          0,
                          ModeData->GopWidth,
                          ModeData->GopHeight,
                          0
                          );
    }
  }

  //
  // The new mode is valid, so commit the mode change
  //
  This->Mode->Mode = (INT32) ModeNumber;

  //
  // Move the text cursor to the upper left hand corner of the displat and enable it
  //
  This->SetCursorPosition (This, 0, 0);

  Status = EFI_SUCCESS;

Done:
  gBS->RestoreTPL (OldTpl);
  return Status;
}


/**
  Sets the background and foreground colors for the OutputString () and
  ClearScreen () functions.

  Implements SIMPLE_TEXT_OUTPUT.SetAttribute().

  @param  This                  Protocol instance pointer.
  @param  Attribute             The attribute to set. Bits 0..3 are the foreground
                                color, and bits 4..6 are the background color. 
                                All other bits are undefined and must be zero.

  @retval EFI_SUCCESS           The requested attribute is set.
  @retval EFI_DEVICE_ERROR      The requested attribute cannot be set due to Graphics Console port error.
  @retval EFI_UNSUPPORTED       The attribute requested is not defined.

**/
EFI_STATUS
EFIAPI
GraphicsConsoleConOutSetAttribute (
  IN  EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL  *This,
  IN  UINTN                            Attribute
  )
{
  EFI_TPL               OldTpl;

  if ((Attribute | 0xFF) != 0xFF) {
    return EFI_UNSUPPORTED;
  }

  if ((INT32) Attribute == This->Mode->Attribute) {
    return EFI_SUCCESS;
  }

  OldTpl = gBS->RaiseTPL (TPL_NOTIFY);

  EraseCursor (This);

  This->Mode->Attribute = (INT32) Attribute;

  EraseCursor (This);

  gBS->RestoreTPL (OldTpl);

  return EFI_SUCCESS;
}


/**
  Clears the output device(s) display to the currently selected background 
  color.

  Implements SIMPLE_TEXT_OUTPUT.ClearScreen().

  @param  This                  Protocol instance pointer.

  @retval  EFI_SUCCESS      The operation completed successfully.
  @retval  EFI_DEVICE_ERROR The device had an error and could not complete the request.
  @retval  EFI_UNSUPPORTED  The output device is not in a valid text mode.

**/
EFI_STATUS
EFIAPI
GraphicsConsoleConOutClearScreen (
  IN  EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL  *This
  )
{
  EFI_STATUS                    Status;
  GRAPHICS_CONSOLE_DEV          *Private;
  GRAPHICS_CONSOLE_MODE_DATA    *ModeData;
  EFI_GRAPHICS_OUTPUT_PROTOCOL  *GraphicsOutput;
  EFI_UGA_DRAW_PROTOCOL         *UgaDraw;
  EFI_GRAPHICS_OUTPUT_BLT_PIXEL Foreground;
  EFI_GRAPHICS_OUTPUT_BLT_PIXEL Background;
  EFI_TPL                       OldTpl;

  OldTpl = gBS->RaiseTPL (TPL_NOTIFY);

  Private   = GRAPHICS_CONSOLE_CON_OUT_DEV_FROM_THIS (This);
  GraphicsOutput = Private->GraphicsOutput;
  UgaDraw   = Private->UgaDraw;
  ModeData  = &(Private->ModeData[This->Mode->Mode]);

  GetTextColors (This, &Foreground, &Background);
  if (GraphicsOutput != NULL) {
    Status = GraphicsOutput->Blt (
                        GraphicsOutput,
                        &Background,
                        EfiBltVideoFill,
                        0,
                        0,
                        0,
                        0,
                        ModeData->GopWidth,
                        ModeData->GopHeight,
                        0
                        );
  } else if (FeaturePcdGet (PcdUgaConsumeSupport)) {
    Status = UgaDraw->Blt (
                        UgaDraw,
                        (EFI_UGA_PIXEL *) (UINTN) &Background,
                        EfiUgaVideoFill,
                        0,
                        0,
                        0,
                        0,
                        ModeData->GopWidth,
                        ModeData->GopHeight,
                        0
                        );
  } else {
    Status = EFI_UNSUPPORTED;
  }

  This->Mode->CursorColumn  = 0;
  This->Mode->CursorRow     = 0;

  EraseCursor (This);

  gBS->RestoreTPL (OldTpl);

  return Status;
}


/**
  Sets the current coordinates of the cursor position.

  Implements SIMPLE_TEXT_OUTPUT.SetCursorPosition().

  @param  This        Protocol instance pointer.
  @param  Column      The position to set the cursor to. Must be greater than or
                      equal to zero and less than the number of columns and rows
                      by QueryMode ().
  @param  Row         The position to set the cursor to. Must be greater than or
                      equal to zero and less than the number of columns and rows
                      by QueryMode ().

  @retval EFI_SUCCESS      The operation completed successfully.
  @retval EFI_DEVICE_ERROR The device had an error and could not complete the request.
  @retval EFI_UNSUPPORTED  The output device is not in a valid text mode, or the
                           cursor position is invalid for the current mode.

**/
EFI_STATUS
EFIAPI
GraphicsConsoleConOutSetCursorPosition (
  IN  EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL  *This,
  IN  UINTN                            Column,
  IN  UINTN                            Row
  )
{
  GRAPHICS_CONSOLE_DEV        *Private;
  GRAPHICS_CONSOLE_MODE_DATA  *ModeData;
  EFI_STATUS                  Status;
  EFI_TPL                     OldTpl;

  Status = EFI_SUCCESS;

  OldTpl = gBS->RaiseTPL (TPL_NOTIFY);

  Private   = GRAPHICS_CONSOLE_CON_OUT_DEV_FROM_THIS (This);
  ModeData  = &(Private->ModeData[This->Mode->Mode]);

  if ((Column >= ModeData->Columns) || (Row >= ModeData->Rows)) {
    Status = EFI_UNSUPPORTED;
    goto Done;
  }

  if ((This->Mode->CursorColumn == (INT32) Column) && (This->Mode->CursorRow == (INT32) Row)) {
    Status = EFI_SUCCESS;
    goto Done;
  }

  EraseCursor (This);

  This->Mode->CursorColumn  = (INT32) Column;
  This->Mode->CursorRow     = (INT32) Row;

  EraseCursor (This);

Done:
  gBS->RestoreTPL (OldTpl);

  return Status;
}


/**
  Makes the cursor visible or invisible.

  Implements SIMPLE_TEXT_OUTPUT.EnableCursor().

  @param  This                  Protocol instance pointer.
  @param  Visible               If TRUE, the cursor is set to be visible, If FALSE,
                                the cursor is set to be invisible.

  @retval EFI_SUCCESS           The operation completed successfully.

**/
EFI_STATUS
EFIAPI
GraphicsConsoleConOutEnableCursor (
  IN  EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL  *This,
  IN  BOOLEAN                          Visible
  )
{
  EFI_TPL               OldTpl;

  OldTpl = gBS->RaiseTPL (TPL_NOTIFY);

  EraseCursor (This);

  This->Mode->CursorVisible = Visible;

  EraseCursor (This);

  gBS->RestoreTPL (OldTpl);
  return EFI_SUCCESS;
}

/**
  Gets Graphics Console devcie's foreground color and background color.

  @param  This                  Protocol instance pointer.
  @param  Foreground            Returned text foreground color.
  @param  Background            Returned text background color.

  @retval EFI_SUCCESS           It returned always.

**/
EFI_STATUS
GetTextColors (
  IN  EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL  *This,
  OUT EFI_GRAPHICS_OUTPUT_BLT_PIXEL    *Foreground,
  OUT EFI_GRAPHICS_OUTPUT_BLT_PIXEL    *Background
  )
{
  INTN  Attribute;

  Attribute   = This->Mode->Attribute & 0x7F;

  *Foreground = mEfiColors[Attribute & 0x0f];
  *Background = mEfiColors[Attribute >> 4];

  return EFI_SUCCESS;
}

/**
  Draw Unicode string on the Graphice Console device's screen.

  @param  This                  Protocol instance pointer.
  @param  UnicodeWeight         One Unicode string to be displayed.
  @param  Count                 The count of Unicode string.

  @retval EFI_OUT_OF_RESOURCES  If no memory resource to use.
  @retval EFI_UNSUPPORTED       If no Graphics Output protocol and UGA Draw
                                protocol exist.
  @retval EFI_SUCCESS           Drawing Unicode string implemented successfully.

**/
EFI_STATUS
DrawUnicodeWeightAtCursorN (
  IN  EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL  *This,
  IN  CHAR16                           *UnicodeWeight,
  IN  UINTN                            Count
  )
{
  EFI_STATUS                        Status;
  GRAPHICS_CONSOLE_DEV              *Private;
  EFI_IMAGE_OUTPUT                  *Blt;
  EFI_STRING                        String;
  EFI_FONT_DISPLAY_INFO             *FontInfo;
  EFI_UGA_DRAW_PROTOCOL             *UgaDraw;
  EFI_HII_ROW_INFO                  *RowInfoArray;
  UINTN                             RowInfoArraySize;

  Private = GRAPHICS_CONSOLE_CON_OUT_DEV_FROM_THIS (This);
  Blt = (EFI_IMAGE_OUTPUT *) AllocateZeroPool (sizeof (EFI_IMAGE_OUTPUT));
  if (Blt == NULL) {
    return EFI_OUT_OF_RESOURCES;
  }

  Blt->Width        = (UINT16) (Private->ModeData[This->Mode->Mode].GopWidth);
  Blt->Height       = (UINT16) (Private->ModeData[This->Mode->Mode].GopHeight);

  String = AllocateCopyPool ((Count + 1) * sizeof (CHAR16), UnicodeWeight);
  if (String == NULL) {
    FreePool (Blt);
    return EFI_OUT_OF_RESOURCES;
  }
  //
  // Set the end character
  //
  *(String + Count) = L'\0';

  FontInfo = (EFI_FONT_DISPLAY_INFO *) AllocateZeroPool (sizeof (EFI_FONT_DISPLAY_INFO));
  if (FontInfo == NULL) {
    FreePool (Blt);
    FreePool (String);
    return EFI_OUT_OF_RESOURCES;
  }
  //
  // Get current foreground and background colors.
  //
  GetTextColors (This, &FontInfo->ForegroundColor, &FontInfo->BackgroundColor);

  if (Private->GraphicsOutput != NULL) {
    //
    // If Graphcis Output protocol exists, using HII Font protocol to draw. 
    //
    Blt->Image.Screen = Private->GraphicsOutput;

    Status = mHiiFont->StringToImage (
                         mHiiFont,
                         EFI_HII_IGNORE_IF_NO_GLYPH | EFI_HII_DIRECT_TO_SCREEN,
                         String,
                         FontInfo,
                         &Blt,
                         This->Mode->CursorColumn * EFI_GLYPH_WIDTH + Private->ModeData[This->Mode->Mode].DeltaX,
                         This->Mode->CursorRow * EFI_GLYPH_HEIGHT + Private->ModeData[This->Mode->Mode].DeltaY,
                         NULL,
                         NULL,
                         NULL
                         );

  } else if (FeaturePcdGet (PcdUgaConsumeSupport)) {
    //
    // If Graphics Output protocol cannot be found and PcdUgaConsumeSupport enabled, 
    // using UGA Draw protocol to draw.
    //
    ASSERT (Private->UgaDraw!= NULL);

    UgaDraw = Private->UgaDraw;

    Blt->Image.Bitmap = AllocateZeroPool (Blt->Width * Blt->Height * sizeof (EFI_GRAPHICS_OUTPUT_BLT_PIXEL));
    if (Blt->Image.Bitmap == NULL) {
      FreePool (Blt);
      FreePool (String);
      return EFI_OUT_OF_RESOURCES;
    }

    RowInfoArray = NULL;
    //
    //  StringToImage only support blt'ing image to device using GOP protocol. If GOP is not supported in this platform,
    //  we ask StringToImage to print the string to blt buffer, then blt to device using UgaDraw.
    //
    Status = mHiiFont->StringToImage (
                          mHiiFont,
                          EFI_HII_IGNORE_IF_NO_GLYPH,
                          String,
                          FontInfo,
                          &Blt,
                          This->Mode->CursorColumn * EFI_GLYPH_WIDTH + Private->ModeData[This->Mode->Mode].DeltaX,
                          This->Mode->CursorRow * EFI_GLYPH_HEIGHT + Private->ModeData[This->Mode->Mode].DeltaY,
                          &RowInfoArray,
                          &RowInfoArraySize,
                          NULL
                          );

    if (!EFI_ERROR (Status)) {
      //
      // Line breaks are handled by caller of DrawUnicodeWeightAtCursorN, so the updated parameter RowInfoArraySize by StringToImage will
      // always be 1 or 0 (if there is no valid Unicode Char can be printed). ASSERT here to make sure.
      //
      ASSERT (RowInfoArraySize <= 1);

      Status = UgaDraw->Blt (
                          UgaDraw,
                          (EFI_UGA_PIXEL *) Blt->Image.Bitmap,
                          EfiUgaBltBufferToVideo,
                          This->Mode->CursorColumn * EFI_GLYPH_WIDTH  + Private->ModeData[This->Mode->Mode].DeltaX,
                          (This->Mode->CursorRow) * EFI_GLYPH_HEIGHT + Private->ModeData[This->Mode->Mode].DeltaY,
                          This->Mode->CursorColumn * EFI_GLYPH_WIDTH  + Private->ModeData[This->Mode->Mode].DeltaX,
                          (This->Mode->CursorRow) * EFI_GLYPH_HEIGHT + Private->ModeData[This->Mode->Mode].DeltaY,
                          RowInfoArray[0].LineWidth,
                          RowInfoArray[0].LineHeight,
                          Blt->Width * sizeof (EFI_GRAPHICS_OUTPUT_BLT_PIXEL)
                          );
    }

    FreePool (RowInfoArray);
    FreePool (Blt->Image.Bitmap);
  } else {
    Status = EFI_UNSUPPORTED;
  }

  if (Blt != NULL) {
    FreePool (Blt);
  }
  if (String != NULL) {
    FreePool (String);
  }
  if (FontInfo != NULL) {
    FreePool (FontInfo);
  }
  return Status;
}

/**
  Erase the cursor on the screen.

  @param  This                  Protocol instance pointer.

  @retval EFI_SUCCESS           The cursor is erased successfully.

**/
EFI_STATUS
EraseCursor (
  IN  EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL  *This
  )
{
  GRAPHICS_CONSOLE_DEV        *Private;
  EFI_SIMPLE_TEXT_OUTPUT_MODE *CurrentMode;
  INTN                        GlyphX;
  INTN                        GlyphY;
  EFI_GRAPHICS_OUTPUT_PROTOCOL        *GraphicsOutput;
  EFI_UGA_DRAW_PROTOCOL       *UgaDraw;
  EFI_GRAPHICS_OUTPUT_BLT_PIXEL_UNION Foreground;
  EFI_GRAPHICS_OUTPUT_BLT_PIXEL_UNION Background;
  EFI_GRAPHICS_OUTPUT_BLT_PIXEL_UNION BltChar[EFI_GLYPH_HEIGHT][EFI_GLYPH_WIDTH];
  UINTN                       PosX;
  UINTN                       PosY;

  CurrentMode = This->Mode;

  if (!CurrentMode->CursorVisible) {
    return EFI_SUCCESS;
  }

  Private = GRAPHICS_CONSOLE_CON_OUT_DEV_FROM_THIS (This);
  GraphicsOutput = Private->GraphicsOutput;
  UgaDraw = Private->UgaDraw;

  //
  // In this driver, only narrow character was supported.
  //
  //
  // Blt a character to the screen
  //
  GlyphX  = (CurrentMode->CursorColumn * EFI_GLYPH_WIDTH) + Private->ModeData[CurrentMode->Mode].DeltaX;
  GlyphY  = (CurrentMode->CursorRow * EFI_GLYPH_HEIGHT) + Private->ModeData[CurrentMode->Mode].DeltaY;
  if (GraphicsOutput != NULL) {
    GraphicsOutput->Blt (
              GraphicsOutput,
              (EFI_GRAPHICS_OUTPUT_BLT_PIXEL *) BltChar,
              EfiBltVideoToBltBuffer,
              GlyphX,
              GlyphY,
              0,
              0,
              EFI_GLYPH_WIDTH,
              EFI_GLYPH_HEIGHT,
              EFI_GLYPH_WIDTH * sizeof (EFI_GRAPHICS_OUTPUT_BLT_PIXEL)
              );
  } else if (FeaturePcdGet (PcdUgaConsumeSupport)) {
    UgaDraw->Blt (
              UgaDraw,
              (EFI_UGA_PIXEL *) (UINTN) BltChar,
              EfiUgaVideoToBltBuffer,
              GlyphX,
              GlyphY,
              0,
              0,
              EFI_GLYPH_WIDTH,
              EFI_GLYPH_HEIGHT,
              EFI_GLYPH_WIDTH * sizeof (EFI_UGA_PIXEL)
              );
  }

  GetTextColors (This, &Foreground.Pixel, &Background.Pixel);

  //
  // Convert Monochrome bitmap of the Glyph to BltBuffer structure
  //
  for (PosY = 0; PosY < EFI_GLYPH_HEIGHT; PosY++) {
    for (PosX = 0; PosX < EFI_GLYPH_WIDTH; PosX++) {
      if ((mCursorGlyph.GlyphCol1[PosY] & (1 << PosX)) != 0) {
        BltChar[PosY][EFI_GLYPH_WIDTH - PosX - 1].Raw ^= Foreground.Raw;
      }
    }
  }

  if (GraphicsOutput != NULL) {
    GraphicsOutput->Blt (
              GraphicsOutput,
              (EFI_GRAPHICS_OUTPUT_BLT_PIXEL *) BltChar,
              EfiBltBufferToVideo,
              0,
              0,
              GlyphX,
              GlyphY,
              EFI_GLYPH_WIDTH,
              EFI_GLYPH_HEIGHT,
              EFI_GLYPH_WIDTH * sizeof (EFI_GRAPHICS_OUTPUT_BLT_PIXEL)
              );
  } else if (FeaturePcdGet (PcdUgaConsumeSupport)) {
    UgaDraw->Blt (
              UgaDraw,
              (EFI_UGA_PIXEL *) (UINTN) BltChar,
              EfiUgaBltBufferToVideo,
              0,
              0,
              GlyphX,
              GlyphY,
              EFI_GLYPH_WIDTH,
              EFI_GLYPH_HEIGHT,
              EFI_GLYPH_WIDTH * sizeof (EFI_UGA_PIXEL)
              );
  }

  return EFI_SUCCESS;
}

/**
  The user Entry Point for module GraphicsConsole. The user code starts with this function.

  @param[in] ImageHandle    The firmware allocated handle for the EFI image.
  @param[in] SystemTable    A pointer to the EFI System Table.

  @retval EFI_SUCCESS       The entry point is executed successfully.
  @retval other             Some error occurs when executing this entry point.

**/
EFI_STATUS
EFIAPI
InitializeGraphicsConsole (
  IN EFI_HANDLE           ImageHandle,
  IN EFI_SYSTEM_TABLE     *SystemTable
  )
{
  EFI_STATUS              Status;

  //
  // Install driver model protocol(s).
  //
  Status = EfiLibInstallDriverBindingComponentName2 (
             ImageHandle,
             SystemTable,
             &gGraphicsConsoleDriverBinding,
             ImageHandle,
             &gGraphicsConsoleComponentName,
             &gGraphicsConsoleComponentName2
             );
  ASSERT_EFI_ERROR (Status);


  return Status;
}


