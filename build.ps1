$ErrorActionPreference = "Stop"

$workspaceRoot = "d:\H723VGT6\H723VGT6"
$debugDir = Join-Path $workspaceRoot "fault_tolerence\Debug"
$linkerScript = Join-Path $workspaceRoot "fault_tolerence\STM32H723VGTX_FLASH.ld"
$gcc = "C:\Users\user\AppData\Local\stm32cube\bundles\gnu-tools-for-stm32\14.3.1+st.2\bin\arm-none-eabi-gcc.exe"

$includes = @(
    "-I../Core/Inc",
    "-I../Drivers/STM32H7xx_HAL_Driver/Inc",
    "-I../Drivers/STM32H7xx_HAL_Driver/Inc/Legacy",
    "-I../Middlewares/Third_Party/FreeRTOS/Source/include",
    "-I../Middlewares/Third_Party/FreeRTOS/Source/CMSIS_RTOS_V2",
    "-I../Middlewares/Third_Party/FreeRTOS/Source/portable/GCC/ARM_CM4F",
    "-I../Drivers/CMSIS/RTOS2/Include",
    "-I../Drivers/CMSIS/Device/ST/STM32H7xx/Include",
    "-I../Drivers/CMSIS/Include"
)

$cflags = @(
    "-mcpu=cortex-m7",
    "-std=gnu11",
    "-g3",
    "-DDEBUG",
    "-DUSE_PWR_LDO_SUPPLY",
    "-DUSE_HAL_DRIVER",
    "-DSTM32H723xx",
    "-c",
    "-O0",
    "-ffunction-sections",
    "-fdata-sections",
    "-Wall",
    "--specs=nano.specs",
    "-mfpu=fpv5-d16",
    "-mfloat-abi=hard",
    "-mthumb"
)

Push-Location $debugDir
try {
    Write-Host "Compiling modified sources..."
    & $gcc @cflags @includes "../Core/Src/main.c" -o "./Core/Src/main.o"
    & $gcc @cflags @includes "../Core/Src/mpu6050.c" -o "./Core/Src/mpu6050.o"
    & $gcc @cflags @includes "../Core/Src/stm32h7xx_hal_msp.c" -o "./Core/Src/stm32h7xx_hal_msp.o"

    Write-Host "Linking fault_tolerence.elf..."
    $linkArgs = @(
        "-o", "fault_tolerence.elf",
        "@objects.list",
        "-mcpu=cortex-m7",
        "-T$linkerScript",
        "--specs=nosys.specs",
        "-Wl,-Map=fault_tolerence.map",
        "-Wl,--gc-sections",
        "-static",
        "--specs=nano.specs",
        "-mfpu=fpv5-d16",
        "-mfloat-abi=hard",
        "-mthumb",
        "-Wl,--start-group",
        "-lc",
        "-lm",
        "-Wl,--end-group"
    )
    & $gcc @linkArgs
    Write-Host "Build complete: fault_tolerence.elf successfully generated!"
}
finally {
    Pop-Location
}
