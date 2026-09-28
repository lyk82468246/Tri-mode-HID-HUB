#include "HAL.h"
#include "board_pins.h"
#include "board_config.h"
#include "board_power.h"
#include "usb_device.h"

static BoardPowerStatus g_power __attribute__((aligned(4)));
static uint32_t g_adc_deadline;
static uint32_t g_button_since;
static uint8_t g_adc_phase;
static uint8_t g_adc_channel;
static uint8_t g_button_candidate;

static uint8_t Due(uint32_t now, uint32_t deadline)
{
    return (int32_t)(now - deadline) >= 0;
}

static void SetChargerMode(uint8_t mode)
{
    if(mode == g_power.charger_mode) return;
    /* Never traverse ILIM (EN2=1,EN1=0) while changing USB modes. */
    if(mode == 3u)
    {
        GPIOB_SetBits(BOARD_CHARGER_EN1_PIN);
        GPIOB_SetBits(BOARD_CHARGER_EN2_PIN);
    }
    else
    {
        GPIOB_ResetBits(BOARD_CHARGER_EN2_PIN);
        if(mode == 1u) GPIOB_SetBits(BOARD_CHARGER_EN1_PIN);
        else GPIOB_ResetBits(BOARD_CHARGER_EN1_PIN);
    }
    g_power.charger_mode = mode;
}

static void SampleAdc(uint32_t now)
{
    if(!Due(now, g_adc_deadline)) return;
    if(g_adc_phase == 0u)
    {
        R8_ADC_CHANNEL = g_adc_channel ? BOARD_VBUS_ADC_CHANNEL :
                                        BOARD_BATTERY_ADC_CHANNEL;
        g_adc_phase = 1u;
        g_adc_deadline = now + 2u; /* settle mux, never spin */
    }
    else if(g_adc_phase == 1u)
    {
        R8_ADC_CONVERT = RB_ADC_START;
        g_adc_phase = 2u;
        g_adc_deadline = now + 8u; /* 5ms conversion deadline */
    }
    else
    {
        if(R8_ADC_CONVERT & RB_ADC_START)
        {
            R8_ADC_CONVERT = 0u;
            ++g_power.adc_timeout;
            g_power.adc_valid_mask &= (uint8_t)~(1u << g_adc_channel);
        }
        else
        {
            if(g_adc_channel) g_power.vbus_raw = R16_ADC_DATA & RB_ADC_DATA;
            else g_power.battery_raw = R16_ADC_DATA & RB_ADC_DATA;
            g_power.adc_valid_mask |= (uint8_t)(1u << g_adc_channel);
        }
        g_adc_channel ^= 1u;
        g_adc_phase = 0u;
        g_adc_deadline = now + 80u; /* 50ms */
    }
}

void BoardPower_Init(void)
{
    g_power = (BoardPowerStatus){0};
    g_power.host_requested = 1u;
    GPIOB_ResetBits(BOARD_USB_HOST_ENABLE_PIN |
                   BOARD_CHARGER_EN1_PIN | BOARD_CHARGER_EN2_PIN);
    R8_TKEY_CFG &= (uint8_t)~RB_TKEY_PWR_ON;
    R8_ADC_CTRL_DMA = 0u;
    /* External single-ended, buffered, -12dB, 3.2MHz. Raw values only:
     * divider/PGA/reference calibration must precede millivolt reporting. */
    R8_ADC_CFG = RB_ADC_POWER_ON | RB_ADC_BUF_EN;
    g_adc_phase = 0u;
    g_adc_channel = 0u;
    g_adc_deadline = TMOS_GetSystemClock() + 16u;
    g_button_candidate = 0u;
    g_button_since = TMOS_GetSystemClock();
}

void BoardPower_Process(void)
{
    uint32_t now = TMOS_GetSystemClock();
    uint8_t pressed = GPIOB_ReadPortPin(BOARD_USER_PIN) == 0u;
    uint8_t fault = GPIOB_ReadPortPin(BOARD_HOST_FAULT_PIN) == 0u;
    uint8_t permitted;
    uint8_t mode = 0u;

    g_power.input_good = GPIOB_ReadPortPin(BOARD_INPUT_PGOOD_PIN) == 0u;
    g_power.charging = GPIOB_ReadPortPin(BOARD_CHARGING_PIN) == 0u;
    if(fault && !g_power.host_fault_latched)
    {
        g_power.host_fault_latched = 1u;
        ++g_power.fault_count;
    }
    if(UsbDevice_IsSuspended()) mode = 3u;
#if BOARD_USB_MAX_POWER_MA == 500u
    else if(g_power.input_good && UsbDevice_IsReady()) mode = 1u;
#endif
    permitted = ((mode == 1u) ||
                 (!g_power.input_good && BOARD_BATTERY_PERIPHERALS_ALLOWED)) &&
                !UsbDevice_IsSuspended();
    g_power.host_enabled = permitted && g_power.host_requested &&
                           !g_power.host_fault_latched;
    /* Shed loads before reducing the input allowance. */
    if(!g_power.host_enabled) GPIOB_ResetBits(BOARD_USB_HOST_ENABLE_PIN);
    SetChargerMode(mode);
    if(g_power.host_enabled) GPIOB_SetBits(BOARD_USB_HOST_ENABLE_PIN);

    if(pressed != g_button_candidate)
    {
        g_button_candidate = pressed;
        g_button_since = now;
    }
    else if(pressed != g_power.user_pressed &&
            Due(now, g_button_since + 32u)) /* 20ms */
    {
        g_power.user_pressed = pressed;
        if(pressed) ++g_power.user_press_count;
    }
    SampleAdc(now);
}

void BoardPower_RequestHost(uint8_t enabled)
{
    g_power.host_requested = enabled != 0u;
}

uint8_t BoardPower_ClearFault(void)
{
    if(g_power.host_enabled ||
       !GPIOB_ReadPortPin(BOARD_HOST_FAULT_PIN)) return 0u;
    g_power.host_fault_latched = 0u;
    return 1u;
}

uint8_t BoardPower_HostEnabled(void) { return g_power.host_enabled; }
void BoardPower_GetStatus(BoardPowerStatus *status)
{
    if(status) *status = g_power;
}
