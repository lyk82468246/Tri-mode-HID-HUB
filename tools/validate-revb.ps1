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
    Assert-Text $pins 'BOARD_USB_DEVICE_DP_PIN.*1u << 11'
    Assert-Text $pins 'BOARD_USB_HOST_DP_PIN.*1u << 13'
    Assert-Text $pins 'BOARD_USER_PIN.*1u << 18'
    Assert-Text $pins 'BOARD_INPUT_PGOOD_PIN.*1u << 16'
    Assert-Text $pins 'BOARD_UART3_RX_PIN.*1u << 4'
    Assert-Text $pins 'BOARD_UART0_RX_PIN.*1u << 4'
    Assert-Text $pins 'BOARD_I2C_SCL_PIN.*1u << 21'
    Assert-Text 'src/board.c' 'R16_PIN_ALTERNATE = RB_PIN_I2C'
    Assert-Text 'src/board_power.c' 'host_fault_latched'
    Assert-Text 'src/event_router_types.h' 'ROUTER_CONTROL_CLEAR_HOST_FAULT'
    Assert-Text 'src/event_router.c' 'BoardPower_ClearFault'
    Assert-Text 'src/ir_remote.c' 'IR_REMOTE_EDGE_CAPACITY       128u'
    Assert-Text 'src/ir_remote.c' 'IR_NEC_REPEAT_MARK'

    $projectFiles = @('.cproject', 'CH582M.wvproj')
    foreach($projectFile in $projectFiles) {
        foreach($excluded in @('CH58x_uart3.c', 'CH58x_pwm.c', 'CH58x_timer0.c', 'CH58x_spi0.c')) {
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
