#include "config.h"

#ifdef MZ61581

#include "spi.h"

#include <memory.h>
#include <stdio.h>

static void MZ61581ClearScreen()
{
  // Since we are doing delta updates to only changed pixels, clear display initially to black for known starting state
  for(int y = 0; y < DISPLAY_HEIGHT; ++y)
  {
    SPI_TRANSFER(DISPLAY_SET_CURSOR_X, 0, 0, DISPLAY_WIDTH >> 8, DISPLAY_WIDTH & 0xFF);
    SPI_TRANSFER(DISPLAY_SET_CURSOR_Y, (uint8_t)(y >> 8), (uint8_t)(y & 0xFF), DISPLAY_HEIGHT >> 8, DISPLAY_HEIGHT & 0xFF);
    SPITask *clearLine = AllocTask(DISPLAY_WIDTH*2);
    clearLine->cmd = DISPLAY_WRITE_PIXELS;
    memset(clearLine->data, 0, clearLine->size);
    CommitTask(clearLine);
    RunSPITask(clearLine);
    DoneTask(clearLine);
  }
  SPI_TRANSFER(DISPLAY_SET_CURSOR_X, 0, 0, DISPLAY_WIDTH >> 8, DISPLAY_WIDTH & 0xFF);
  SPI_TRANSFER(DISPLAY_SET_CURSOR_Y, 0, 0, DISPLAY_HEIGHT >> 8, DISPLAY_HEIGHT & 0xFF);
}

void InitMZ61581()
{
  // If a Reset pin is defined, toggle it briefly high->low->high to enable the device. Some devices do not have a reset pin, in which case compile with GPIO_TFT_RESET_PIN left undefined.
#if defined(GPIO_TFT_RESET_PIN) && GPIO_TFT_RESET_PIN >= 0
  printf("Resetting display at reset GPIO pin %d\n", GPIO_TFT_RESET_PIN);
  SET_GPIO_MODE(GPIO_TFT_RESET_PIN, 1);
  SET_GPIO(GPIO_TFT_RESET_PIN);
  usleep(120 * 1000);
  CLEAR_GPIO(GPIO_TFT_RESET_PIN);
  usleep(120 * 1000);
  SET_GPIO(GPIO_TFT_RESET_PIN);
  usleep(120 * 1000);
#endif

  // Do the initialization with a very low SPI bus speed, so that it will succeed even if the bus speed chosen by the user is too high.
  spi->clk = 34;
  __sync_synchronize();

  BEGIN_SPI_COMMUNICATION();
  {
    // Reverse engineered with logic analyzer, not sure what these mean. If you have a data sheet for MZ61581, please send it my way.
    SPI_TRANSFER(0xB0, 0x00);
    SPI_TRANSFER(0xB3, 0x02, 0x00, 0x00, 0x00);
    SPI_TRANSFER(0xC0, 0x13, 0x3B, 0x00, 0x02, 0x00, 0x01, 0x00, 0x43);
    SPI_TRANSFER(0xC1, 0x08, 0x16, 0x08, 0x08);
    SPI_TRANSFER(0xC4, 0x11, 0x07, 0x03, 0x03);
    SPI_TRANSFER(0xC6, 0x00);
    SPI_TRANSFER(0xC8, 0x03, 0x03, 0x13, 0x5C, 0x03, 0x07, 0x14, 0x08, 0x00, 0x21, 0x08, 0x14, 0x07, 0x53, 0x0C, 0x13, 0x03, 0x03, 0x21, 0x00);
    SPI_TRANSFER(0x35, 0x00);
    SPI_TRANSFER(0x44, 0x00, 0x01);
    SPI_TRANSFER(0xD0, 0x07, 0x07, 0x1D, 0x03);
    SPI_TRANSFER(0xD1, 0x03, 0x30, 0x10);
    SPI_TRANSFER(0xD2, 0x03, 0x14, 0x04);

    // The following coincide with e.g. ILI9341.

    SPI_TRANSFER(0x3A/*COLMOD: Pixel Format Set*/, 0x55/*DPI=16bits/pixel,DBI=16bits/pixel*/);

#define MADCTL_BGR_PIXEL_ORDER (1<<3)
#define MADCTL_ROW_COLUMN_EXCHANGE (1<<5)
#define MADCTL_COLUMN_ADDRESS_ORDER_SWAP (1<<6)
#define MADCTL_ROW_ADDRESS_ORDER_SWAP (1<<7)
#define MADCTL_ROTATE_180_DEGREES 0xC0

    uint8_t madctl = MADCTL_BGR_PIXEL_ORDER | MADCTL_COLUMN_ADDRESS_ORDER_SWAP;
#ifdef DISPLAY_ROTATE_180_DEGREES
    madctl |= MADCTL_ROTATE_180_DEGREES;
#endif
#if defined(DISPLAY_OUTPUT_LANDSCAPE) && !defined(DISPLAY_FLIP_OUTPUT_XY_IN_SOFTWARE)
    madctl |= MADCTL_ROW_COLUMN_EXCHANGE;
#endif
    SPI_TRANSFER(0x36/*MADCTL: Memory Access Control*/, madctl);

    SPI_TRANSFER(0x11/*Sleep Out*/);
    usleep(300 * 1000);
    SPI_TRANSFER(0x29/*Display ON*/);
    SPI_TRANSFER(0x2C);

    // TONTEC_MZ61581 has backlight active when backlight GPIO is low, and at boot, it seems to be disabled, so always need to enable it.
#if defined(GPIO_TFT_BACKLIGHT) && (defined(BACKLIGHT_CONTROL) || defined(TONTEC_MZ61581))
    printf("Setting TFT backlight on at pin %d\n", GPIO_TFT_BACKLIGHT);
    SET_GPIO_MODE(GPIO_TFT_BACKLIGHT, 0x01); // Set backlight pin to digital 0/1 output mode (0x01) in case it had been PWM controlled
    CLEAR_GPIO(GPIO_TFT_BACKLIGHT); // And turn the backlight on. MZ61581 backlight is on when the Backlight GPIO pin is 0.
#endif

    MZ61581ClearScreen();
  }
#ifndef USE_DMA_TRANSFERS // For DMA transfers, keep SPI CS & TA active.
  END_SPI_COMMUNICATION();
#endif

  // And speed up to the desired operation speed finally after init is done.
  spi->clk = SPI_BUS_CLOCK_DIVISOR;
}

