# Independent A/B button input

Each physical button now has its own raw level, debounce clock, accepted press
time and repeating debounce timer. A release records the button, duration and
short/long classification in a bounded FIFO for the main loop.

For example, hold A from 0 to 1200 ms and B from 900 to 1300 ms with a 1000 ms
long-press threshold. A must produce a long action and B a short action even if
the main loop consumes both releases together. The previous shared press clock
and shared long-press flag could classify A using B's clock or classification.
Two edges inside the same 50 ms debounce interval could also overwrite the
single shared timer context.

## Behavior and integration

- Both keys require 50 ms without a level change; a 10 ms repeating timer polls
  only while that particular key is debouncing. Both timers stop when idle.
- Duration uses the accepted physical edges, so delayed debounce callbacks do
  not inflate the measured hold. The existing configurable threshold is read
  when releasing a key. Equality remains a short press (`duration > threshold`
  is long).
- Tick arithmetic uses the Nordic RTC counter mask, including a counter wrap.
  As before, duration measurement does not distinguish holds spanning multiple
  full RTC counter periods.
- Physical `BUTTON_2` maps to A and `BUTTON_1` to B, preserving the existing
  board mapping. Actions are dispatched only by the main loop.
- Short and long functions are selected independently. Disabling the short
  function no longer disables detection of a configured long function.
- A button already held at initialization must be released before a fresh click
  can trigger an action.
- Dismissing the battery overlay consumes queued clicks and suppresses both
  held keys until they are stably released. A debounced edge interrupting
  shutdown similarly consumes the wake gesture.
- GPIO callbacks, timer callbacks and main-loop FIFO access use the SDK's
  critical-region API. No function pointers or GPIO context are shared between
  the two button timers.
- The FIFO holds 16 releases. If the main loop is blocked long enough to fill
  it, new releases are dropped, old records retain their order, and the
  saturating `dropped_events` counter records overload.

No command IDs, stored settings layout, slot data or CLI syntax are changed.
Existing `hw settings btnpress` and `hw settings longpressthreshold` commands
continue to configure the actions and threshold.

## Native regression tests

The state machine has no Nordic SDK dependency. Run the actual implementation
with a native GCC or Clang:

```sh
cc -std=c11 -Wall -Wextra -Werror -g \
  -fsanitize=address,undefined -fno-omit-frame-pointer \
  -I firmware/application/src \
  firmware/application/src/button_input.c \
  firmware/application/tests/test_button_input.c \
  -o /tmp/test_button_input
/tmp/test_button_input
```

The 20 regression scenarios cover independent clocks and both release orders,
simultaneous debounce, bounce on either key, missed/coalesced GPIO edges,
glitches, exact and configurable thresholds, delayed callbacks, RTC wrapping,
boot-held keys, delayed consumers, FIFO wrapping/overload/recovery, suppression
during battery dismissal and shutdown, and invalid key IDs.

The `Button input tests` workflow runs the same suite with GCC and Clang and
address/undefined-behavior sanitizers on pushes and pull requests. The existing
firmware pipeline compiles both Ultra and Lite.

## Hardware acceptance

Native tests exercise the real state machine, not physical GPIO, Nordic timer
scheduling or real power/LED behavior. Build success also does not establish
physical button acceptance. These checks remain manual until recorded:

1. Verify individual short/long A and B actions on a device.
2. Hold A long, tap B short, and release in both orders; repeat with A/B swapped.
3. Disable A's short action, configure its long action, and verify the long
   action still runs. Repeat for B and restore the original settings afterward.
4. Use a nondefault long-press threshold and test both sides of the boundary.
5. Dismiss the battery overlay while holding the other key; release both and
   confirm no extra slot action occurs.
6. Interrupt shutdown, release the wake gesture, and verify a subsequent fresh
   click works.
7. Repeat on battery and USB power, and check wake-from-sleep behavior and Lite.

No physical-button or power-consumption measurement is claimed by the native
regression suite.
