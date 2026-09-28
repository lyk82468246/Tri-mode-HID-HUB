param(
    [switch]$Build
)

$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path $PSScriptRoot -Parent
Push-Location $projectRoot
try {
    $failures = New-Object System.Collections.Generic.List[string]

    function Assert-File([string]$path) {
        if(!(Test-Path -LiteralPath $path)) {
            $failures.Add("missing file: $path")
        }
    }

    function Assert-Text([string]$path, [string]$pattern) {
        if(!(Select-String -LiteralPath $path -Pattern $pattern -Quiet)) {
            $failures.Add("missing '$pattern' in $path")
        }
    }

    @(
        'src/board_pins.h',
        'src/board.c',
        'src/board_power.c',
        'src/uart_input.c',
        'src/irda_link.c',
        'src/ir_remote.c',
        'src/board_bus.c',
        'docs/hardware/pin-allocation-revb.csv',
        'docs/revb-firmware-test-plan.md'
    ) | ForEach-Object { Assert-File $_ }

    $pins = 'src/board_pins.h'
    $hardwareRows = @(Import-Csv -LiteralPath 'docs/hardware/pin-allocation-revb.csv')
    if($hardwareRows.Count -ne 49) {
        $failures.Add("hardware pin CSV must contain 49 rows, found $($hardwareRows.Count)")
    }
    $pinExpectations = @(
        @{ Net = 'USB_DEV_DP_MCU'; Silicon = 'PB11'; Macro = 'BOARD_USB_DEVICE_DP_PIN'; Bit = 11 },
        @{ Net = 'USB_DEV_DN_MCU'; Silicon = 'PB10'; Macro = 'BOARD_USB_DEVICE_DM_PIN'; Bit = 10 },
        @{ Net = 'USB_HOST_DP_MCU'; Silicon = 'PB13'; Macro = 'BOARD_USB_HOST_DP_PIN'; Bit = 13 },
        @{ Net = 'USB_HOST_DN_MCU'; Silicon = 'PB12'; Macro = 'BOARD_USB_HOST_DM_PIN'; Bit = 12 },
        @{ Net = 'HOST_EN'; Silicon = 'PB6'; Macro = 'BOARD_USB_HOST_ENABLE_PIN'; Bit = 6 },
        @{ Net = 'HOST_FAULT_N'; Silicon = 'PB5'; Macro = 'BOARD_HOST_FAULT_PIN'; Bit = 5 },
        @{ Net = 'CHG_N'; Silicon = 'PB9'; Macro = 'BOARD_CHARGING_PIN'; Bit = 9 },
        @{ Net = 'CHG_EN1'; Silicon = 'PB8'; Macro = 'BOARD_CHARGER_EN1_PIN'; Bit = 8 },
        @{ Net = 'CHG_EN2'; Silicon = 'PB17'; Macro = 'BOARD_CHARGER_EN2_PIN'; Bit = 17 },
        @{ Net = 'INPUT_PGOOD_N'; Silicon = 'PB16'; Macro = 'BOARD_INPUT_PGOOD_PIN'; Bit = 16 },
        @{ Net = 'USER_N'; Silicon = 'PB18'; Macro = 'BOARD_USER_PIN'; Bit = 18 },
        @{ Net = 'VBAT_SENSE'; Silicon = 'PA6'; Macro = 'BOARD_BATTERY_ADC_PIN'; Bit = 6 },
        @{ Net = 'USB_C_VBUS_ADC'; Silicon = 'PA7'; Macro = 'BOARD_VBUS_ADC_PIN'; Bit = 7 },
        @{ Net = 'RS232_RX_TTL'; Silicon = 'PA8'; Macro = 'BOARD_UART1_RX_PIN'; Bit = 8 },
        @{ Net = 'RS232_TX_TTL'; Silicon = 'PA9'; Macro = 'BOARD_UART1_TX_PIN'; Bit = 9 },
        @{ Net = 'UART_TTL_RX'; Silicon = 'PA4'; Macro = 'BOARD_UART3_RX_PIN'; Bit = 4 },
        @{ Net = 'UART_TTL_TX'; Silicon = 'PA5'; Macro = 'BOARD_UART3_TX_PIN'; Bit = 5 },
        @{ Net = 'IRDA_UART_RX'; Silicon = 'PB4'; Macro = 'BOARD_UART0_RX_PIN'; Bit = 4 },
        @{ Net = 'IRDA_UART_TX'; Silicon = 'PB7'; Macro = 'BOARD_UART0_TX_PIN'; Bit = 7 },
        @{ Net = 'IRDA_CODEC_EN'; Silicon = 'PB3'; Macro = 'BOARD_IRDA_ENABLE_PIN'; Bit = 3 },
        @{ Net = 'IRDA_MODE'; Silicon = 'PB2'; Macro = 'BOARD_IRDA_MODE_PIN'; Bit = 2 },
        @{ Net = 'IRDA_SD'; Silicon = 'PB19'; Macro = 'BOARD_IRDA_SHUTDOWN_PIN'; Bit = 19 },
        @{ Net = 'IR_REMOTE_RX'; Silicon = 'PB1'; Macro = 'BOARD_IR_RX_PIN'; Bit = 1 },
        @{ Net = 'IR_REMOTE_TX'; Silicon = 'PB0'; Macro = 'BOARD_IR_TX_PIN'; Bit = 0 },
        @{ Net = 'I2C_SCL'; Silicon = 'PB21'; Macro = 'BOARD_I2C_SCL_PIN'; Bit = 21 },
        @{ Net = 'I2C_SDA'; Silicon = 'PB20'; Macro = 'BOARD_I2C_SDA_PIN'; Bit = 20 },
        @{ Net = 'SPI_CS_N'; Silicon = 'PA12'; Macro = 'BOARD_SPI_CS_PIN'; Bit = 12 },
        @{ Net = 'SPI_SCK'; Silicon = 'PA13'; Macro = 'BOARD_SPI_SCK_PIN'; Bit = 13 },
        @{ Net = 'SPI_MOSI'; Silicon = 'PA14'; Macro = 'BOARD_SPI_MOSI_PIN'; Bit = 14 },
        @{ Net = 'SPI_MISO'; Silicon = 'PA15'; Macro = 'BOARD_SPI_MISO_PIN'; Bit = 15 }
    )
    foreach($expectation in $pinExpectations) {
        $rows = @($hardwareRows | Where-Object { $_.net -eq $expectation.Net })
        if($rows.Count -ne 1) {
            $failures.Add("hardware pin CSV must have exactly one row for $($expectation.Net)")
            continue
        }
        if($rows[0].silicon -notmatch ('^' + [regex]::Escape($expectation.Silicon))) {
            $failures.Add("$($expectation.Net) is $($rows[0].silicon), expected $($expectation.Silicon)")
        }
        Assert-Text $pins ([regex]::Escape($expectation.Macro) + '.*1u << ' + $expectation.Bit)
    }
    Assert-Text $pins 'BOARD_USB_DEVICE_DP_PIN.*1u << 11'
    Assert-Text $pins 'BOARD_USB_DEVICE_DM_PIN.*1u << 10'
    Assert-Text $pins 'BOARD_USB_HOST_DP_PIN.*1u << 13'
    Assert-Text $pins 'BOARD_USB_HOST_DM_PIN.*1u << 12'
    Assert-Text $pins 'BOARD_USB_HOST_ENABLE_PIN.*1u << 6'
    Assert-Text $pins 'BOARD_HOST_FAULT_PIN.*1u << 5'
    Assert-Text $pins 'BOARD_CHARGING_PIN.*1u << 9'
    Assert-Text $pins 'BOARD_CHARGER_EN1_PIN.*1u << 8'
    Assert-Text $pins 'BOARD_CHARGER_EN2_PIN.*1u << 17'
    Assert-Text $pins 'BOARD_USER_PIN.*1u << 18'
    Assert-Text $pins 'BOARD_INPUT_PGOOD_PIN.*1u << 16'
    Assert-Text $pins 'BOARD_BATTERY_ADC_PIN.*1u << 6'
    Assert-Text $pins 'BOARD_VBUS_ADC_PIN.*1u << 7'
    Assert-Text $pins 'BOARD_UART1_RX_PIN.*1u << 8'
    Assert-Text $pins 'BOARD_UART1_TX_PIN.*1u << 9'
    Assert-Text $pins 'BOARD_UART3_RX_PIN.*1u << 4'
    Assert-Text $pins 'BOARD_UART3_TX_PIN.*1u << 5'
    Assert-Text $pins 'BOARD_UART0_RX_PIN.*1u << 4'
    Assert-Text $pins 'BOARD_UART0_TX_PIN.*1u << 7'
    Assert-Text $pins 'BOARD_IRDA_ENABLE_PIN.*1u << 3'
    Assert-Text $pins 'BOARD_IRDA_MODE_PIN.*1u << 2'
    Assert-Text $pins 'BOARD_IRDA_SHUTDOWN_PIN.*1u << 19'
    Assert-Text $pins 'BOARD_IR_RX_PIN.*1u << 1'
    Assert-Text $pins 'BOARD_IR_TX_PIN.*1u << 0'
    Assert-Text $pins 'BOARD_I2C_SCL_PIN.*1u << 21'
    Assert-Text $pins 'BOARD_I2C_SDA_PIN.*1u << 20'
    Assert-Text $pins 'BOARD_SPI_CS_PIN.*1u << 12'
    Assert-Text $pins 'BOARD_SPI_SCK_PIN.*1u << 13'
    Assert-Text $pins 'BOARD_SPI_MOSI_PIN.*1u << 14'
    Assert-Text $pins 'BOARD_SPI_MISO_PIN.*1u << 15'
    Assert-Text 'src/board.c' 'R16_PIN_ALTERNATE = RB_PIN_I2C'
    Assert-Text 'src/board_power.c' 'host_fault_latched'
    Assert-Text 'src/event_router_types.h' 'ROUTER_CONTROL_CLEAR_HOST_FAULT'
    Assert-Text 'src/event_router.c' 'BoardPower_ClearFault'
    Assert-Text 'src/ir_remote.c' 'IR_REMOTE_EDGE_CAPACITY       128u'
    Assert-Text 'src/ir_remote.c' 'IR_NEC_REPEAT_MARK'
    Assert-Text 'src/ir_remote.c' 'else if\(!level && IrRemote_InRange\(duration, 1800u, 2800u\)'
    Assert-Text 'src/ir_remote.c' 'if\(level && IrRemote_InRange\(duration, 350u, 800u\)\)'
    Assert-Text 'src/ir_remote.c' 'g_ir_edge_overrun_reported'
    Assert-Text 'src/ir_remote.c' 'StaticSpscRing_Clear\(&g_ir_edge_ring\)'
    Assert-Text 'src/board_bus.c' 'BoardBus_ConfigureSpi'
    Assert-Text 'src/board_bus.c' 'SPI0_MasterDefInit'
    Assert-Text 'docs/hardware/pin-allocation-revb.md' '固件已按本表迁移'
    Assert-Text 'CH582M.wvproj' '"mcu": "CH582M"'
    Assert-Text 'src/tmos_app.c' 'FIRMWARE_SERVICE_PERIOD_MS  2u'

    $projectFiles = @('.cproject', 'CH582M.wvproj')
    $requiredExcluded = @('CH58x_usbhostClass.c', 'CH58x_usbhostBase.c',
                          'CH58x_usb2dev.c', 'CH58x_usbdev.c',
                          'CH58x_uart2.c',
                          'CH58x_timer3.c', 'CH58x_timer2.c',
                          'CH58x_timer1.c', 'CH58x_adc.c')
    foreach($projectFile in $projectFiles) {
        foreach($excluded in $requiredExcluded) {
            $escaped = [regex]::Escape($excluded)
            if(!(Select-String -LiteralPath $projectFile -Pattern $escaped -Quiet)) {
                $failures.Add("$projectFile must exclude $excluded")
            }
        }
        foreach($excluded in @('CH58x_uart3.c', 'CH58x_pwm.c', 'CH58x_timer0.c', 'CH58x_spi0.c',
                              'CH58x_i2c.c', 'CH58x_usb2hostBase.c')) {
            $escaped = [regex]::Escape($excluded)
            if(Select-String -LiteralPath $projectFile -Pattern $escaped -Quiet) {
                $failures.Add("$projectFile still excludes $excluded")
            }
        }
    }

    $sourceFiles = @(Get-ChildItem src -File -Include '*.c','*.h')
    if($sourceFiles.Count -gt 0) {
        $heapUse = Select-String -Path $sourceFiles.FullName `
            -Pattern '(?<![A-Za-z0-9_])(malloc|free)\s*\('
        if($heapUse) {
            $failures.Add('application source contains malloc/free')
        }

        $blockingUse = Select-String -Path $sourceFiles.FullName `
            -Pattern '(?<![A-Za-z0-9_])(mDelaymS|mDelayuS|USB2HostTransact|U2HostCtrlTransfer)\s*\('
        if($blockingUse) {
            $locations = ($blockingUse | ForEach-Object { $_.Path + ':' + $_.LineNumber }) -join ', '
            $failures.Add("application source calls a blocking SDK helper: $locations")
        }
    }

    & git diff --check
    if($LASTEXITCODE -ne 0) {
        $failures.Add('git diff --check failed')
    }

    if($failures.Count -gt 0) {
        $failures | ForEach-Object { Write-Error $_ }
        exit 1
    }

    Write-Host 'Rev B static validation: PASS'
    if($Build) {
        & "$PSScriptRoot/build-firmware.ps1" -UsbPowerMa 100
        & "$PSScriptRoot/build-firmware.ps1" -UsbPowerMa 500
        if($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
    }
}
finally {
    Pop-Location
}
