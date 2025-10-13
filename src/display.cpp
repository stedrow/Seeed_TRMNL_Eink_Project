#include <Arduino.h>
#include <display.h>
#include <PNGdec.h>
#include <SPIFFS.h>
#include <Preferences.h>
#include <preferences_persistence.h>
#include "DEV_Config.h"
#include <vector>
#include "Group5.h"
#include <trmnl_log.h>

#if defined(BOARD_SEEED_RETERMINAL_E1002)
// GxEPD2 library for E1002 7-color display
#include <GxEPD2_7C.h>
#include <Fonts/FreeMonoBold9pt7b.h>

// E1002 display: 7.3" ACeP (7-color) e-Paper
// Display dimensions: 800x480
// Use GxEPD2_7C wrapper for full paged drawing support
GxEPD2_7C<GxEPD2_730c_GDEP073E01, GxEPD2_730c_GDEP073E01::HEIGHT> display(GxEPD2_730c_GDEP073E01(EPD_CS_PIN, EPD_DC_PIN, EPD_RST_PIN, EPD_BUSY_PIN));

// Forward declarations and global variables for PNG rendering
int png_draw_to_gxepd2(PNGDRAW *pDraw);
int png_draw_to_buffer(PNGDRAW *pDraw);
static int g_png_offset_x = 0;
static int g_png_offset_y = 0;
static int g_png_scale = 1;
static bool g_png_rendering = false;
static uint16_t *g_image_buffer = nullptr;
static int g_buffer_width = 0;
static int g_buffer_height = 0;

// Counts the number of partial updates to know when to do a full update
RTC_DATA_ATTR int iUpdateCount = 0;

// E1002-specific configuration constants
#define E1002_COLOR_CACHE_SIZE 512
#define E1002_MAX_COLORS 7
#define E1002_COLOR_MAP_SIZE 7

// Define constants for compatibility
#define REFRESH_FULL 0
#define REFRESH_PARTIAL 1
#define REFRESH_FAST 2

// Panel type constants for compatibility
#define ONE_BIT_PANEL 0
#define TWO_BIT_PANEL 1

// Plane constants for compatibility
#define PLANE_0 0
#define PLANE_1 1

// Sleep mode constants
#define DEEP_SLEEP 1

// BB_RECT structure definition (must be before E1002DisplayWrapper)
typedef struct {
    int16_t x, y;
    uint16_t w, h;
} BB_RECT;

// Helper class to provide bbep-like interface for E1002
class E1002DisplayWrapper {
private:
    static uint8_t* buffer;
    static bool bufferAllocated;
    static const void* currentFont;
    static uint16_t textColor;
    static uint16_t bgColor;
    static int16_t cursorX;
    static int16_t cursorY;
    static bool inPagedMode;

    // PNG image data storage (simplified for direct rendering)
    static bool hasImageData;

    // Structure to store drawing commands for paged rendering
    struct DrawCommand {
        enum Type { TEXT, RECT, LINE } type;
        String text;
        int16_t x, y;
        uint16_t color;
    };
    static std::vector<DrawCommand> drawCommands;

public:
    static uint8_t* getCache() {
        static uint8_t cache[E1002_COLOR_CACHE_SIZE];
        return cache;
    }

    static void writeData(uint8_t* data, size_t len) {
        // Not used in direct rendering approach
        (void)data; (void)len; // Suppress unused warnings
    }

    static void writeCmd(uint8_t cmd) {
        // Not used in direct rendering approach
        (void)cmd; // Suppress unused warning
    }

    static uint16_t width() { return display.width(); }
    static uint16_t height() { return display.height(); }

    static void setFont(const void* font) {
        currentFont = font;
        // For E1002, we'll use FreeMonoBold9pt7b from GxEPD2
        // bb_epaper fonts are ignored
        if (inPagedMode) {
            display.setFont(&FreeMonoBold9pt7b);
        }
    }

    static void setTextColor(uint16_t fg, uint16_t bg) {
        textColor = fg;
        bgColor = bg;
        if (inPagedMode) {
            display.setTextColor(fg);
        }
    }

    static void setCursor(int16_t x, int16_t y) {
        if (y == -1) {
            // bb_epaper uses -1 to mean "next line"
            cursorY += 20; // Approximate line height
        } else {
            cursorY = y;
        }
        if (x != -1) {
            cursorX = x;
        }
        if (inPagedMode) {
            display.setCursor(cursorX, cursorY);
        }
    }

    static void print(const char* str) {
        if (inPagedMode) {
            display.print(str);
        } else {
            // Store for later rendering
            DrawCommand cmd;
            cmd.type = DrawCommand::TEXT;
            cmd.text = String(str);
            cmd.x = cursorX;
            cmd.y = cursorY;
            cmd.color = textColor;
            drawCommands.push_back(cmd);
        }
    }

    static void print(String str) {
        print(str.c_str());
    }

    static void println(const char* str) {
        print(str);
        cursorY += 20; // Move to next line
    }

    static void println(String str) {
        println(str.c_str());
    }

    static void getStringBox(const char* str, BB_RECT* rect) {
        // For bb_epaper fonts, use approximate measurements
        // For GxEPD2, we'd need to measure with actual font
        rect->x = cursorX;
        rect->y = cursorY;
        // Approximate: nicoclean_8 is about 6 pixels wide, 8 pixels tall
        rect->w = strlen(str) * 6;
        rect->h = 8;
    }

    static void getStringBox(String str, BB_RECT* rect) {
        getStringBox(str.c_str(), rect);
    }

    static void allocBuffer(bool twoPlanes) {
        // GxEPD2 manages its own buffer internally
        bufferAllocated = true;
    }

    static void freeBuffer() {
        bufferAllocated = false;
    }

    static uint8_t* getBuffer() {
        // For compatibility with code that expects direct buffer access
        if (!buffer) {
            size_t bufferSize = (display.width() / 8) * display.height();
            buffer = (uint8_t*)malloc(bufferSize);
            if (buffer) {
                memset(buffer, 0xFF, bufferSize); // Initialize to white
            }
        }
        return buffer;
    }

    static void setBuffer(uint8_t* buf) {
        buffer = buf;
    }

    static void fillScreen(uint16_t color) {
        if (inPagedMode) {
            display.fillScreen(color);
        }
        // Note: When not in paged mode, just store the state for later
    }

    static void loadG5Image(const uint8_t* data, int x, int y, uint16_t fg, uint16_t bg) {
        // Group5 compressed images - need to decompress and render
        // TODO: Implement G5 decompression and rendering with GxEPD2
        (void)data; (void)x; (void)y; (void)fg; (void)bg; // Suppress unused warnings
    }

    static int loadBMP(const uint8_t* data, int x, int y, uint16_t fg, uint16_t bg) {
        // BMP loading for E1002 - needs implementation
        // TODO: Implement BMP loading with GxEPD2
        (void)data; (void)x; (void)y; (void)fg; (void)bg; // Suppress unused warnings
        return 0;
    }

    static void writePlane(int plane) {
        // E1002 doesn't use planes the same way as bb_epaper
        // This is a no-op for GxEPD2
        (void)plane; // Suppress unused warning
    }

    static void refresh(int mode, bool wait) {
        // This is the key method - triggers actual display update
        (void)mode; (void)wait; // Will use these when we add mode-specific handling

        // Start paged drawing mode
        display.setFullWindow();
        display.firstPage();
        inPagedMode = true;

        do {
            // Clear screen
            display.fillScreen(GxEPD_WHITE);

            // Execute any buffered drawing commands (text overlays)
            display.setFont(&FreeMonoBold9pt7b);
            for (const auto& cmd : drawCommands) {
                if (cmd.type == DrawCommand::TEXT) {
                    display.setTextColor(cmd.color);
                    display.setCursor(cmd.x, cmd.y);
                    display.print(cmd.text);
                }
            }

        } while (display.nextPage());

        inPagedMode = false;

        // Clear draw commands after rendering
        drawCommands.clear();
        hasImageData = false;
    }

    static void setAddrWindow(int x, int y, int w, int h) {
        // Memory window setup - not needed for GxEPD2 paged mode
        (void)x; (void)y; (void)w; (void)h; // Suppress unused warnings
    }

    static void startWrite(int plane) {
        // Start writing to a plane - not used in GxEPD2
        (void)plane; // Suppress unused warning
    }

