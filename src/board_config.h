#ifndef TRI_MODE_BOARD_CONFIG_H
#define TRI_MODE_BOARD_CONFIG_H

/* Entire USB-C input budget, including charging and all downstream loads.
 * Raise only after the Rev B power budget is established. */
#ifndef BOARD_USB_MAX_POWER_MA
#define BOARD_USB_MAX_POWER_MA 100u
#endif
#if (BOARD_USB_MAX_POWER_MA != 100u) && (BOARD_USB_MAX_POWER_MA != 500u)
#error "BQ24074 USB mode must match a 100mA or 500mA descriptor budget"
#endif

/* Permission from a validated battery/power configuration, not PGOOD#.
 * Uncalibrated ADC values must never grant this permission implicitly. */
#ifndef BOARD_BATTERY_PERIPHERALS_ALLOWED
#define BOARD_BATTERY_PERIPHERALS_ALLOWED 0
#endif

#endif
