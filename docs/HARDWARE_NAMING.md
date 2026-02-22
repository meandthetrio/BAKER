# Hardware Naming Convention (Canonical)

This file defines the canonical names used in prompts, docs, and (eventually) code IDs.

## Controls (prompt names)

### Encoders
- **LEnc**   = Pod encoder (built-in)
- **LClick** = Pod encoder click
- **REnc**   = External encoder
- **RClick** = External encoder click

### Shift buttons
- **LShift** = Shift button on Pod **ADC1** pin (**A1 / D16**) (new)
- **RShift** = Shift button on Seed **D9** (existing)

### Pod buttons
- **Btn1** = Pod tactile switch 1
- **Btn2** = Pod tactile switch 2

## LEDs
- **LED1** = Pod RGB LED 1
- **LED2** = Pod RGB LED 2

Optional per-channel naming:
- `LED1_R`, `LED1_G`, `LED1_B`
- `LED2_R`, `LED2_G`, `LED2_B`

## Rules (to keep everything consistent)
1) These names are the only names used in future prompts and docs.
2) “Shift” always means a modifier button (held state).
3) “Click” always means an encoder push-switch.
4) If a control is later remapped, update `docs/HARDWARE_CONNECTIONS.md` but KEEP the canonical name stable.