    static void setPanelType(int type) {
        // E1002 is always 7-color, no panel type switching
        (void)type; // Suppress unused warning
    }

    static void fullUpdate() {
        // Same as refresh with full mode
        refresh(REFRESH_FULL, true);
    }
};

// Initialize static members
uint8_t* E1002DisplayWrapper::buffer = nullptr;
bool E1002DisplayWrapper::bufferAllocated = false;
const void* E1002DisplayWrapper::currentFont = nullptr;
uint16_t E1002DisplayWrapper::textColor = GxEPD_BLACK;
uint16_t E1002DisplayWrapper::bgColor = GxEPD_WHITE;
int16_t E1002DisplayWrapper::cursorX = 0;
int16_t E1002DisplayWrapper::cursorY = 0;
bool E1002DisplayWrapper::inPagedMode = false;
std::vector<E1002DisplayWrapper::DrawCommand> E1002DisplayWrapper::drawCommands;

// PNG image data storage static members (simplified)
bool E1002DisplayWrapper::hasImageData = false;

// Create a bbep-like object for E1002 code compatibility
static E1002DisplayWrapper bbep;

// Color constants for compatibility
#define BBEP_BLACK GxEPD_BLACK
#define BBEP_WHITE GxEPD_WHITE

// Font compatibility for E1002
// Since E1002 uses Adafruit GFX fonts, we need a placeholder for bb_epaper font references
#define nicoclean_8 nullptr  // Will be replaced with GxEPD2 font in actual rendering

#else
// Original bb_epaper implementation for non-E1002 boards
#define BB_EPAPER
#ifdef BB_EPAPER
#include "bb_epaper.h"
#define ONE_BIT_PANEL EP75_800x480
#define TWO_BIT_PANEL EP75_800x480_4GRAY_OLD
BBEPAPER bbep(ONE_BIT_PANEL);
RTC_DATA_ATTR int iUpdateCount = 0;
#else
#include "FastEPD.h"
FASTEPD bbep;
#endif
#include "../lib/bb_epaper/Fonts/Roboto_20.h"
#include "../lib/bb_epaper/Fonts/nicoclean_8.h"
#endif

#include "Group5.h"
#include <config.h>
#include "wifi_connect_qr.h"
#include "wifi_failed_qr.h"
#include <ctype.h> //iscntrl()
#include <api-client/display.h>
#include "png_flip.h"

extern char filename[];
extern Preferences preferences;
extern ApiDisplayResult apiDisplayResult;

/**
 * @brief Function to init the display
 * @param none
 * @return none
 */
void display_init(void)
{
    Log_info("dev module start");
#if defined(BOARD_SEEED_RETERMINAL_E1002)
    // Initialize GxEPD2 for E1002
    // Configure SPI pins
    SPI.begin(EPD_SCK_PIN, -1, EPD_MOSI_PIN, EPD_CS_PIN);

    // Initialize the display
    display.init(115200, true, 2, false); // 115200 baud, initial refresh, reset duration 2ms, no pulldown

    Log_info("E1002: GxEPD2 initialized - 800x480 7-color display");

    // Test E1002 color capabilities on initialization (disabled for production)
    // test_e1002_color_mapping();
#else
    // Original bb_epaper initialization for non-E1002 boards
    #ifdef BB_EPAPER
    bbep.initIO(EPD_DC_PIN, EPD_RST_PIN, EPD_BUSY_PIN, EPD_CS_PIN, EPD_MOSI_PIN, EPD_SCK_PIN, 8000000);
    #else
    bbep.initPanel(BB_PANEL_EPDIY_V7);
    bbep.setPanelSize(1448, 1072);
    #endif
#endif

    Log_info("dev module end");
}

void display_show_battery(float vBatt)
{
char szTemp[32];

#if defined(BOARD_SEEED_RETERMINAL_E1002)
    display.setFullWindow();
    display.firstPage();
    do {
        display.fillScreen(GxEPD_WHITE);
        display.setFont(&FreeMonoBold9pt7b);
        display.setTextColor(GxEPD_BLACK);
        display.setCursor(0, 100);
        sprintf(szTemp, "VBatt = %f", vBatt);
        display.print(szTemp);
    } while (display.nextPage());
    display.hibernate();
#else
    bbep.allocBuffer(false);
    bbep.fillScreen(BBEP_WHITE);
    bbep.setFont(nicoclean_8);
    bbep.setTextColor(BBEP_BLACK, BBEP_WHITE);
    bbep.setCursor(0, 100);
    sprintf(szTemp, "VBatt = %f", vBatt);
    bbep.print(szTemp);
    bbep.writePlane();
    bbep.refresh(REFRESH_FULL, true);
    bbep.sleep(DEEP_SLEEP);
#endif
    while (1) {
        vTaskDelay(1);
    }
} /* display_show_battery() */

/**
 * @brief Function to sleep the ESP32 while saving power
 * @param u32Millis represents the sleep time in milliseconds
 * @return none
 */
void display_sleep(uint32_t u32Millis)
{
  esp_sleep_enable_timer_wakeup(u32Millis * 1000L);
  esp_light_sleep_start();
}

/**
 * @brief Function to reset the display
 * @param none
 * @return none
 */
void display_reset(void)
{
    Log_info("e-Paper Clear start");
#if defined(BOARD_SEEED_RETERMINAL_E1002)
    display.setFullWindow();
    display.firstPage();
    do {
        display.fillScreen(GxEPD_WHITE);
    } while (display.nextPage());
#else
    bbep.fillScreen(BBEP_WHITE);
    #ifdef BB_EPAPER
    if (!apiDisplayResult.response.maximum_compatibility) {
        bbep.refresh(REFRESH_FAST, true);
    } else {
        bbep.refresh(REFRESH_FULL, true); // incompatible panel
    }
    #else
    bbep.fullUpdate();
    #endif
#endif
    Log_info("e-Paper Clear end");
}

/**
 * @brief Function to read the display height
 * @return uint16_t - height of display in pixels
 */
uint16_t display_height()
{
#if defined(BOARD_SEEED_RETERMINAL_E1002)
    return display.height();
#else
    return bbep.height();
#endif
}

/**
 * @brief Function to read the display width
 * @return uint16_t - width of display in pixels
 */
uint16_t display_width()
{
#if defined(BOARD_SEEED_RETERMINAL_E1002)
    return display.width();
#else
    return bbep.width();
#endif
}

/**
 * @brief Function to draw multi-line text onto the display
 * @param x_start X coordinate to start drawing
 * @param y_start Y coordinate to start drawing
 * @param message Text message to draw
 * @param max_width Maximum width in pixels for each line
 * @param font_width Width of a single character in pixels
 * @param color_fg Foreground color
 * @param color_bg Background color
 * @param font Font to use
 * @param is_center_aligned If true, center the text; if false, left-align
 * @return none
 */
