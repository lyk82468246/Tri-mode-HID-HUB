#include "CH58x_common.h"
#include "board.h"
#include "board_pins.h"

void Board_Init(void)
{
    /* Disable outputs before changing multiplexers. UART2/SPI1 cannot be
     * used on Rev B: their pins belong to ADC/PS2 or BOOT/RESET. */
    R8_UART0_IER = 0u;
    R8_UART0_MCR = 0u;
    R8_UART1_IER = 0u;
    R8_UART2_IER = 0u;
    R8_UART3_IER = 0u;
    R8_TMR0_INTER_EN = 0u;
    R8_TMR0_CTRL_MOD = RB_TMR_ALL_CLEAR;
    R8_SPI1_CTRL_MOD = 0u;
    R8_PWM_OUT_EN = 0u;
    R16_PIN_ALTERNATE = (R16_PIN_ALTERNATE &
        (uint16_t)~(RB_PIN_UART0 | RB_PIN_UART1 | RB_PIN_UART2 |
                    RB_PIN_UART3 | RB_PIN_SPI0 | RB_PIN_PWMX |
                    RB_PIN_MODEM | RB_PIN_U0_INV)) | RB_PIN_I2C;

    /* Preload output latches before switching direction: USB100, external
     * 5V off, codec disabled, optical transceiver asleep, IR LED off. */
    GPIOB_ResetBits(BOARD_USB_HOST_ENABLE_PIN | BOARD_CHARGER_EN1_PIN |
                    BOARD_CHARGER_EN2_PIN | BOARD_IRDA_ENABLE_PIN |
                    BOARD_IR_TX_PIN);
    GPIOB_SetBits(BOARD_IRDA_SHUTDOWN_PIN | BOARD_IRDA_MODE_PIN);
    GPIOB_ModeCfg(BOARD_USB_HOST_ENABLE_PIN | BOARD_CHARGER_EN1_PIN |
                  BOARD_CHARGER_EN2_PIN | BOARD_IRDA_ENABLE_PIN |
                  BOARD_IR_TX_PIN | BOARD_IRDA_SHUTDOWN_PIN |
                  BOARD_IRDA_MODE_PIN, GPIO_ModeOut_PP_5mA);
    GPIOB_ModeCfg(BOARD_HOST_FAULT_PIN | BOARD_CHARGING_PIN |
                  BOARD_INPUT_PGOOD_PIN | BOARD_USER_PIN |
                  BOARD_IR_RX_PIN, GPIO_ModeIN_PU);

    GPIOA_ModeCfg(BOARD_BATTERY_ADC_PIN | BOARD_VBUS_ADC_PIN,
                  GPIO_ModeIN_Floating);
    R16_PIN_ANALOG_IE |= RB_PIN_ADC10_IE | RB_PIN_ADC11_IE;

    /* Buses remain idle until their owner starts a transaction. External
     * I2C pullups belong to 3V3; do not drive either line high push-pull. */
    GPIOB_ModeCfg(BOARD_I2C_SCL_PIN | BOARD_I2C_SDA_PIN,
                  GPIO_ModeIN_Floating);
    GPIOA_SetBits(BOARD_SPI_CS_PIN);
    GPIOA_ModeCfg(BOARD_SPI_CS_PIN, GPIO_ModeOut_PP_5mA);
    GPIOA_ModeCfg(BOARD_SPI_SCK_PIN | BOARD_SPI_MOSI_PIN |
                  BOARD_SPI_MISO_PIN, GPIO_ModeIN_Floating);
    /* PB14/15, PB22/23 and PA10/11 retain debug/boot/clock ownership. */
}