void TurnDisplayOff()
{
#if defined(GPIO_TFT_BACKLIGHT) && defined(BACKLIGHT_CONTROL)
  SET_GPIO_MODE(GPIO_TFT_BACKLIGHT, 0x01); // Set backlight pin to digital 0/1 output mode (0x01) in case it had been PWM controlled
  SET_GPIO(GPIO_TFT_BACKLIGHT); // And turn the backlight off.
#endif
#if 0
  QUEUE_SPI_TRANSFER(0x28/*Display OFF*/);
  QUEUE_SPI_TRANSFER(0x10/*Enter Sleep Mode*/);
  usleep(120*1000); // Sleep off can be sent 120msecs after entering sleep mode the earliest, so synchronously sleep here for that duration to be safe.
#endif
//  printf("Turned display OFF\n");
}

void TurnDisplayOn()
{
#if 0
  QUEUE_SPI_TRANSFER(0x11/*Sleep Out*/);
  usleep(120 * 1000);
  QUEUE_SPI_TRANSFER(0x29/*Display ON*/);
#endif
#if defined(GPIO_TFT_BACKLIGHT) && defined(BACKLIGHT_CONTROL)
  SET_GPIO_MODE(GPIO_TFT_BACKLIGHT, 0x01); // Set backlight pin to digital 0/1 output mode (0x01) in case it had been PWM controlled
  CLEAR_GPIO(GPIO_TFT_BACKLIGHT); // And turn the backlight on.
#endif
//  printf("Turned display ON\n");
}

void DeinitSPIDisplay()
{

}

#endif