void Paint_DrawMultilineText(UWORD x_start, UWORD y_start, const char *message,
                             uint16_t max_width, uint16_t font_width,
                             UWORD color_fg, UWORD color_bg, const void *font,
                             bool is_center_aligned)
{
    BB_FONT_SMALL *pFont = (BB_FONT_SMALL *)font;
    uint16_t display_width_pixels = max_width;
    int max_chars_per_line = display_width_pixels / font_width;
    const int font_height = pFont->height;
    uint8_t MAX_LINES = 4;

    char lines[MAX_LINES][max_chars_per_line + 1] = {0};
    uint16_t line_count = 0;

    int text_len = strlen(message);
    int current_width = 0;
    int line_index = 0;
    int line_pos = 0;
    int word_start = 0;
    int i = 0;
    char word_buffer[max_chars_per_line + 1] = {0};
    int word_length = 0;

    bbep.setFont(font);
    bbep.setTextColor(color_fg, color_bg);

    bbep.setFont(font);
    bbep.setTextColor(color_fg, color_bg);

    while (i <= text_len && line_index < MAX_LINES)
    {
        word_length = 0;
        word_start = i;

        // Skip leading spaces
        while (i < text_len && message[i] == ' ')
        {
            i++;
        }
        word_start = i;

        // Find end of word or end of text
        while (i < text_len && message[i] != ' ')
        {
            i++;
        }

        word_length = i - word_start;
        if (word_length > max_chars_per_line)
        {
            word_length = max_chars_per_line; // Truncate if word is too long
        }

        if (word_length > 0)
        {
            strncpy(word_buffer, message + word_start, word_length);
            word_buffer[word_length] = '\0';
        }
        else
        {
            i++;
            continue;
        }

        int word_width = word_length * font_width;

        // Check if adding the word exceeds max_width
        if (current_width + word_width + (current_width > 0 ? font_width : 0) <= display_width_pixels)
        {
            // Add space before word if not the first word in the line
            if (current_width > 0 && line_pos < max_chars_per_line - 1)
            {
                lines[line_index][line_pos++] = ' ';
                current_width += font_width;
            }

            // Add word to current line
            if (line_pos + word_length <= max_chars_per_line)
            {
                strcpy(&lines[line_index][line_pos], word_buffer);
                line_pos += word_length;
                current_width += word_width;
            }
        }
        else
        {
            // Current line is full, draw it
            if (line_pos > 0)
            {
                lines[line_index][line_pos] = '\0'; // Null-terminate the current line
                line_index++;
                line_count++;

                if (line_index >= MAX_LINES)
                {
                    break;
                }

                // Start new line with this word
                strncpy(lines[line_index], word_buffer, word_length);
                line_pos = word_length;
                current_width = word_width;
            }
            else
            {
                // Single long word case
                strncpy(lines[line_index], word_buffer, max_chars_per_line);
                lines[line_index][max_chars_per_line] = '\0';
                line_index++;
                line_count++;
                line_pos = 0;
                current_width = 0;
            }
        }

        // Move to next word
        if (message[i] == ' ')
        {
            i++;
        }
    }

    // Store the last line if any
    if (line_pos > 0 && line_index < MAX_LINES)
    {
        lines[line_index][line_pos] = '\0';
        line_count++;
    }

    // Draw the lines
    for (int j = 0; j < line_count; j++)
    {
        uint16_t line_width = strlen(lines[j]) * font_width;
        uint16_t draw_x = x_start;

        if (is_center_aligned)
        {
            if (line_width < max_width)
            {
                draw_x = x_start + (max_width - line_width) / 2;
            }
        }
        bbep.setCursor(draw_x, y_start + j * (font_height + 5));
        bbep.print(lines[j]);
    }
}
/** 
 * @brief Reduce the bit depth of line of pixels using thresholding (aka simple color mapping)
 * @param Destination bit count (1 or 2)
 * @param Pointer to a PNG palette (3 bytes per entry)
 * @param Pointer to the source pixels
 * @param Pointer to the destination pixels
 * @param Pixel count
 * @param Original bit depth
 * @return none
 */
void ReduceBpp(int iDestBpp, int iPixelType, uint8_t *pPalette, uint8_t *pSrc, uint8_t *pDest, int w, int iSrcBpp)
{
    int g = 0, x, iDelta;
    uint8_t *s, *d, *pPal, u8, count;
    const uint8_t u8G2ToG8[4] = {0x00, 0x55, 0xaa, 0xff}; // 2-bit to 8-bit gray

    if (iPixelType == PNG_PIXEL_TRUECOLOR) iSrcBpp = 24;
    else if (iPixelType == PNG_PIXEL_TRUECOLOR_ALPHA) iSrcBpp = 32;
    iDelta = iSrcBpp/8; // bytes per pixel
    count = 8; // bits in a byte
    u8 = 0; // start with all black
    d = pDest;
    s = pSrc;
    for (x=0; x<w; x++) {
        u8 <<= iDestBpp;
        switch (iSrcBpp) {
            case 24:
            case 32:
                g = (s[0] + s[1]*2 + s[2])/4; // convert color to gray value
                s += iDelta;
                break;
            case 8:
                if (iPixelType == PNG_PIXEL_INDEXED) {
                    pPal = &pPalette[s[0] * 3];
                    g = (pPal[0] + pPal[1]*2 + pPal[2])/4;
                } else { // must be grayscale
                    g = s[0];
                }
                s++;
                break;
            case 4:
                if (x & 1) {
                    if (iPixelType == PNG_PIXEL_INDEXED) {
                        pPal = &pPalette[(s[0] & 0xf) * 3];
                        g = (pPal[0] + pPal[1]*2 + pPal[2])/4;
                    } else {
                        g = (s[0] & 0xf) | (s[0] << 4);
                    }
                    s++;
                } else {
                    if (iPixelType == PNG_PIXEL_INDEXED) {
                        pPal = &pPalette[(s[0]>>4) * 3];
                        g = (pPal[0] + pPal[1]*2 + pPal[2])/4;
                    } else {
                        g = (s[0] & 0xf0) | (s[0] >> 4);
                    }
                }
                break;
            case 2: // We need to handle this case for 2-bit images with (random) palettes
                g = s[0] >> (6-((x & 3) * 2));
                if (iPixelType == PNG_PIXEL_INDEXED) {
                    pPal = &pPalette[(g & 3)*3];
                    g = (pPal[0] + pPal[1]*2 + pPal[2])/4;
                } else {
                    g = u8G2ToG8[g & 3];
                }
                if ((x & 3) == 3) {
                    s++;
                }
                break;
        } // switch on bpp
        if (iDestBpp == 1) {
            u8 |= (g >> 7); // B/W
        } else { // generate 4 gray levels (2 bits)
            u8 |= (3 ^ (g >> 6)); // 4 gray levels (inverted relative to 1-bit)
        }
        count -= iDestBpp;        
        if (count == 0) { // byte is full, move on
            *d++ = u8;
            u8 = 0;
            count = 8;
        }
    } // for x
    if (count != 8) { // partial byte remaining
        u8 <<= count;
        *d++ = u8;
    }
} /* ReduceBpp() */
/** 
 * @brief Callback function for each line of PNG decoded
 * @param PNGDRAW structure containing the current line and relevant info
 * @return none
 */
int png_draw(PNGDRAW *pDraw)
{
    int x;
    uint8_t ucBppChanged = 0, ucInvert = 0;
    uint8_t uc, ucMask, src, *s, *d, *pTemp = bbep.getCache(); // get some scratch memory (not from the stack)

    if (pDraw->iPixelType == PNG_PIXEL_INDEXED || pDraw->iBpp > 2) {
        if (pDraw->iBpp == 1) { // 1-bit output, just see which color is brighter
            uint32_t u32Gray0, u32Gray1;
            u32Gray0 = pDraw->pPalette[0] + (pDraw->pPalette[1]<<2) + pDraw->pPalette[2];
            u32Gray1 = pDraw->pPalette[3] + (pDraw->pPalette[4]<<2) + pDraw->pPalette[5];
          if (u32Gray0 < u32Gray1) {
            ucInvert = 0xff;
          }
        } else {
            // Reduce the source image to 1-bpp or 2-bpp
            ReduceBpp((pDraw->pUser) ? 2:1, pDraw->iPixelType, pDraw->pPalette, pDraw->pPixels, pTemp, pDraw->iWidth, pDraw->iBpp);
            ucBppChanged = 1;
        }
    } else if (pDraw->iBpp == 2) {
        ucInvert = 0xff; // 2-bit non-palette images need to be inverted colors for 4-gray mode
    }
    s = (ucBppChanged) ? pTemp : (uint8_t *)pDraw->pPixels;
    d = pTemp;
    if (!pDraw->pUser) {
        // 1-bit output, decode the single plane and write it
        for (x=0; x<pDraw->iWidth; x+= 8) {
          d[0] = s[0] ^ ucInvert;
          d++; s++;
        }
    } else { // we need to split the 2-bit data into plane 0 and 1
        src = *s++;
        src ^= ucInvert;
        uc = 0; // suppress warning/error
        if (*(int *)pDraw->pUser > 1) { // draw 2bpp data as 1-bit to use for partial update
            ucInvert = ~ucInvert; // the invert rule is backwards for grayscale data
            src = ~src;
            for (x=0; x<pDraw->iWidth; x++) {
                uc <<= 1;
                if (src & 0xc0) { // non-white -> black
                    uc |= 1; // high bit of source pair
                }
                src <<= 2;
                if ((x & 3) == 3) { // new input byte
                    src = *s++;
                    src ^= ucInvert;
                }
                if ((x & 7) == 7) { // new output byte
                    *d++ = uc;
                }
            } // for x
        } else { // normal 0/1 split plane
            ucMask = (*(int *)pDraw->pUser == 0) ? 0x40 : 0x80; // lower or upper source bit
            for (x=0; x<pDraw->iWidth; x++) {
                uc <<= 1;
                if (src & ucMask) {
                    uc |= 1; // high bit of source pair
                }
                src <<= 2;
                if ((x & 3) == 3) { // new input byte
                    src = *s++;
                    src ^= ucInvert;
                }
                if ((x & 7) == 7) { // new output byte
                    *d++ = uc;
                }
            } // for x
        }
    }
    bbep.writeData(pTemp, (pDraw->iWidth+7)/8);
    return 1;
} /* png_draw() */

