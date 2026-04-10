# Hardware + Peripheral Connections (Reference)

This document is a **reference** for the hardware/peripherals connected or assumed by this repo.

Where the repo does not explicitly define pin mappings (because libDaisy/DaisyPod defaults are used),
this document notes that clearly. It also includes “hardware reference” sections for onboard resources
that may not be driven in code yet.

## Platform
- Electrosmith **Daisy Seed** on **Daisy Pod** carrier.

## Naming convention (short)
For canonical names used in prompts and docs, see: `docs/HARDWARE_NAMING.md`.

## Pod built-ins (buttons + encoder are used by code via `DaisyPod`, pins abstracted)
These are actively read in `controls.cpp`, but the repo does not restate their pin numbers because they’re handled by the DaisyPod hardware abstraction.

### Pod Buttons
- Btn1 = `hw.button1` (Pod tactile switch 1)
- Btn2 = `hw.button2` (Pod tactile switch 2)
Source: `controls.cpp`

### Pod Encoder (Left encoder)
- LEnc  = `hw.encoder` increment (A/B)
- LClick = `hw.encoder.RisingEdge()` (encoder click)
Source: `controls.cpp`

> Note: Pod pin numbers for these controls are not defined in this repo; they are set by libDaisy’s DaisyPod mapping.

## External controls (explicit pins in repo)
### External Encoder (Right encoder)
Wiring (explicit in repo):
- REnc_A  -> Seed **D7**
- REnc_B  -> Seed **D8**
- RClick  -> Seed **D22** (also labeled A7/ADC7 on Seed silkscreen)
Electrical:
- Switch wired to **GND**, uses **internal pull-up**
Source: `controls.cpp` (`Controls_Init`)

### Existing Shift Button (Right shift)
Wiring (explicit in repo):
- RShift signal -> Seed **D9**
- Other side -> **GND**
Electrical:
- **internal pull-up**, **POLARITY_INVERTED**
Source: `controls.cpp` (`Controls_Init`)

## Left Shift Button (LShift)
Wiring (explicit in repo):
- LShift signal -> Pod **ADC1** pin = **A1 / D16**
- Other side -> **GND**
Electrical:
- Use as a **digital input** with **internal pull-up** (pressed = LOW), polarity inverted.
Source: `controls.cpp` (`Controls_Init`)

## Display (explicit pins in repo)
### OLED (SSD130x over I2C)
Configured in repo:
- I2C address: **0x3C**
- Peripheral: **I2C_1**
- Speed: **400 kHz**
Wiring (explicit in repo):
- SCL -> Seed **D11**
- SDA -> Seed **D12**
Source: `main.cpp` (`InitOled`)

## MIDI (used by code; pins not stated in repo)
- MIDI receive is started via: `hw.midi.StartReceive();`
Source: `main.cpp`
> Note: Physical pin/jack wiring is not stated in this repo (handled by DaisyPod/libDaisy defaults).

## Record input source selection
- The RECORD flow switches between `LINE IN` and `MICROPHONE` in UI/state code.
- The repo documents the logical source selection, but it does not restate additional pin-level analog routing here.
Source: `src/ui/ui_screen_record.cpp`, `main.cpp`

## SD card storage (used by code; pins not stated in repo)
Used modules:
- SDMMC (`per/sdmmc.h`), FatFS (`fatfs.h`, `ff.h`, `FatFSInterface`)
Initialization behavior:
- `SdmmcHandler::Config sd_cfg; sd_cfg.Defaults();`
Source: `ui_worker.cpp` (`EnsureSdMounted`)
> Note: SD pin mapping is not stated in this repo because SDMMC is initialized with libDaisy Defaults().

## Hardware reference (available on Daisy Pod)
These resources exist on the Daisy Pod hardware. The pin numbers below come from the Daisy Pod pinout
reference (not from this repo’s source files). If/when the code starts using them, cite the source file(s)
here and move them into the “used by code” sections above.

### Pod LEDs (2 × RGB)
LED1 (RGB)
- LED1_R -> **D20**
- LED1_G -> **D19**
- LED1_B -> **D18**

LED2 (RGB)
- LED2_R -> **D17**
- LED2_G -> **D24**
- LED2_B -> **D23**

Status:
- Currently **NOT** driven directly by this repo’s code (no LED set/update calls found).
- Pin mapping shown here is for hardware reference only.
