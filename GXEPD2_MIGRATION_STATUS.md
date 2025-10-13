# GxEPD2 Migration Status for E1002

## Overview
This document tracks the migration from bb_epaper to GxEPD2 library for the reTerminal E1002 7-color display.

## ✅ Completed Changes

### 1. Dependencies (platformio.ini)
- ✅ Added `https://github.com/ZinggJM/GxEPD2.git`
- ✅ Added `adafruit/Adafruit GFX Library@^1.11.9`

### 2. Display Initialization (src/display.cpp)
- ✅ Lines 9-203: Added conditional compilation for E1002 vs other boards
- ✅ Created `GxEPD2_7C<GxEPD2_730c_GDEP073E01, GxEPD2_730c_GDEP073E01::HEIGHT> display` object for E1002
- ✅ Created `E1002DisplayWrapper` class to provide bb_epaper-compatible interface
- ✅ Added BB_RECT, constants (REFRESH_*, PLANE_*, ONE_BIT_PANEL, etc.)
- ✅ `display_init()` - SPI initialization and GxEPD2 setup
- ✅ `display_show_battery()` - GxEPD2 paged drawing
- ✅ `display_reset()` - GxEPD2 screen clear
- ✅ `display_height()` and `display_width()` - GxEPD2 dimensions
- ✅ `display_sleep()` - GxEPD2 hibernate
- ✅ `test_e1002_color_mapping()` - GxEPD2 color logging
- ✅ Fixed Paint_DrawMultilineText() signature to use `const void *font`
- ✅ Fixed imagePointer declaration in bl.cpp

### 3. Compatibility Wrapper (E1002DisplayWrapper)
- ✅ getCache(), writeData(), writeCmd()
- ✅ width(), height()
- ✅ setFont(), setTextColor(), setCursor()
- ✅ print(), println() (with String and const char* overloads)
- ✅ getStringBox() (with String and const char* overloads)
- ✅ allocBuffer(), freeBuffer(), getBuffer(), setBuffer()
- ✅ fillScreen(), loadG5Image(), loadBMP()
- ✅ writePlane(), refresh(), setAddrWindow(), startWrite(), setPanelType()
- ✅ fullUpdate()

## ⚠️ Known Issues / TODO

### Compilation Status
**Latest Status**: ✅ Code compiles successfully!
**Rendering Status**: ✅ Basic text rendering implemented with paged drawing!

### Implemented Rendering Features

The E1002DisplayWrapper now includes functional implementations:

1. **Text Rendering (✅ Implemented)**:
   - Buffered text commands for paged drawing
   - `print()` and `println()` store text for later rendering
   - `refresh()` executes all buffered drawing commands
   - Uses FreeMonoBold9pt7b font from Adafruit GFX
   - Supports color specification via `setTextColor()`

2. **Display Management (✅ Implemented)**:
   - `refresh()` - Full paged drawing with GxEPD2
   - `fillScreen()` - Works in both paged and buffered modes
   - `setFont()`, `setTextColor()`, `setCursor()` - Functional
   - Command buffering system for non-paged operations

### Remaining Work - Advanced Features

While basic rendering works, the following features still need implementation:

#### High Priority - Image Rendering:
1. **PNG Processing** (Partially Working):
   - `png_draw_into_4bpp()` - Implemented, needs testing with real hardware
   - `png_to_7color_epd()` - Implemented, needs testing
   - Color mapping from RGB to 7-color palette needs validation

2. **Compressed Image Formats** (Not Implemented):
   - `loadG5Image()` - Group5 compressed images (stub, logs warning)
   - `loadBMP()` - BMP image loading (stub, logs warning)
   - `draw_virtual_bmp_from_png()` - Virtual BMP for E1002 (calls stub)
   - **Recommendation**: These are lower priority, can be deferred

3. **Buffer Rendering** (Partial):
   - 1-bpp buffer to GxEPD2 conversion not yet implemented
   - Currently buffers are allocated but not rendered in `refresh()`

#### Medium Priority - Text & Font Improvements:
4. **Font Handling**:
   - ✅ Basic font support working (FreeMonoBold9pt7b)
   - ⚠️ bb_epaper fonts (nicoclean_8) mapped to nullptr - uses GxEPD2 font instead
   - ⚠️ `getStringBox()` uses approximate measurements (6px wide, 8px tall)
   - TODO: Improve font metrics for accurate text layout

5. **Paint_DrawMultilineText()**:
   - ✅ Basic implementation works with wrapper methods
   - TODO: Test with E1002-specific code paths

## 🔧 Recommended Next Steps

### Option 1: Minimal Working Solution (Recommended First)
1. Comment out complex PNG/BMP functions for E1002
2. Implement basic text-only message display with GxEPD2
3. Test hardware initialization and simple drawing
4. Gradually add back image support

### Option 2: Full PNG Implementation
1. Study GxEPD2's color handling in the Seeed example
2. Implement RGB to nearest color mapping
3. Use GxEPD2's `writeImage()` or `drawBitmap()` methods
4. Test with actual PNG files

### Option 3: Hybrid Approach
1. Keep simple black & white images working
2. Add color support incrementally
3. Fall back to grayscale for complex images

## 📝 GxEPD2 API Key Differences

### bb_epaper → GxEPD2 Equivalents:
- `bbep.initIO()` → `display.init()`
- `bbep.allocBuffer()` → Not needed (GxEPD2 manages internally)
- `bbep.fillScreen()` → `display.fillScreen()`
- `bbep.setFont()` → `display.setFont()` (Adafruit GFX fonts)
- `bbep.setTextColor()` → `display.setTextColor()`
- `bbep.setCursor()` → `display.setCursor()`
- `bbep.print()` → `display.print()`
- `bbep.writePlane()` → Not needed
- `bbep.refresh()` → `display.nextPage()` (in paged mode)
- `bbep.sleep()` → `display.hibernate()`
- `bbep.width()/height()` → `display.width()/height()`

### GxEPD2 Drawing Pattern:
```cpp
display.setFullWindow();
display.firstPage();
do {
    // All drawing commands here
    display.fillScreen(GxEPD_WHITE);
    display.setTextColor(GxEPD_BLACK);
    display.print("Text");
} while (display.nextPage());
```

## 🎨 Color Support

### GxEPD2 E1002 Colors:
- `GxEPD_BLACK` (0x0000)
- `GxEPD_WHITE` (0xFFFF)
- `GxEPD_RED`
- `GxEPD_YELLOW`
- `GxEPD_BLUE`
- `GxEPD_GREEN`

Note: The Seeed wiki mentions 6 colors (not 7). Orange support needs verification.

## 🧪 Testing Plan

1. **Hardware Test**: Verify display initializes without errors
2. **Basic Drawing**: Test simple shapes and text
3. **Color Test**: Draw colored rectangles to verify color rendering
4. **Image Test**: Load and display a test PNG
5. **Integration Test**: Test with full TRMNL workflow

## 📚 References

- Seeed E1002 Arduino Example: https://wiki.seeedstudio.com/reterminal_e10xx_with_arduino/
- GxEPD2 GitHub: https://github.com/ZinggJM/GxEPD2
- GxEPD2 Examples: Check library examples for 7-color displays
