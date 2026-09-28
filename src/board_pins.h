#ifndef TRI_MODE_HID_HUB_BOARD_PINS_H
#define TRI_MODE_HID_HUB_BOARD_PINS_H

/*
 * CH582M Rev B. Authoritative assignment:
 * docs/hardware/pin-allocation-revb.csv (2026-09-28).
 * All masks below are port-local; comments specify port and polarity.
 * Peripheral alternate mappings are owned by Board_Init(), not drivers.
 */
#ifndef BOARD_PS2_KEYBOARD_CLK_PIN
#define BOARD_PS2_KEYBOARD_CLK_PIN       (1u << 0) /* PA0 / KBD_CLK_MCU */
#endif

#ifndef BOARD_PS2_KEYBOARD_DATA_PIN
#define BOARD_PS2_KEYBOARD_DATA_PIN      (1u << 1) /* PA1 / KBD_DATA_MCU */
#endif

#ifndef BOARD_PS2_MOUSE_CLK_PIN
#define BOARD_PS2_MOUSE_CLK_PIN          (1u << 2) /* PA2 / MOUSE_CLK_MCU */
#endif

#ifndef BOARD_PS2_MOUSE_DATA_PIN
#define BOARD_PS2_MOUSE_DATA_PIN         (1u << 3) /* PA3 / MOUSE_DATA_MCU */
#endif

#ifndef BOARD_UART1_RX_PIN
#define BOARD_UART1_RX_PIN               (1u << 8) /* PA8 / UART_RX */
#endif

#ifndef BOARD_UART1_TX_PIN
#define BOARD_UART1_TX_PIN               (1u << 9) /* PA9 / UART_TX */
#endif

#ifndef BOARD_USB_HOST_ENABLE_PIN
#define BOARD_USB_HOST_ENABLE_PIN        (1u << 6) /* PB6 / HOST_EN, active high */
#endif

#ifndef BOARD_UART1_BAUDRATE
#define BOARD_UART1_BAUDRATE             115200u
#endif

#define BOARD_USB_DEVICE_DP_PIN          (1u << 11) /* PB11 */
#define BOARD_USB_DEVICE_DM_PIN          (1u << 10) /* PB10 */
#define BOARD_USB_HOST_DP_PIN            (1u << 13) /* PB13 */
#define BOARD_USB_HOST_DM_PIN            (1u << 12) /* PB12 */
#define BOARD_HOST_FAULT_PIN             (1u << 5)  /* PB5, active low */
#define BOARD_CHARGING_PIN               (1u << 9)  /* PB9, active low */
#define BOARD_CHARGER_EN1_PIN            (1u << 8)  /* PB8 */
#define BOARD_CHARGER_EN2_PIN            (1u << 17) /* PB17 */
#define BOARD_INPUT_PGOOD_PIN            (1u << 16) /* PB16, digital only */
#define BOARD_USER_PIN                   (1u << 18) /* PB18, active low */
#define BOARD_BATTERY_ADC_PIN            (1u << 6)  /* PA6 */
#define BOARD_BATTERY_ADC_CHANNEL        10u
#define BOARD_VBUS_ADC_PIN               (1u << 7)  /* PA7, USB-C VBUS */
#define BOARD_VBUS_ADC_CHANNEL           11u
#define BOARD_UART3_RX_PIN               (1u << 4)  /* PA4, J10 TTL */
#define BOARD_UART3_TX_PIN               (1u << 5)  /* PA5, J10 TTL */
#define BOARD_UART0_RX_PIN               (1u << 4)  /* PB4, MCP2120 RX */
#define BOARD_UART0_TX_PIN               (1u << 7)  /* PB7, MCP2120 TX */
#define BOARD_IRDA_ENABLE_PIN            (1u << 3)  /* PB3, active high */
#define BOARD_IRDA_MODE_PIN              (1u << 2)  /* PB2, high=data */
#define BOARD_IRDA_SHUTDOWN_PIN          (1u << 19) /* PB19, active high */
#define BOARD_IR_RX_PIN                  (1u << 1)  /* PB1, demodulated */
#define BOARD_IR_TX_PIN                  (1u << 0)  /* PB0, PWM6 */
#define BOARD_I2C_SCL_PIN                (1u << 21) /* PB21, remapped */
#define BOARD_I2C_SDA_PIN                (1u << 20) /* PB20, remapped */
#define BOARD_SPI_CS_PIN                 (1u << 12) /* PA12, active low */
#define BOARD_SPI_SCK_PIN                (1u << 13) /* PA13 */
#define BOARD_SPI_MOSI_PIN               (1u << 14) /* PA14 */
#define BOARD_SPI_MISO_PIN               (1u << 15) /* PA15 */
#define BOARD_DEBUG_TIO_PIN              (1u << 14) /* PB14, reserved */
#define BOARD_DEBUG_TCK_PIN              (1u << 15) /* PB15, reserved */
#define BOARD_BOOT_PIN                   (1u << 22) /* PB22, reserved */
#define BOARD_RESET_PIN                  (1u << 23) /* PB23, reserved */
#define BOARD_LSE_PINS                   ((1u << 10) | (1u << 11)) /* PA */

#ifndef BOARD_UART3_BAUDRATE
#define BOARD_UART3_BAUDRATE             115200u
#endif
#define BOARD_IRDA_INITIAL_BAUDRATE      9600u

#endif /* TRI_MODE_HID_HUB_BOARD_PINS_H */
