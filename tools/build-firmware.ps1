param(
    [string]$ToolchainBin = 'C:\MounRiver\MounRiver_Studio2\resources\app\resources\win32\components\WCH\Toolchain\RISC-V Embedded GCC\bin',
    [ValidateSet(100,500)][int]$UsbPowerMa = 100,
    [switch]$TestPattern
)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path $PSScriptRoot -Parent
Push-Location $projectRoot
try {
    $compiler = Join-Path $ToolchainBin 'riscv-none-embed-gcc.exe'
    if(!(Test-Path -LiteralPath $compiler)) { throw "Compiler not found: $compiler" }
    $project = Get-Content -Raw CH582M.wvproj | ConvertFrom-Json
    $config = $project.buildConfig.configurations[0]
    $testPatternValue = if($TestPattern) { 1 } else { 0 }
    $outDir = if($TestPattern) { "obj/revb-$UsbPowerMa-pattern" } else { "obj/revb-$UsbPowerMa" }
    New-Item -ItemType Directory -Force $outDir | Out-Null
    $flags = @('-march=rv32imac','-mabi=ilp32','-msmall-data-limit=8',
        '-std=gnu99','-Os','-g','-ffunction-sections','-fdata-sections',
        '-fno-common','-Wall','-Wextra',"-DBOARD_USB_MAX_POWER_MA=$UsbPowerMa",
        "-DCH582M_M1_TEST_PATTERN=$testPatternValue")
    $flags += $config.ccompiler.preprocessor.defined_symbols | ForEach-Object { "-D$_" }
    $flags += @('-IStdPeriphDriver/inc','-IRVMSIS','-IBLE/HAL/include','-IBLE/LIB','-Isrc')
    $excluded = $config.excludeResources | ForEach-Object { ($_ -replace '\$\{project\}/','') }
    $sources = @(Get-ChildItem src,BLE/HAL,StdPeriphDriver -Filter '*.c' |
        Sort-Object FullName | Where-Object {
            $relative = $_.FullName.Substring($projectRoot.Length + 1).Replace('\','/')
            $excluded -notcontains $relative
        })
    $sources += Get-Item Startup/startup_CH583.S
    $objects = @()
    foreach($source in $sources) {
        $object = Join-Path $outDir ($source.BaseName + '.o')
        & $compiler @flags -c $source.FullName -o $object
        if($LASTEXITCODE -ne 0) { throw "Compile failed: $source" }
        $objects += $object
    }
    $elf = "$outDir/CH582M.elf"
    & $compiler -march=rv32imac -mabi=ilp32 -nostartfiles --specs=nano.specs --specs=nosys.specs `
        -T Ld/Link.ld -LStdPeriphDriver -LBLE/LIB @objects -lISP583 -lCH58xBLE `
        '-Wl,--gc-sections' "-Wl,-Map=$outDir/CH582M.map" '-Wl,--print-memory-usage' -o $elf
    if($LASTEXITCODE -ne 0) { throw 'Link failed' }
    & (Join-Path $ToolchainBin 'riscv-none-embed-size.exe') $elf
    if($LASTEXITCODE -ne 0) { throw 'Size failed' }
} finally { Pop-Location }
