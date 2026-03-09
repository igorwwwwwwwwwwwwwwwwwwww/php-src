/*
 * Copyright (c) 2024 Raspberry Pi (Trading) Ltd.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

// -----------------------------------------------------
// NOTE: THIS HEADER IS ALSO INCLUDED BY ASSEMBLER SO
//       SHOULD ONLY CONSIST OF PREPROCESSOR DIRECTIVES
// -----------------------------------------------------

// pico_cmake_set PICO_PLATFORM=rp2350
// pico_cmake_set PICO_CYW43_SUPPORTED = 1

#ifndef _BOARDS_PICO2_W_H
#define _BOARDS_PICO2_W_H

#define BW_RTC_I2C       i2c0
#define BW_RTC_ADDR      (0x51)
#define BW_RTC_I2C_SDA   (4)
#define BW_RTC_I2C_SCL   (5)

#define BW_LED_0         (0)
#define BW_LED_1         (1)
#define BW_LED_2         (2)
#define BW_LED_3         (3)

#define BW_PSRAM_CS      (8)

#define BW_SWITCH_A      (7)
#define BW_SWITCH_B      (9)
#define BW_SWITCH_C      (10)
#define BW_SWITCH_UP     (11)
#define BW_SWITCH_DOWN   (6)
#define BW_RESET_SW      (14)

#define BW_LCD_BACKLIGHT (26)
#define BW_LCD_CS        (27)
#define BW_LCD_DC        (28)
#define BW_LCD_WR        (30)
#define BW_LCD_RD        (31)
#define BW_LCD_D0        (32)

#define BW_VBAT_SENSE    (40)
#define BW_SW_POWER_EN   (41)
#define BW_SENSE_1V1     (42)
#define BW_LIGHT_SENSE   (43)

#define BW_VBUS_DETECT   (12)
#define BW_RTC_ALARM     (13)
#define BW_SWITCH_HOME   (22)
#define BW_SWITCH_INT    (15)
#define BW_SWITCH_MASK   ((1 << BW_SWITCH_A) | (1 << BW_SWITCH_B) | (1 << BW_SWITCH_C) | (1 << BW_SWITCH_UP) | (1 << BW_SWITCH_DOWN))

#define PLL_SYS_REFDIV   (1)
#define PLL_SYS_VCO_FREQ_HZ (1200000000)
#define PLL_SYS_POSTDIV1 (6)
#define PLL_SYS_POSTDIV2 (1)
#define SYS_CLK_HZ       (200000000)

#define CYW43_PIO_CLOCK_DIV_INT 3

#ifndef PICO_DEFAULT_I2C
#define PICO_DEFAULT_I2C 0
#endif
#ifndef PICO_DEFAULT_I2C_SDA_PIN
#define PICO_DEFAULT_I2C_SDA_PIN BW_RTC_I2C_SDA
#endif
#ifndef PICO_DEFAULT_I2C_SCL_PIN
#define PICO_DEFAULT_I2C_SCL_PIN BW_RTC_I2C_SCL
#endif

#define PICO_BOOT_STAGE2_CHOOSE_W25Q080 1

#ifndef PICO_FLASH_SPI_CLKDIV
#define PICO_FLASH_SPI_CLKDIV 2
#endif

#ifndef PICO_FLASH_SIZE_BYTES
#define PICO_FLASH_SIZE_BYTES (16 * 1024 * 1024)
#endif

#ifndef CYW43_WL_GPIO_COUNT
#define CYW43_WL_GPIO_COUNT 3
#endif

#ifndef PICO_RP2350_A2_SUPPORTED
#define PICO_RP2350_A2_SUPPORTED 1
#endif

#ifndef CYW43_PIN_WL_DYNAMIC
#define CYW43_PIN_WL_DYNAMIC 0
#endif
#ifndef CYW43_DEFAULT_PIN_WL_REG_ON
#define CYW43_DEFAULT_PIN_WL_REG_ON 23u
#endif
#ifndef CYW43_DEFAULT_PIN_WL_DATA_OUT
#define CYW43_DEFAULT_PIN_WL_DATA_OUT 24u
#endif
#ifndef CYW43_DEFAULT_PIN_WL_DATA_IN
#define CYW43_DEFAULT_PIN_WL_DATA_IN 24u
#endif
#ifndef CYW43_DEFAULT_PIN_WL_HOST_WAKE
#define CYW43_DEFAULT_PIN_WL_HOST_WAKE 24u
#endif
#ifndef CYW43_DEFAULT_PIN_WL_CLOCK
#define CYW43_DEFAULT_PIN_WL_CLOCK 29u
#endif
#ifndef CYW43_DEFAULT_PIN_WL_CS
#define CYW43_DEFAULT_PIN_WL_CS 25u
#endif

#endif