//
// A table to accelerate the testing of 2-bit images for the number
// of unique colors. Each entry sets bits 0-3 depending on the presence
// of colors 0-3 in each 2-bit pixel
//
const uint8_t ucTwoBitFlags[256] = {
0x01,0x03,0x05,0x09,0x03,0x03,0x07,0x0b,0x05,0x07,0x05,0x0d,0x09,0x0b,0x0d,0x09,
0x03,0x03,0x07,0x0b,0x03,0x03,0x07,0x0b,0x07,0x07,0x07,0x0f,0x0b,0x0b,0x0f,0x0b,
0x05,0x07,0x05,0x0d,0x07,0x07,0x07,0x0f,0x05,0x07,0x05,0x0d,0x0d,0x0f,0x0d,0x0d,
0x09,0x0b,0x0d,0x09,0x0b,0x0b,0x0f,0x0b,0x0d,0x0f,0x0d,0x0d,0x09,0x0b,0x0d,0x09,
0x03,0x03,0x07,0x0b,0x03,0x03,0x07,0x0b,0x07,0x07,0x07,0x0f,0x0b,0x0b,0x0f,0x0b,
0x03,0x03,0x07,0x0b,0x03,0x02,0x06,0x0a,0x07,0x06,0x06,0x0e,0x0b,0x0a,0x0e,0x0a,
0x07,0x07,0x07,0x0f,0x07,0x06,0x06,0x0e,0x07,0x06,0x06,0x0e,0x0f,0x0e,0x0e,0x0e,
0x0b,0x0b,0x0f,0x0b,0x0b,0x0a,0x0e,0x0a,0x0f,0x0e,0x0e,0x0e,0x0b,0x0a,0x0e,0x0a,
0x05,0x07,0x05,0x0d,0x07,0x07,0x07,0x0f,0x05,0x07,0x05,0x0d,0x0d,0x0f,0x0d,0x0d,
0x07,0x07,0x07,0x0f,0x07,0x06,0x06,0x0e,0x07,0x06,0x06,0x0e,0x0f,0x0e,0x0e,0x0e,
0x05,0x07,0x05,0x0d,0x07,0x06,0x06,0x0e,0x05,0x06,0x04,0x0c,0x0d,0x0e,0x0c,0x0c,
0x0d,0x0f,0x0d,0x0d,0x0f,0x0e,0x0e,0x0e,0x0d,0x0e,0x0c,0x0c,0x0d,0x0e,0x0c,0x0c,
0x09,0x0b,0x0d,0x09,0x0b,0x0b,0x0f,0x0b,0x0d,0x0f,0x0d,0x0d,0x09,0x0b,0x0d,0x09,
0x0b,0x0b,0x0f,0x0b,0x0b,0x0a,0x0e,0x0a,0x0f,0x0e,0x0e,0x0e,0x0b,0x0a,0x0e,0x0a,
0x0d,0x0f,0x0d,0x0d,0x0f,0x0e,0x0e,0x0e,0x0d,0x0e,0x0c,0x0c,0x0d,0x0e,0x0c,0x0c,
0x09,0x0b,0x0d,0x09,0x0b,0x0a,0x0e,0x0a,0x0d,0x0e,0x0c,0x0c,0x09,0x0a,0x0c,0x08
};

int png_draw_count(PNGDRAW *pDraw)
{
    int x, *pFlags = (int *)pDraw->pUser;
    uint8_t *s, set_bits;

    if (pDraw->y > 430) return 0; // Workaround to ignore the icon in the lower left corner

    set_bits = pFlags[0]; // use a local var
    s = (uint8_t *)pDraw->pPixels;
    for (x=0; x<pDraw->iWidth; x+=4) {
        set_bits |= ucTwoBitFlags[*s++]; // do 4 pixels at a time
    } // for x
    pFlags[0] = set_bits; // put it back in the flags array
    return 1;
} /* png_draw_count() */
/** 
 * @brief Function to decode a PNG and count the number of unique colors
 *        This is needed because 2-bit (4gray) images can sometimes contain
 *        only 2 unique colors. This will allow us to use partial (non-flickering)
 *        updates on these images.
 * @param pointer to the PNG class instance
 * @param pointer to the buffer holding the PNG file
 * @param size of the PNG file
 * @return the number of unique colors in the image (2 to 4)
 */
int png_count_colors(PNG *png, const uint8_t *pData, int iDataSize)
{
int i, iColors;
    png->openRAM((uint8_t *)pData, iDataSize, png_draw_count);
    i = 0;
    png->decode(&i, 0);
    png->close();
    iColors = 0;
    if (i & 1) iColors++;
    if (i & 2) iColors++;
    if (i & 4) iColors++;
    if (i & 8) iColors++;
    Log_info("%s [%d]: png_count_colors: %d\r\n", __FILE__, __LINE__, iColors);
    return iColors;
} /* png_count_colors() */
/** 
 * @brief Function to decode and display a PNG image from memory
 *        The decoded lines are written directly into the EPD framebuffer
 *        due to insufficient RAM to hold the fully decoded image
 * @param pointer to the buffer holding the PNG file
 * @param size of the PNG file
 * @return refresh mode based on image type and presence of old image
 */

int png_to_epd(const uint8_t *pPNG, int iDataSize)
{
int iPlane, rc = -1;
PNG *png = new PNG();

    if (!png) return PNG_MEM_ERROR; // not enough memory for the decoder instance
    rc = png->openRAM((uint8_t *)pPNG, iDataSize, png_draw);
    png->close();
    if (rc == PNG_SUCCESS) {
        if (png->getWidth() != bbep.width() || png->getHeight() != bbep.height()) {
            Log_error("PNG image size doesn't match display size");
            rc = -1;
        } else { // okay to decode
            Log_info("%s [%d]: Decoding %d-bpp png (current)\r\n", __FILE__, __LINE__, png->getBpp());
            // Prepare target memory window (entire display)
            bbep.setAddrWindow(0, 0, bbep.width(), bbep.height());
            if (png->getBpp() == 1 || (png->getBpp() == 2 && png_count_colors(png, pPNG, iDataSize) == 2)) { // 1-bit image (single plane)
                bbep.setPanelType(ONE_BIT_PANEL);
                rc = REFRESH_PARTIAL; // the new image is 1bpp - try a partial update
                bbep.startWrite(PLANE_0); // start writing image data to plane 0
                png->openRAM((uint8_t *)pPNG, iDataSize, png_draw);
                if (png->getBpp() == 1 || png->getBpp() > 2) {
                    png->decode(NULL, 0);
                } else { // convert the 2-bit image to 1-bit output
                    Log_info("%s [%d]: Current png only has 2 unique colors!\n", __FILE__, __LINE__);
                    iPlane = 2;
                    if (png->decode(&iPlane, 0) != PNG_SUCCESS) {
                        Log_info("%s [%d]: Error decoding image = %d\n", __FILE__, __LINE__, png->getLastError());
                    }
                }
                png->close();
            } else { // 2-bpp
                bbep.setPanelType(TWO_BIT_PANEL);
                rc = REFRESH_FULL; // 4gray mode must be full refresh
                iUpdateCount = 0; // grayscale mode resets the partial update counter
                bbep.startWrite(PLANE_0); // start writing image data to plane 0
                iPlane = 0;
                Log_info("%s [%d]: decoding 4-gray plane 0\r\n", __FILE__, __LINE__);
                png->openRAM((uint8_t *)pPNG, iDataSize, png_draw);
                png->decode(&iPlane, 0); // tell PNGDraw to use bits for plane 0
                png->close(); // start over for plane 1
                iPlane = 1;
                Log_info("%s [%d]: decoding 4-gray plane 1\r\n", __FILE__, __LINE__);
                png->openRAM((uint8_t *)pPNG, iDataSize, png_draw);
                bbep.startWrite(PLANE_1); // start writing image data to plane 1
                png->decode(&iPlane, 0); // decode it again to get plane 1 data
            }
        }
    }
    free(png); // free the decoder instance
    return rc;
} /* png_to_epd() */

