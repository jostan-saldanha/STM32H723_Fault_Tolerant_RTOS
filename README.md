# STM32H723VGT6 Fault-Tolerant RTOS IMU System

A mission-critical, fault-tolerant embedded system running FreeRTOS on the **STM32H723VGT6** (ARM Cortex-M7 @ 550 MHz). It integrates an **MPU6050 / MPU6500 6-axis IMU** over hardware I2C with multi-level fault recovery, physical plausibility validation, supervisor heartbeat monitoring, and an independent hardware watchdog (IWDG).

---

## 1. Hardware Pinout & Wiring

### Sensor Wiring (MPU6050 / MPU6500)
| STM32 Pin | MPU6050 Pin | Function | Notes |
|---|---|---|---|
| **PB8** | SCL | I2C1 SCL | Hardware I2C (100 kHz Standard Mode) |
| **PB9** | SDA | I2C1 SDA | Hardware I2C (100 kHz Standard Mode) |
| **3V3** | VCC | Power | 3.3V power rail |
| **GND** | GND | Ground | Common Ground |
| **GND** | AD0 | Address Select | Sets I2C address to `0x68` |

### Telemetry / Serial Output
| STM32 Pin | Target | Description |
|---|---|---|
| **PB14** | USB-TTL RX | USART1 TX (115200 baud, 8N1) |
| **GND** | USB-TTL GND | Common Ground |

---

## 2. Using in STM32CubeIDE

**Does this code support STM32CubeIDE?**  
**Yes, natively.** The project was generated with STM32CubeMX and includes the Eclipse CDT project files (`.project`, `.cproject`, `.mxproject`) and the `.ioc` configuration file.

### How to Import & Build in STM32CubeIDE:
1. Open **STM32CubeIDE**.
2. Go to **File -> Import... -> General -> Existing Projects into Workspace**, then click **Next**.
3. Under **Select root directory**, browse and select the `fault_tolerence` folder:
   ```
   <repo-root>/fault_tolerence
   ```
4. Ensure the `fault_tolerence` project is checked in the Projects list.
5. Click **Finish**.
6. To build: Click the **Hammer icon** (Build) or press `Ctrl + B`.
7. To edit hardware/clocks: Double-click `fault_tolerence.ioc` to open the STM32CubeMX graphical pinout & clock tree configurator.
8. To flash/debug: Connect an ST-LINK to the SWD pins (SWDIO, SWCLK, GND, 3V3) and click the **Bug icon** (Debug) or **Play icon** (Run).

---

## 3. Using in Visual Studio Code

This repository includes preconfigured VS Code build and flash tasks in `.vscode/tasks.json` and a lightweight build script `build.ps1`.

### Prerequisites
1. **ARM GNU Toolchain (`arm-none-eabi-gcc`)**:
   Included with STM32CubeIDE or standalone ARM GNU Toolchain.
2. **STM32CubeProgrammer CLI (`STM32_Programmer_CLI.exe`)**:
   Default path: `C:\Program Files\STMicroelectronics\STM32Cube\STM32CubeProgrammer\bin\STM32_Programmer_CLI.exe`.
3. **Recommended VS Code Extensions**:
   - *C/C++* (ms-vscode.cpptools)
   - *Cortex-Debug* (marus25.cortex-debug)
   - *Serial Monitor* (ms-vscode.vscode-serial-monitor)

### Building the Project
- Press `Ctrl + Shift + B` and select **Build Firmware**, or execute in terminal:
  ```powershell
  powershell -ExecutionPolicy Bypass -File build.ps1
  ```
- This compiles modified source files and links `fault_tolerence/Debug/fault_tolerence.elf`.

### Flashing via USB-C DFU (No ST-LINK Required)
The WeAct STM32H723 board can be flashed directly through its onboard USB-C port:
1. Connect the board via USB-C to your PC.
2. Enter **DFU Bootloader Mode**:
   - Press and hold the **BOOT0** button.
   - Press and release the **NRST** (Reset) button.
   - Release the **BOOT0** button.
3. In VS Code, press `Ctrl + Shift + B` (runs default task **Flash Firmware (USB-C DFU)**).
   - This automatically executes `build.ps1` and uploads `fault_tolerence.elf` via `STM32_Programmer_CLI -c port=usb1 -w ... -v -s`.
