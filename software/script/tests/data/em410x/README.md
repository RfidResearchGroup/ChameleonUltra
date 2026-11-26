EM410x receiver captures

These files contain the exact little-endian uint16 interval inputs read from the Chameleon GPIO ring. Each interval is measured in 125 kHz carrier periods and has already been clamped to 255 by the GPIO callback.

The files without an `-after` suffix were recorded with the original decoder. The matching `-after` files were recorded on hardware with the corrected decoder, using the same physical card or PM5 simulation ID. Both builds used identical temporary capture instrumentation. The diagnostic froze the first result and continued recording for 510 ms to retain complete frames. No ring drops or trace truncation occurred.

| Source | Synthetic EM4100 ID | Original live result | Fixed live result |
| --- | --- | --- | --- |
| Physical T5577, PM5 field off | 1020304050 | EM4100 at input 545, 330 ms | EM4100 at input 56, 30 ms |
| PM5 simulation controlled by Flipper | A1B2C3D4E5 | No tag in 762 inputs, 510 ms | EM4100 at input 99, 60 ms |

Times come from the device's 10 ms timer. `captures.json` preserves each recording's hash, interval count, live status, first result and replay offsets. Status 64 means a successful LF read; 65 means no tag. Type 103 is standard EM410x.

`physical-rf64.u16` contains 834 intervals from the physical T5577. `physical-rf64-after.u16` contains 846 intervals from the same card programmed with the same ID.

The complete `physical-rf64.u16` stream decodes with both versions. Starting at the real interval offset 337, the original decoder finds no frame in the remaining 497 inputs. The corrected decoder reads the expected ID after 208 inputs. The fixture retains the complete capture, and the test selects the starting offset.

The capture firmware was based on 8a5d730 with temporary memory recording immediately before decoder dispatch. The fixed build added the decoder reset. The diagnostic returned the recorded data over USB after RF stopped and is absent from the production fix. Ordinary reads without instrumentation were also compared separately.

`pm5-simulation-rf64.u16` was captured while the PM5 simulated synthetic ID A1B2C3D4E5 under Flipper control. It contains all 762 intervals from a live 510 ms no-tag read, with zero dropped or truncated inputs. Replaying this complete stream through the original decoder pipeline returns no result, matching the live status. The corrected decoder returns the expected standard EM4100 ID after 99 intervals.

`pm5-simulation-rf64-after.u16` contains 768 intervals from the matching successful live read with the fixed decoder. Replaying it through the fixed decoder returns the expected ID at input 99, matching the live result.

Across 54 recorded reads, every original pipeline replay matches the live first type, tag data and result interval index. The raw fixtures are unmodified copies of their recorded interval payloads.

Run `python3 software/script/tests/test_em410x_decoder.py` from the repository root to check all four recordings, the physical-card offset and generated RF/16, RF/32 and RF/64 inputs. `CC` selects the host C compiler.
