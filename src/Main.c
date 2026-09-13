#include "CH58x_common.h"

#include "tmos_app.h"

static void DebugUart_Init(void)
{
    GPIOA_SetBits(GPIO_Pin_9);
    GPIOA_ModeCfg(GPIO_Pin_8, GPIO_ModeIN_PU);
    GPIOA_ModeCfg(GPIO_Pin_9, GPIO_ModeOut_PP_5mA);
    UART1_DefInit();
}

int main(void)
{
    SetSysClock(CLK_SOURCE_PLL_60MHz);
    DebugUart_Init();
    Firmware_Init();

    for(;;)
    {
        Firmware_Run();
    }
}