// Removed png_draw_into_4bpp - using GxEPD2 approach instead

int png_to_7color_epd(const uint8_t *pPNG, int iDataSize)
{
    int rc = -1;
    PNG *png = new PNG();

    if (!png) {
        Log_error("E1002: Failed to allocate PNG decoder instance");
        return PNG_MEM_ERROR;
    }
    
    rc = png->openRAM((uint8_t *)pPNG, iDataSize, png_draw_to_buffer);
    if (rc == PNG_SUCCESS) {
        Log_info("E1002: Decoding %d-bpp PNG (%dx%d) for 6-color display", 
                png->getBpp(), png->getWidth(), png->getHeight());
        
        int imgWidth = png->getWidth();
        int imgHeight = png->getHeight();
        
        // Center the image on the display
        int offsetX = (display.width() - imgWidth) / 2;
        int offsetY = (display.height() - imgHeight) / 2;
        
        // Ensure image fits on display
        if (offsetX < 0) offsetX = 0;
        if (offsetY < 0) offsetY = 0;
        
        Log_info("E1002: Rendering TRMNL PNG %dx%d at offset (%d,%d)", 
                imgWidth, imgHeight, offsetX, offsetY);
        
        // Set up global rendering parameters
        g_png_offset_x = offsetX;
        g_png_offset_y = offsetY;
        g_png_scale = 1;
        g_png_rendering = true;
        
        // Decode PNG to framebuffer first (only once, outside paged loop)
        uint16_t *imageBuffer = (uint16_t*)malloc(imgWidth * imgHeight * sizeof(uint16_t));
        if (!imageBuffer) {
            Log_error("E1002: Failed to allocate image buffer");
            png->close();
            free(png);
            return -1;
        }
        
        // Set up global variables for buffer decoding
        g_image_buffer = imageBuffer;
        g_buffer_width = imgWidth;
        g_buffer_height = imgHeight;
        g_png_rendering = true;
        
        // Decode PNG to framebuffer (this happens once, outside paged loop)
        Log_info("E1002: Decoding PNG to framebuffer...");
        png->decode(NULL, 0);
        
        // Now render the framebuffer using GxEPD2's paged drawing
        display.setFullWindow();
        display.firstPage();
        
        do {
            // Clear screen
            display.fillScreen(GxEPD_WHITE);
            
            // Render the framebuffer pixel by pixel
            for (int y = 0; y < imgHeight; y++) {
                for (int x = 0; x < imgWidth; x++) {
                    uint16_t color = imageBuffer[y * imgWidth + x];
                    display.drawPixel(offsetX + x, offsetY + y, color);
                }
            }
            
        } while (display.nextPage());
        
        // Clean up
        free(imageBuffer);
        g_png_rendering = false;
        g_image_buffer = nullptr;
        
        g_png_rendering = false;
        Log_info("E1002: PNG rendered using GxEPD2 approach");
        rc = REFRESH_FULL;
    }
    png->close();
    free(png);
    return rc;
} /* png_to_7color_epd() */

/**
 * @brief E1002-specific function to build a virtual bitmap for 7-color ePaper
 * @param image_buffer pointer to the uint8_t image buffer
 * @note This function creates a virtual BMP header for E1002's 7-color display
 *       and processes the image data for optimal color rendering
 */
#if defined(BOARD_SEEED_RETERMINAL_E1002)
static void draw_virtual_bmp_from_png(const uint8_t *image_buffer)
{
    // Add error checking
    if (!image_buffer) {
        Log_error("E1002: Invalid image buffer for virtual BMP");
        return;
    }

    //currently only reTerminal E1002 is using the 7color ePaper, and it's using ESP32-S3, RAM is not an issue
    const unsigned char bmp_header[62] = {
        // BITMAPFILEHEADER (14 bytes)
        0x42, 0x4D,             // bfType: 'BM'
        0x4E, 0xBB, 0x00, 0x00, // bfSize: 48062 bytes = 0x0000BB4E
        0x00, 0x00,             // bfReserved1
        0x00, 0x00,             // bfReserved2
        0x3E, 0x00, 0x00, 0x00, // bfOffBits: 62 bytes (header + palette)

        // BITMAPINFOHEADER (40 bytes)
        0x28, 0x00, 0x00, 0x00, // biSize: 40 bytes
        0x20, 0x03, 0x00, 0x00, // biWidth: 800 px (0x0320)
        0x20, 0xFE, 0xFF, 0xFF, // biHeight: -480 (top-down)
        0x01, 0x00,             // biPlanes: 1
        0x01, 0x00,             // biBitCount: 1bpp
        0x00, 0x00, 0x00, 0x00, // biCompression: BI_RGB (no compression)
        0x80, 0xBB, 0x00, 0x00, // biSizeImage: 48000 bytes (0x0000BB80)
        0x13, 0x0B, 0x00, 0x00, // biXPelsPerMeter: 2835 (72 DPI)
        0x13, 0x0B, 0x00, 0x00, // biYPelsPerMeter: 2835 (72 DPI)
        0x02, 0x00, 0x00, 0x00, // biClrUsed: 2 colors
        0x00, 0x00, 0x00, 0x00, // biClrImportant: 0

        // Color Table (8 bytes)
        0x00, 0x00, 0x00, 0x00, // Color 0: Black (B,G,R,0)
        0xFF, 0xFF, 0xFF, 0x00  // Color 1: White (B,G,R,0)
    };
    uint8_t *p_buff = (uint8_t *)malloc(DISPLAY_BMP_IMAGE_SIZE);
    if (!p_buff) {
        Log_error("E1002: Failed to allocate memory for virtual BMP");
        return;
    }

    Log_info("E1002: Drawing virtual BMP from PNG for 7-color display");

    memcpy(p_buff, bmp_header, 62);  // fillin a dummy header
    memcpy(p_buff + 62, image_buffer, DISPLAY_BMP_IMAGE_SIZE - 62);
    int ret = bbep.loadBMP(p_buff, 0, 0, BBEP_WHITE, BBEP_BLACK);  //loadBMP will handle bpp for the color ePaper
    Log_verbose_serial("E1002: Virtual BMP load result: %d", ret);
    free(p_buff);
}
#endif

/**
 * @brief Convert RGB color to nearest E1002 6-color palette
 * @param r Red component (0-255)
 * @param g Green component (0-255) 
 * @param b Blue component (0-255)
 * @return GxEPD2 color constant
 */
uint16_t rgb_to_e1002_color(uint8_t r, uint8_t g, uint8_t b) {
    // Calculate color distance to each of the 6 supported colors
    // Using simple Euclidean distance in RGB space
    
    // Define the 6 supported colors in RGB
    struct {
        uint8_t r, g, b;
        uint16_t color;
    } palette[] = {
        {0, 0, 0, GxEPD_BLACK},      // Black
        {255, 255, 255, GxEPD_WHITE}, // White
        {255, 0, 0, GxEPD_RED},      // Red
        {255, 255, 0, GxEPD_YELLOW}, // Yellow
        {0, 0, 255, GxEPD_BLUE},     // Blue
        {0, 255, 0, GxEPD_GREEN}     // Green
    };
    
    // Debug: Log color constants only once
    static bool colors_logged = false;
    if (!colors_logged) {
        Log_info("E1002: Color constants - BLACK:0x%04X WHITE:0x%04X RED:0x%04X YELLOW:0x%04X BLUE:0x%04X GREEN:0x%04X",
                GxEPD_BLACK, GxEPD_WHITE, GxEPD_RED, GxEPD_YELLOW, GxEPD_BLUE, GxEPD_GREEN);
        colors_logged = true;
    }
    
    int bestMatch = 0;
    int minDistance = INT_MAX;
    
    for (int i = 0; i < 6; i++) {
        int distance = (r - palette[i].r) * (r - palette[i].r) +
                      (g - palette[i].g) * (g - palette[i].g) +
                      (b - palette[i].b) * (b - palette[i].b);
        
        if (distance < minDistance) {
            minDistance = distance;
            bestMatch = i;
        }
    }
    
    return palette[bestMatch].color;
}


