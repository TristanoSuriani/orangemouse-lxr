# LXR Web: the LXR V2 firmware in the browser

This folder builds the LXR V2 firmware as a browser app. The firmware's own C sources (sound engine,
sequencer, menus, kit/pattern/project storage) are compiled to WebAssembly with
[Emscripten](https://emscripten.org) and run in an AudioWorklet. A thin port layer replaces the
STM32 hardware, and the page draws the display, buttons, LEDs, pots and faders.

## Building

You need Emscripten, CMake 3.21+, Ninja and Python 3.

```sh
# once: install and activate Emscripten (https://emscripten.org/docs/getting_started/downloads.html)
source /path/to/emsdk/emsdk_env.sh

emcmake cmake -S web -B web/build -G Ninja
cmake --build web/build
```

This writes `web/app/lxr.wasm`. Serve `web/app/` over HTTP. Audio worklets need `localhost` or HTTPS,
so opening `index.html` as a file doesn't work:

```sh
python3 -m http.server 8000 -d web/app
```

Then open <http://localhost:8000> and click **Switch on**. Tested in Chrome. MIDI in/out needs a browser
with Web MIDI (Chrome, Edge, Firefox).

## Using it

- **Buttons**: click to press. Hold the mouse button to hold a button, or **Alt-click** to latch it down,
  e.g. Copy or a step while you press another button. Click it again to release it. On a touch screen you can
  hold several at once.
- **Keyboard**: `Shift` = Shift, `Space` = Start/Stop, `←`/`→` turn the data encoder, `Enter` pushes it.
- **Pots and encoder**: drag up/down (or left/right) or use the scroll wheel. The four pots are endless, as on
  the hardware. Click the encoder without dragging to push it.
- **Faders**: the six voice volume faders (Drum 1-3, Snare, Cymbal, Hihat).
- **SD card**: the firmware stores kits, patterns and projects on a 16 MB virtual SD card kept in the browser
  (IndexedDB). It formats the card the first time. The firmware autosaves kit and pattern changes
  2 seconds after the last edit, but not while the sequencer runs. **Export SD card** downloads the card image,
  **Import SD card** restores an exported image and **Erase SD card** starts over.
- **Output**: both stereo outputs of the LXR are mixed to the browser's stereo output.
- **MIDI**: MIDI in is how a sequencer plays the studio. The MIDI device (Alt+1) sets which channel plays which
  part (by default 1-7 the LXR's voices, 8-11 VA, 12-13 Dexed, 14-15 Plaits, 16 Rings). MIDI clock, Start, Continue
  and Stop drive the LXR's sequencer and the mixer's gates, and set the tempo. CCs other than 21-28 (a knob box's)
  move the parameters of the part the channel plays, and pitch bend and program change reach it too (`docs/MIDI_CC.md`);
  on the LXR's channels they reach its firmware by its own settings. MIDI out sends what the LXR's firmware sends.

## How the port works

| Hardware                                   | In the browser (`web/port/`)                                                        |
|--------------------------------------------|-------------------------------------------------------------------------------------|
| Startup, clocks, `main()` loop, interrupts | `web_main.c`: boots like `main.c`; `web_render()` runs one main loop pass per 64-sample block and the 4 kHz / 1 kHz timer interrupts for the time the block covers |
| I2S codecs, DMA                            | `web_io_stubs.c`: the audio worklet reads `dma_buffer`/`dma_buffer2` after each block |
| Peripheral registers (GPIO, USART, ...)    | `lxr_web_port.h`: register blocks live in ordinary arrays, so register access still works (MIDI on USART3, jack switches, page-down button and LED) |
| Shift-register buttons and LEDs (SPI)      | `web_hal.c`: `HAL_SPI_Receive`/`Transmit` exchange the button and LED bits with the page |
| HD44780 display (`lcd.c`)                  | `web_lcd.c`: a model of the display controller (DDRAM, CGRAM); `app/lcd.js` draws it |
| Encoder (`encoder.c`)                      | `web_encoder.c`                                                                      |
| Endless pots and faders (ADC)              | `web_main.c` writes the two pot wipers and the fader values into `adc_buf`          |
| SD card (SDMMC)                            | `web_sdcard.c`: a FatFs disk in memory; the page stores changed sectors in IndexedDB |
| USB-MIDI                                   | not available; browser MIDI uses the DIN MIDI path                                  |
| Cortex-M DSP instructions                  | portable C versions in `lxr_web_port.h`                                              |
| Integer division by zero returns 0         | `tools/arm_division.py` rewrites the divisions in `lxr.wasm` to call `web_arm_division.c` (WebAssembly traps otherwise) |

The firmware sources other than the hardware drivers listed in `web/CMakeLists.txt` are compiled
unchanged.
