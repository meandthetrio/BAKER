# Vendored DSP headers

In-tree, header-only copies of third-party libraries used by the bake feature.
Both libraries are header-only and MIT-licensed.

## signalsmith-stretch

PSOLA-style pitch shifting / time stretching for the bake worker.

- Upstream: https://github.com/Signalsmith-Audio/signalsmith-stretch
- Commit:   `57b93f4e9206a089a45387eaa39bdc9f310d3308`
- License:  MIT (see `signalsmith-stretch/LICENSE.txt`)
- Files:    `signalsmith-stretch/signalsmith-stretch.h`

## signalsmith-linear

FFT/STFT dependency for signalsmith-stretch. The stretch header
`#include "signalsmith-linear/stft.h"`.

- Upstream: https://github.com/Signalsmith-Audio/linear
- Commit:   `88c701ce8d581946de5ee587848cde4a572ed6b5`
- License:  MIT (see `signalsmith-linear/LICENSE.txt`)
- Files:    `signalsmith-linear/stft.h`, `signalsmith-linear/fft.h`
            (`linear.h` is NOT vendored — not needed by the stretch.h chain)

## Include path

Top-level `-I./src/dsp/` makes both libraries reachable via their natural include
form:

```cpp
#include "signalsmith-stretch/signalsmith-stretch.h"
// (which internally does #include "signalsmith-linear/stft.h")
```

## Updating

If a future upstream change is needed:

1. `cd /tmp && git clone --depth 1 https://github.com/Signalsmith-Audio/signalsmith-stretch.git`
2. Copy the top-level `signalsmith-stretch.h` and `LICENSE.txt` into
   `src/dsp/signalsmith-stretch/`, overwriting.
3. Same for the `linear` repo's `stft.h`, `fft.h`, `LICENSE.txt`.
4. Update commit hashes above.
5. Rebuild and verify the bake worker still produces correct pitched output.