/**
 * @brief Custom PNG draw callback for E1002 GxEPD2 rendering
 * @param pDraw PNG draw context
 * @return 1 on success, 0 on failure
 */
int png_draw_to_gxepd2(PNGDRAW *pDraw) {
    if (!g_png_rendering) return 1;
    
    // Convert PNG pixels to E1002 colors and draw them
    uint8_t *pixels = (uint8_t *)pDraw->pPixels;
    int y = pDraw->y;
    
    // Calculate Y coordinate
    int pixelY = g_png_offset_y + y;
    
    // Process each pixel in the line
    for (int x = 0; x < pDraw->iWidth; x++) {
        uint8_t r, g, b;
        
        // Extract RGB values based on pixel type
        if (pDraw->iPixelType == PNG_PIXEL_TRUECOLOR) {
            r = pixels[x * 3];
            g = pixels[x * 3 + 1];
            b = pixels[x * 3 + 2];
        } else if (pDraw->iPixelType == PNG_PIXEL_TRUECOLOR_ALPHA) {
            r = pixels[x * 4];
            g = pixels[x * 4 + 1];
            b = pixels[x * 4 + 2];
        } else if (pDraw->iPixelType == PNG_PIXEL_INDEXED) {
            int paletteIndex = pixels[x];
            r = pDraw->pPalette[paletteIndex * 3];
            g = pDraw->pPalette[paletteIndex * 3 + 1];
            b = pDraw->pPalette[paletteIndex * 3 + 2];
        } else {
            r = g = b = pixels[x];
        }
        
        // Convert to E1002 color and draw pixel
        uint16_t color = rgb_to_e1002_color(r, g, b);
        int pixelX = g_png_offset_x + x;
        
        if (pixelX >= 0 && pixelX < display.width() && 
            pixelY >= 0 && pixelY < display.height()) {
            display.drawPixel(pixelX, pixelY, color);
        }
    }
    
    return 1;
}

/**
 * @brief PNG draw callback for decoding to framebuffer (for GxEPD2)
 * @param pDraw PNG draw context
 * @return 1 on success, 0 on failure
 */
int png_draw_to_buffer(PNGDRAW *pDraw) {
    if (!g_png_rendering || !g_image_buffer) return 1;
    
    // Convert PNG pixels to E1002 colors and store in framebuffer
    uint8_t *pixels = (uint8_t *)pDraw->pPixels;
    int y = pDraw->y;
    
    // Process each pixel in the line
    for (int x = 0; x < pDraw->iWidth; x++) {
        uint8_t r, g, b;
        
        // Extract RGB values based on pixel type
        if (pDraw->iPixelType == PNG_PIXEL_TRUECOLOR) {
            r = pixels[x * 3];
            g = pixels[x * 3 + 1];
            b = pixels[x * 3 + 2];
        } else if (pDraw->iPixelType == PNG_PIXEL_TRUECOLOR_ALPHA) {
            r = pixels[x * 4];
            g = pixels[x * 4 + 1];
            b = pixels[x * 4 + 2];
        } else if (pDraw->iPixelType == PNG_PIXEL_INDEXED) {
            int paletteIndex = pixels[x];
            r = pDraw->pPalette[paletteIndex * 3];
            g = pDraw->pPalette[paletteIndex * 3 + 1];
            b = pDraw->pPalette[paletteIndex * 3 + 2];
        } else {
            r = g = b = pixels[x];
        }
        
        // Convert to E1002 color and store in framebuffer
        uint16_t color = rgb_to_e1002_color(r, g, b);
        
        // Store in framebuffer (row-major order) as uint16_t
        if (y < g_buffer_height && x < g_buffer_width) {
            g_image_buffer[y * g_buffer_width + x] = color;
        }
    }
    
    return 1;
}

/**
 * @brief E1002-specific test function to validate color mapping
 * @note This function tests the 6-color capabilities of the E1002 display using GxEPD2
 */
#if defined(BOARD_SEEED_RETERMINAL_E1002)
void test_e1002_color_mapping() {
    Log_info("E1002: Testing GxEPD2 6-color display capabilities");

    // GxEPD2 color constants for E1002 6-color display
    const char* colorNames[] = {"Black", "White", "Red", "Yellow", "Blue", "Green"};
    const uint16_t colors[] = {GxEPD_BLACK, GxEPD_WHITE, GxEPD_RED, GxEPD_YELLOW, GxEPD_BLUE, GxEPD_GREEN};

    Log_info("E1002: Available GxEPD2 colors:");
    for (int i = 0; i < 6; i++) {
        Log_info("  Color %d: %s (0x%04X)", i, colorNames[i], colors[i]);
    }

    Log_info("E1002: GxEPD2 color mapping test completed");
    Log_info("E1002: Display size: %dx%d", display.width(), display.height());
}
#endif

/**
 * @brief Function to show the image on the display
 * @param image_buffer pointer to the uint8_t image buffer
 * @param reverse shows if the color scheme is reverse
 * @return none
 */
void display_show_image(uint8_t *image_buffer, int data_size, bool bWait)

{
    bool isPNG = data_size >= 4 && MOTOLONG(image_buffer) == (int32_t)0x89504e47;;
    auto width = display_width();
    auto height = display_height();
//    uint32_t *d32;
    bool bAlloc = false;
    int iRefreshMode = REFRESH_FULL; // assume full (slow) refresh

   // Log_info("Paint_NewImage %d", reverse);
    Log_info("display_show_image start");
    Log_info("maximum_compatibility = %d\n", apiDisplayResult.response.maximum_compatibility);
#ifdef FUTURE
    if (reverse)
    {
        d32 = (uint32_t *)image_buffer; // get framebuffer as a 32-bit pointer
        d32 = (uint32_t *)image_buffer; // get framebuffer as a 32-bit pointer
        Log_info("inverse the image");
        for (size_t i = 0; i < buf_size; i+=sizeof(uint32_t))
        for (size_t i = 0; i < buf_size; i+=sizeof(uint32_t))
        {
            d32[0] = ~d32[0];
            d32++;
            d32[0] = ~d32[0];
            d32++;
        }
    }
#endif
    if (isPNG == true && data_size < MAX_IMAGE_SIZE)
    {
        Log_info("Drawing PNG");
#if defined(BOARD_SEEED_RETERMINAL_E1002)
        iRefreshMode = png_to_7color_epd(image_buffer, data_size);
#else
        iRefreshMode = png_to_epd(image_buffer, data_size);
#endif
    }
    else // uncompressed BMP or Group5 compressed image
    {
#if defined(BOARD_SEEED_RETERMINAL_E1002)
        // For E1002, use the original bb_epaper approach for BMP/Group5 images
        // This preserves the original TRMNL loading logo functionality
        Log_info("E1002: Processing BMP/Group5 image with original bb_epaper approach");
        
        if (*(uint16_t *)image_buffer == BB_BITMAP_MARKER)
        {
            // G5 compressed image
            BB_BITMAP *pBBB = (BB_BITMAP *)image_buffer;
            bbep.allocBuffer(false);
            bAlloc = true;
            int x = (width - pBBB->width)/2;
            int y = (height - pBBB->height)/2; // center it
            if (x > 0 || y > 0) // only clear if the image is smaller than the display
            {
                bbep.fillScreen(BBEP_WHITE); 
            }     
            bbep.loadG5Image(image_buffer, x, y, BBEP_WHITE, BBEP_BLACK);
        } 
        else 
        {
         // This work-around is due to a lack of RAM; the correct method would be to use loadBMP()
            flip_image(image_buffer+62, bbep.width(), bbep.height(), false); // fix bottom-up bitmap images
            bbep.allocBuffer(false);
            bAlloc = true;
            draw_virtual_bmp_from_png(image_buffer+62); // uncompressed 1-bpp bitmap
        }
        bbep.writePlane(PLANE_0); // send image data to the EPD
        iRefreshMode = REFRESH_PARTIAL;
        iUpdateCount = 1; // use partial update
#else
        if (*(uint16_t *)image_buffer == BB_BITMAP_MARKER)
        {
            // G5 compressed image
            BB_BITMAP *pBBB = (BB_BITMAP *)image_buffer;
#ifdef BB_EPAPER
            bbep.allocBuffer(false);
            bAlloc = true;
#endif
            int x = (width - pBBB->width)/2;
            int y = (height - pBBB->height)/2; // center it
            if (x > 0 || y > 0) // only clear if the image is smaller than the display
            {
                bbep.fillScreen(BBEP_WHITE); 
            }     
            bbep.loadG5Image(image_buffer, x, y, BBEP_WHITE, BBEP_BLACK);
        } 
        else 
        {
         // This work-around is due to a lack of RAM; the correct method would be to use loadBMP()
            flip_image(image_buffer+62, bbep.width(), bbep.height(), false); // fix bottom-up bitmap images
#ifdef BB_EPAPER
            bbep.allocBuffer(false);
            bAlloc = true;
            bbep.setBuffer(image_buffer+62); // uncompressed 1-bpp bitmap
#endif
        }
        bbep.writePlane(PLANE_0); // send image data to the EPD
        iRefreshMode = REFRESH_PARTIAL;
        iUpdateCount = 1; // use partial update
#endif
    }
    Log_info("Display refresh start");
#if defined(BOARD_SEEED_RETERMINAL_E1002)
    // For E1002, the image rendering is already done in the PNG/BMP processing above
    // GxEPD2 handles the refresh automatically in the paged drawing mode
    Log_info("E1002: Image rendering completed with GxEPD2");
    iUpdateCount++;
#else
#ifdef BB_EPAPER
    if ((iUpdateCount & 7) == 0 || apiDisplayResult.response.maximum_compatibility == true) {
        Log_info("%s [%d]: Forcing full refresh; desired refresh mode was: %d\r\n", __FILE__, __LINE__, iRefreshMode);
        iRefreshMode = REFRESH_FULL; // force full refresh every 8 partials
    }
    int refresh_seconds = preferences.getUInt(PREFERENCES_SLEEP_TIME_KEY, SLEEP_TIME_TO_SLEEP);
    if (refresh_seconds >= 30*60 && iRefreshMode == REFRESH_PARTIAL) {
        // For users who set updates 30 minutes or longer, use the "fast" update to prevent ghosting
        Log_info("%s [%d]: Forcing fast refresh (not partial) since the TRMNL refresh_rate is set to > 30 min\n", __FILE__, __LINE__);
        iRefreshMode = REFRESH_FAST;
    }
    if (!bWait) iRefreshMode = REFRESH_PARTIAL; // fast update when showing loading screen
    Log_info("%s [%d]: EPD refresh mode: %d\r\n", __FILE__, __LINE__, iRefreshMode);
    bbep.refresh(iRefreshMode, bWait);
    if (bAlloc) {
        bbep.freeBuffer();
    }
    iUpdateCount++;
#else
    bbep.fullUpdate();
#endif
#endif
    Log_info("display_show_image end");
}
/**
 * @brief Function to read an image from the file system
 * @param filename
 * @param pointer to file size returned
 * @return pointer to allocated buffer
 */
