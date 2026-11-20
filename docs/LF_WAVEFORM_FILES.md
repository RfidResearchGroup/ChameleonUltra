# LF waveform capture and offline exports

These commands use the existing CLI requirements, without a plotting dependency:

```text
hw connect
lf generic capture -o private/capture.json
data waveform info -f private/capture.json
data waveform export -f private/capture.json -o private/samples.bin --format bin
data waveform export -f private/capture.json -o private/samples.csv --format csv
data waveform export -f private/capture.json -o private/samples.svg --format svg
```

Create the private output directory first. Existing files and symlinks are
refused. The current interactive parser splits on whitespace; use paths without
spaces. Capture actively energizes the 125 kHz field; it is not passive sniffing.
Raw samples can contain credential-related modulation, so keep captures private.

## Firmware compatibility

The host first checks capabilities. Provisional command 3042, proposed separately
in PR #544, supports `--samples 1` through `--samples 4000`. The host validates
the exact version, sample size/count, nominal rate, requested count, completeness
and continuity flags before publishing a `dma-complete` file.

Official older firmware uses command 3009, returns exactly 800 samples, and is
recorded as `legacy-unverified`. This host feature works independently of #544;
it does not claim to repair old firmware's sampling discontinuities. A failed or
malformed 3042 response is an error, never silently downgraded to a legacy capture.

Capture temporarily enters Reader mode when necessary, then restores and reads
back the original device mode before returning or saving a file. Existing Reader
mode is retained. Cleanup failure cannot turn a successful read into a successful
command. Cancellation stays cancellation; a secondary restoration failure is
attached to the primary exception as `restore_error` on Python 3.9 too.

## File and analysis boundaries

`chameleon-lf-waveform-v1` stores the byte sample array, count, format, nominal
125000 Hz rate, UTC timestamp, acquisition command and continuity provenance.
SHA-256 covers both the samples and the complete canonical metadata document.
Checksums detect corruption, not source forgery. Duplicate/unknown members,
oversized files, unsupported formats and inconsistent counts are rejected.

Files are created exclusively, flushed/fsynced, closed, and reread before success
is reported. A storage failure can leave a partial file; it is never described as
a valid capture and will not be overwritten on a retry.

Offline `info` reports min/max, mean, AC RMS and midpoint-threshold transitions.
These are signal statistics, not tag identification or a card decoder. CSV/SVG
time coordinates and durations are nominal sample coordinates; in particular,
they do not prove continuous time for `legacy-unverified` files.

BIN is a sample-only export and omits JSON metadata/checksums. CSV preserves
nominal sample indices/times; SVG is a static numeric waveform without scripts
or external resources. Offline commands require no connected device.

## Validation

```sh
cd software/script
python -m unittest discover -s tests -p "test_lf_waveform*.py" -v
```

Tests exercise malformed responses, mode restoration and cancellation, strict
file parsing and corruption checks, no-clobber/storage failures, numeric exports,
statistics and real CLI units. Hardware/noise/RF acceptance is recorded
separately from host tests and the new capture firmware's acceptance.
