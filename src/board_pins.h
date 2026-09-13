#ifndef TRI_MODE_HID_HUB_BOARD_PINS_H
#define TRI_MODE_HID_HUB_BOARD_PINS_H

/*
 * First-pass CH582M development-board wiring.  The PCB net names in
 * docs/pin-plan.md are the source of truth for the eventual board; keeping
 * these masks in one header lets a bench harness override them without
 * touching the protocol or router layers.
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

#ifndef BOARD_UART1_BAUDRATE
#define BOARD_UART1_BAUDRATE             115200u
#endif

#endif /* TRI_MODE_HID_HUB_BOARD_PINS_H */