uint8_t * display_read_file(const char *filename, int *file_size)
{
File f = SPIFFS.open(filename, "r");
uint8_t *buffer;

  if (!f) {
    Serial.println("Failed to open file!");
    *file_size = 0;
    return nullptr;
  }
  *file_size = f.size();
  buffer = (uint8_t *)malloc(*file_size);
  if (!buffer) {
    Serial.println("Memory allocation filed!");
    *file_size = 0;
    return nullptr;
  }
  f.read(buffer, *file_size);
  f.close();
  return buffer;
} /* display_read_file() */

/**
 * @brief Function to show the image with message on the display
 * @param image_buffer pointer to the uint8_t image buffer
 * @param message_type type of message that will show on the screen
 * @return none
 */
void display_show_msg(uint8_t *image_buffer, MSG message_type)
{
    auto width = display_width();
    auto height = display_height();
    UWORD Imagesize = ((width % 8 == 0) ? (width / 8) : (width / 8 + 1)) * height;
    BB_RECT rect;

    Log_info("display_show_msg start");
    Log_info("maximum_compatibility = %d\n", apiDisplayResult.response.maximum_compatibility);
#ifdef BB_EPAPER
    bbep.allocBuffer(false);
#endif
    if (*(uint16_t *)image_buffer == BB_BITMAP_MARKER)
    {
        // G5 compressed image
        BB_BITMAP *pBBB = (BB_BITMAP *)image_buffer;
        int x = (width - pBBB->width)/2;
        int y = (height - pBBB->height)/2; // center it
        if (x > 0 || y > 0) // only clear if the image is smaller than the display
        {
            bbep.fillScreen(BBEP_WHITE); 
        }
        bbep.loadG5Image(image_buffer, x, y, BBEP_WHITE, BBEP_BLACK);
    }
    else
    {
#ifdef BB_EPAPER
        memcpy(bbep.getBuffer(), image_buffer+62, Imagesize); // uncompressed 1-bpp bitmap
#endif
    }

    bbep.setFont(nicoclean_8); //Roboto_20);
    bbep.setTextColor(BBEP_BLACK, BBEP_WHITE);

    switch (message_type)
    {
    case WIFI_CONNECT:
    {
        const char string1[] = "Connect to TRMNL WiFi";
        bbep.getStringBox(string1, &rect);
        bbep.setCursor((bbep.width() - rect.w)/2, 430);
        bbep.println(string1);
        const char string2[] = "on your phone or computer";
        bbep.getStringBox(string2, &rect);
        bbep.setCursor((bbep.width() - rect.w)/2, -1);
        bbep.print(string2);
    }
    break;
    case WIFI_FAILED:
    {
        const char string1[] = "Can't establish WiFi connection.";
        bbep.getStringBox(string1, &rect);
        bbep.setCursor((bbep.width() - rect.w)/2, 386);
        bbep.println(string1);
        const char string2[] = "Hold button on the back to reset WiFi, or scan QR Code for help.";
        bbep.getStringBox(string2, &rect);
        bbep.setCursor((bbep.width() - rect.w) / 2, -1);
        bbep.println(string2);

        bbep.loadG5Image(wifi_failed_qr, bbep.width() - 66 - 40, 40, BBEP_WHITE, BBEP_BLACK);
    }
    break;
    case WIFI_INTERNAL_ERROR:
    {
        const char string1[] = "WiFi connected, but";
        bbep.getStringBox(string1, &rect);
        bbep.setCursor((bbep.width() - 132 - rect.w) / 2, 340);
        bbep.println(string1);
        const char string2[] = "API connection cannot be";
        bbep.getStringBox(string2, &rect);
        bbep.setCursor((bbep.width() - 132 - rect.w) / 2, -1);
        bbep.println(string2);
        const char string3[] = "established. Try to refresh,";
        bbep.getStringBox(string3, &rect);
        bbep.setCursor((bbep.width() - 132 - rect.w) / 2, -1);
        bbep.println(string3);
        const char string4[] = "or scan QR Code for help.";
        bbep.getStringBox(string4, &rect);
        bbep.setCursor((bbep.width() - 132 - rect.w) / 2, -1);
        bbep.print(string4);

        bbep.loadG5Image(wifi_failed_qr, 639, 336, BBEP_WHITE, BBEP_BLACK);
    }
    break;
    case WIFI_WEAK:
    {
        const char string1[] = "WiFi connected but signal is weak";
        bbep.getStringBox(string1, &rect);
        bbep.setCursor((bbep.width() - rect.w) / 2, 400);
        bbep.print(string1);
    }
    break;
    case API_ERROR:
    {
        const char string1[] = "WiFi connected, TRMNL not responding.";
        bbep.getStringBox(string1, &rect);
        bbep.setCursor((bbep.width() - rect.w) / 2, 340);
        bbep.println(string1);
        const char string2[] = "Short click the button on back,";
        bbep.getStringBox(string2, &rect);
        bbep.setCursor((bbep.width() - rect.w) / 2, -1);
        bbep.println(string2);
        const char string3[] = "otherwise check your internet.";
        bbep.getStringBox(string3, &rect);
        bbep.setCursor((bbep.width() - rect.w) / 2, -1);
        bbep.print(string3);
    }
    break;
    case API_SIZE_ERROR:
    {
        const char string1[] = "WiFi connected, TRMNL content malformed.";
        bbep.getStringBox(string1, &rect);
        bbep.setCursor((bbep.width() - rect.w) / 2, 400);
        bbep.println(string1);
        const char string2[] = "Wait or reset by holding button on back.";
        bbep.getStringBox(string2, &rect);
        bbep.setCursor((bbep.width() - rect.w) / 2, -1);
        bbep.print(string2);
    }
    break;
    case FW_UPDATE:
    {
        const char string1[] = "Firmware update available! Starting now...";
        bbep.getStringBox(string1, &rect);
        bbep.setCursor((bbep.width() - rect.w) / 2, 400);
        bbep.print(string1);
    }
    break;
    case FW_UPDATE_FAILED:
    {
        const char string1[] = "Firmware update failed. Device will restart...";
        bbep.getStringBox(string1, &rect);
        bbep.setCursor((bbep.width() - rect.w) / 2, 400);
        bbep.print(string1);
    }
    break;
    case FW_UPDATE_SUCCESS:
    {
        const char string1[] = "Firmware update success. Device will restart...";
        bbep.getStringBox(string1, &rect);
        bbep.setCursor((bbep.width() - rect.w) / 2, 400);
        bbep.print(string1);
    }
    break;
    case MSG_TOO_BIG:
    {
        const char string1[] = "The image file from this URL is too large.";
        bbep.getStringBox(string1, &rect);
        bbep.setCursor((bbep.width() - rect.w) / 2, 360);
        bbep.println(string1);
        if (strlen(filename) > 40) {
            filename[40] = 0; // truncate and add elipses
            strcat(filename, "...");
        }
        bbep.getStringBox(filename, &rect);
        bbep.setCursor((bbep.width() - rect.w) / 2, -1);
        bbep.println(filename);

        const char string2[] = "PNG images can be a maximum of";
        bbep.getStringBox(string2, &rect);
        bbep.setCursor((bbep.width() - rect.w) / 2, -1);
        bbep.println(string2);
        String string3 = String(MAX_IMAGE_SIZE) + String(" bytes each and 1 or 2-bpp");
        bbep.getStringBox(string3.c_str(), &rect);
        bbep.setCursor((bbep.width() - rect.w) / 2, -1);
        bbep.print(string3);
    }
    break;
    case MSG_FORMAT_ERROR:
    {
        const char string1[] = "The image format is incorrect";
        bbep.getStringBox(string1, &rect);
        bbep.setCursor((bbep.width() - rect.w) / 2, 400);
        bbep.print(string1);
    }
    break;
    case TEST:
    {
        bbep.setCursor(0, 40);
        bbep.println("ABCDEFGHIYABCDEFGHIYABCDEFGHIYABCDEFGHIYABCDEFGHIY");
        bbep.println("abcdefghiyabcdefghiyabcdefghiyabcdefghiyabcdefghiy");
        bbep.println("A B C D E F G H I Y A B C D E F G H I Y A B C D E");
        bbep.println("a b c d e f g h i y a b c d e f g h i y a b c d e");
    }
    break;
    default:
        break;
    }
#ifdef BB_EPAPER
    bbep.writePlane(PLANE_0);
    bbep.refresh(REFRESH_FULL, true);
    bbep.freeBuffer();
#else
    bbep.fullUpdate();
#endif
    Log_info("display_show_msg end");
}

