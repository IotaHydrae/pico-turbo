# Measurement traps

> Each of these has produced a wrong conclusion in this project; they are kept because
> the wrong conclusion looked exactly like a result.

## Transport and collection

- **A log written while no host has the port open is discarded, not buffered.** A reader
  that starts late can report a run that never happened, or miss one that did. Attach the
  reader before flashing, and judge a repeated run by the counter the application prints
  rather than by the lines that arrived. An application still running from the previous
  flash answers first otherwise, and the log reads exactly like the current run succeeded.
- **The reader's USB handshake must be retried.** The SDK decides "is a host there" from
  CDC line coding + DTR and drops output when there is none; one
  `[Errno 110] Operation timed out` turned a 30-run soak into an empty log while the
  counter kept incrementing. Re-assert the handshake while no data has arrived yet.
- **Do not leave the core halted.** A stopped core takes the bootrom's USB down with it,
  the board disappears from `lsusb`, and the next flash reports "no accessible RP-series
  devices". End every session with a reset or a resume.

## Frequency and stability

- **A frequency the PLL cannot hit exactly is not a failure.** The SDK leaves the clock
  where it was and reports nothing, so a "failed" point may be a point that was never
  tried: 550 and 560 MHz looked like instability until the run's own state line showed
  the clock had not moved. Check reachability before a long run — three minutes per point
  is an expensive way to learn the PLL cannot hit a number.
- **CoreMark prints "Errors detected" for a run shorter than ten seconds** (its
  reporting rules require it), which is not a result about the chip. A fixed iteration
  count that is right at 150 MHz is under ten seconds at 500 MHz; scale iterations with
  the clock.
- **Verify one point end to end before looping.** Build it, read the flash back and
  compare, and read the state line the run prints. This happened twice: once a whole
  official-Pico-2 sweep died in the flashing step, once a ladder point left the board
  locked up — both were the first point's failure, discovered after the loop.
- **A build whose flash divider is too fast can brick a board past what software can
  fix.** The divider lives in boot stage 2, so every reset re-runs it and fails the same
  way: DIV 4 at 520 MHz (a 130 MHz flash clock) left the core in lockup, unrescuable by
  any debugger reset, and it needed the BOOTSEL button. Keep the button reachable.
- **A chip in lockup needs a reset before anything can be flashed into it**, and
  openocd's flash driver says so in its own words ("failed to call reset core state").
  That is the state the chip is in, not a fault of the tool.

## Reading and preserving state

- **`openocd program ... verify` can report "Verified OK" after writing only part of an
  image**, leaving the rest erased. Erase first, then program, then read the flash back
  and compare; a board running a stale image looks like new firmware misbehaving.
- **Reading the chip's state over SWD halts it**, which pauses the sample watchdog (the
  SDK arms it with pause-on-debug), and openocd's resume of the RP2350 core pair can
  fail, leaving the chip stopped. The console is the channel that does not disturb the
  measurement; a debug reset after reading is the least bad alternative.
- **The watchdog scratch registers survive a debug reset but not a rescue-config reset**,
  so a recovery that uses the latter also throws away the search's memory of what hung.

## Comparing runs

- **Compare like with like.** An earlier version of the flash record compared a
  fifty-run dual-core soak with a ten-run one and called the difference a divider effect.
  Both divider groups were later re-measured through the same flow in one session. A row
  without its run count and flow is not a comparison.
- **Name the compiler.** A 5.30 % multiplier sits between GCC releases and is constant
  across clock and core count, so an absolute score without its compiler is not
  reproducible; see [toolchain-validity.md](toolchain-validity.md).

## Related

- [../flash-divider.md](../flash-divider.md) — the divider rule these traps are about
- [clock-ceilings.md](clock-ceilings.md) — where several of them were hit