4. Press the **NRST** button to start running the application.

### Viewing Telemetry
1. Connect a USB-UART adapter (e.g. CP2102/CH340) to **PB14 (TX)** and **GND**.
2. Open your serial monitor (VS Code Serial Monitor, PuTTY, or Tera Term) on the corresponding COM port:
   - **Baud Rate**: `115200`
   - **Data Bits**: `8`
   - **Parity**: `None`
   - **Stop Bits**: `1`
3. Output format (emitted every 2 seconds):
   ```text
   IMU A: X=24mg Y=-1001mg Z=181mg
   IMU G: X=0 Y=5 Z=-2 dps
   Tasks S:OK V:OK W:OK
   SYSTEM STATUS : HEALTHY
   ```

---

## 4. Software Architecture & Fault Tolerance

```
                           +----------------------+
                           | Hardware Watchdog    |
                           | (IWDG1 ~8s Timeout)  |
                           +----------^-----------+
                                      |
                      Serviced ONLY in HEALTHY / SAFE
                                      |
                           +----------+-----------+
                           |   Supervisor Task    | (Priority 4, 500ms)
                           | - Heartbeat Check    |
                           | - State Machine      |
                           | - Task Regeneration  |
                           +----------^-----------+
                                      |
                 +--------------------+--------------------+
                 |                                         |
     +-----------+-----------+                 +-----------+-----------+
     |      Sensor Task      |                 |    Validation Task    |
     |  (Priority 3, 100ms)  |                 |  (Priority 2, 250ms)  |
     | - I2C1 Read (100Hz)   |                 | - Plausibility Bounds |
     | - MPU6050/6500 Init   |=== Snapshot ===>| - Freshness (<1500ms) |
     | - Bus Recovery Hook   |                 | - Vector Norm Check   |
     +-----------^-----------+                 +-----------------------+
                 |
     +-----------+-----------+
     |   MPU6050 / MPU6500   |
     | (Hardware I2C1 Bus)   |
     +-----------------------+
```

### Key Fault-Tolerant Features:
1. **Bus Recovery (`I2C1_ClearBus`)**: Clocks SCL up to 16 times in GPIO open-drain mode on startup or lockup to free SDA if held low by a slave device, followed by generating an explicit STOP condition.
2. **Sensor Recovery**: Detects repeated I2C errors and automatically soft-resets the MPU and rebinds the peripheral. Supports both MPU6050 (`0x68`) and MPU6500 (`0x70`).
3. **Sensor Validation**: Verifies freshness (<1500ms) and checks physical limits ($0.2g \le \|a\| \le 3.2g$, $\|a_i\| \le 2.2g$, $\|\omega_i\| \le 275^\circ$/s).
4. **Heartbeat Supervisor**: Monitored tasks update tick counts. If a task stalls beyond 1500ms, the supervisor terminates and dynamically recreates the stalled FreeRTOS task.
5. **Hardware Watchdog (IWDG1)**: Prescaled to ~8s. Refreshed strictly by the supervisor task. If the supervisor hangs or FreeRTOS deadlocks, the hardware watchdog forces an MCU hard reset.

---

## 5. Repository Structure

```
.
├── .gitignore                   # Excludes build binaries, objects, logs, and temp images
├── .vscode/
│   └── tasks.json               # VS Code Build & DFU Flash tasks
├── build.ps1                    # Standalone PowerShell compilation script
├── README.md                    # Project documentation & guide
└── fault_tolerence/
    ├── .project                 # STM32CubeIDE project definition
    ├── .cproject                # STM32CubeIDE compiler and build config
    ├── .mxproject               # STM32CubeMX project definition
    ├── fault_tolerence.ioc       # STM32CubeMX graphical configuration file
    ├── STM32H723VGTX_FLASH.ld   # Flash linker script
    ├── Core/
    │   ├── Inc/                 # Application & driver headers
    │   └── Src/
    │       ├── main.c           # Hardware setup, bus clearing, FreeRTOS bootstrap
    │       ├── app.c            # Fault-tolerant RTOS tasks & state machine
    │       └── mpu6050.c        # I2C driver supporting MPU6050 and MPU6500
    ├── Drivers/                 # STM32H7 HAL drivers & CMSIS
    └── Middlewares/             # FreeRTOS kernel source code
```