/**
 * @brief Function to show the image with message on the display
 * @param image_buffer pointer to the uint8_t image buffer
 * @param message_type type of message that will show on the screen
 * @param friendly_id device friendly ID
 * @param id shows if ID exists
 * @param fw_version version of the firmware
 * @param message additional message
 * @return none
 */
void display_show_msg(uint8_t *image_buffer, MSG message_type, String friendly_id, bool id, const char *fw_version, String message)
{
    Log_info("Free heap in display_show_msg - %d", ESP.getMaxAllocHeap());
    Log_info("maximum_compatibility = %d\n", apiDisplayResult.response.maximum_compatibility);
#ifdef BB_EPAPER
    bbep.allocBuffer(false);
    Log_info("Free heap after bbep.allocBuffer() - %d", ESP.getMaxAllocHeap());
#endif

#if !defined(BOARD_SEEED_RETERMINAL_E1002)    //for E1002 the screen refresh takes long, remove this unnecessary refresh
    if (message_type == WIFI_CONNECT)
    {
        Log_info("Display set to white");
        bbep.fillScreen(BBEP_WHITE);
#ifdef BB_EPAPER
        bbep.writePlane(PLANE_0);
        if (!apiDisplayResult.response.maximum_compatibility) {
            bbep.refresh(REFRESH_FAST, true); // newer panel can handle the fast refresh
        } else {
            bbep.refresh(REFRESH_FULL, true); // incompatible panel (for now)
        }
#else
        bbep.fullUpdate();
#endif
        display_sleep(1000);
    }
#endif

    auto width = display_width();
    auto height = display_height();
    UWORD Imagesize = ((width % 8 == 0) ? (width / 8) : (width / 8 + 1)) * height;
    BB_RECT rect;

    Log_info("display_show_msg2 start");

    // Load the image into the bb_epaper framebuffer
    if (*(uint16_t *)image_buffer == BB_BITMAP_MARKER)
    {
        // G5 compressed image
        BB_BITMAP *pBBB = (BB_BITMAP *)image_buffer;
        int x = (width - pBBB->width)/2;
        int y = (height - pBBB->height)/2; // center it
        if (x > 0 || y > 0) // only clear if the image is smaller than the display
        { 
            bbep.fillScreen(BBEP_WHITE);
        }
        bbep.loadG5Image(image_buffer, x, y, BBEP_WHITE, BBEP_BLACK);
    }
    else
    {
#ifdef BB_EPAPER
        memcpy(bbep.getBuffer(), image_buffer+62, Imagesize); // uncompressed 1-bpp bitmap
#endif
    }

    bbep.setFont(nicoclean_8); //Roboto_20);
    bbep.setTextColor(BBEP_BLACK, BBEP_WHITE);
    switch (message_type)
    {
    case FRIENDLY_ID:
    {
        Log_info("friendly id case");
        const char string1[] = "Please sign up at usetrmnl.com/signup";
        bbep.getStringBox(string1, &rect);
        bbep.setCursor((bbep.width() - rect.w)/2, 400);
        bbep.println(string1);

        String string2 = "with Friendly ID ";
        if (id)
        {
            string2 += friendly_id;
        }
        string2 += " to finish setup";
        bbep.getStringBox(string2, &rect);
        bbep.setCursor((bbep.width() - rect.w)/2, -1);
        bbep.print(string2);
    }
    break;
    case WIFI_CONNECT:
    {
        Log_info("wifi connect case");

        String string1 = "TRMNL firmware ";
        string1 += fw_version;
        bbep.setCursor(40, 48); // place in upper left corner
        bbep.println(string1);
        const char string2[] = "Connect your phone or computer to TRMNL WiFi network";
        bbep.getStringBox(string2, &rect);
        bbep.setCursor((bbep.width() - rect.w) / 2, 386);
        bbep.println(string2);
        const char string3[] = "or scan the QR code for help";
        bbep.getStringBox(string3, &rect);
        bbep.setCursor((bbep.width() - rect.w) / 2, -1);
        bbep.print(string3);
        bbep.loadG5Image(wifi_connect_qr, bbep.width() - 40 - 66, 40, BBEP_WHITE, BBEP_BLACK); // 66x66 QR code
    }
    break;
    case MAC_NOT_REGISTERED:
    {
        UWORD y_start = 340;
        UWORD font_width = 18; // DEBUG
        Paint_DrawMultilineText(0, y_start, message.c_str(), width, font_width, BBEP_BLACK, BBEP_WHITE, nicoclean_8/*Roboto_20*/, true);
    }
    break;
    default:
        break;
    }
    Log_info("Start drawing...");
#ifdef BB_EPAPER
    bbep.writePlane(PLANE_0);
    bbep.refresh(REFRESH_FULL, true);
    bbep.freeBuffer();
#else
    bbep.fullUpdate();
#endif
    Log_info("display_show_msg2 end");
}

/**
 * @brief Function to got the display to the sleep
 * @param none
 * @return none
 */
void display_sleep(void)
{
    Log_info("Goto Sleep...");
#if defined(BOARD_SEEED_RETERMINAL_E1002)
    display.hibernate();
#else
    #ifdef BB_EPAPER
    bbep.sleep(DEEP_SLEEP);
    #else
    bbep.einkPower(0);
    bbep.deInit();
    #endif
#endif
}