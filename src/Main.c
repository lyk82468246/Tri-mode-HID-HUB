#include "CH58x_common.h"

#include "tmos_app.h"
#include "board.h"

int main(void)
{
    SetSysClock(CLK_SOURCE_PLL_60MHz);
    Board_Init();
    Firmware_Init();

    for(;;)
    {
        Firmware_Run();
    }
}
