# CPU Benchmark Log

A running record of audio-callback CPU measurements, anchored to the build that
produced them. Append a new entry per build of interest; never edit old entries
(they are point-in-time facts).

## How to read these

- Values are **`now / peak`** as **% of the per-block budget** (one audio block
  period; 100% = the deadline, over = a late callback / dropout).
- **`now`** = current/most-recent block. **`peak`** = worst block since the last
  peak-reset; peaks latch across the whole session, so an idle `now` can sit next
  to a `peak` left over from a prior stress test.
- See `docs/diagnostics.md` for what each metric means and how to open the overlay.

## Reference stress test ("limit test")

The deliberate worst case we harden against (not typical playing):

- Pad-type sample, tuned **+24 semitones** (4× playback ratio).
- Generous **~20% loop seam** on each side (loop-seam crossfade branch active).
- Exponential envelope, **attack & release both > 1000 ms** (voices stay alive
  long → pool fills → stealing happens *during the attack stage*).
- **All FX on** (sat + EQ + delay + reverb).
- **Rapid 5-note re-slams** — forces continuous voice stealing on attack-stage voices.

Goal: keep `callback peak` safely under 100 even here, so no end user can ever
make the instrument click.

---

## Entries

### 2026-06-20 — `207c097`

- **Commit:** `207c097` ("perf: note-on cap, smoothstep steal fade, mono process
  filter, EQ neutral gate, poly 8"), on `2d8953c`.
- **Working tree:** clean (measured on this build). Changes vs `2d8953c`:
  - polyphony max 10 → 8
  - per-block note-on cap (`kMaxNoteOnsPerBlock = 2`) + deferral
  - smoothstep-shaped steal fade-out
  - global process filter made mono (was stereo)
  - EQ stage gated off when neutral (flat tilt / `eq_on` false)

| Metric | Idle (no MIDI), `now` | Limit test, `peak` |
|---|---|---|
| callback total | 43 | 100 |
| pre-voice | 6 | 19 |
| voice render | 3 | 32 |
| fx total | 33 | 48 |
| events (note-on handling) | 0 | 13 |
| steal (fade-out render) | 0 | 15 |
| fetch (sample read) | 0 | 10 |

Notes:
- `LATE` count was 0 at `callback peak ≈ 100` (right at the edge, not over).
- For comparison, the clean **`2d8953c` baseline** (poly 10, no cap) reached
  `callback peak 103` with late callbacks on the same limit test — the WIP cap is
  what brought the edge down to ~100.
- `events`, `steal`, `fetch` are all transient (0 at idle) and peak *together* at
  the slam instant — that co-incident stack is what sets the 100.
- `fx total` idle `now = 33` is high for "no notes" — likely a reverb tail still
  ringing during the reading (or the always-on FX floor). Worth confirming with a
  fully-decayed idle reading.

#### Targets being worked (this build)
- **events** — `AllocateVoice_` does a multi-pass victim scan per note-on; single-pass selection would cut it.
- **steal** — scale steal-fade length to victim env level; attack-stage victims are quiet, so they need little/no fade (cheaper *and* click-safe).
- **fetch** — dominated by +24 ratio + active seam crossfade; revisit after the above.

---

<!-- Template for new entries:

### YYYY-MM-DD — `<short-hash>`

- **Commit:** `<short-hash>` ("<subject>")
- **Working tree:** clean | dirty (list changes)

| Metric | Idle (no MIDI), `now` | Limit test, `peak` |
|---|---|---|
| callback total |  |  |
| pre-voice |  |  |
| voice render |  |  |
| fx total |  |  |
| events |  |  |
| steal |  |  |
| fetch |  |  |

Notes:
-

-->
