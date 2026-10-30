#!/bin/sh
# Fetches the third-party sources Orange Mouse builds on, at pinned commits, into third_party/.
# Re-running it moves each checkout to its pin. The checkouts themselves are not committed.
#
#   name              licence        what for
#   eurorack          MIT            Plaits (Mutable Instruments)
#   eurorack/stmlib   MIT            Plaits' DSP helpers
#   dexed             GPL-3.0        Dexed's FM engine (msfa)
#   sst-effects       GPL-3.0        Surge XT effects
#   sst-basic-blocks  GPL-3.0/MIT    their building blocks
#   sst-filters       GPL-3.0
#   sst-waveshapers   GPL-3.0
#   simde             MIT            SSE intrinsics on ARM and WebAssembly
#   fmt               MIT            used by sst-basic-blocks' parameter metadata
#   axe-core          MPL-2.0        the browser test's accessibility audit (tests only)
set -e
cd "$(dirname "$0")"

# patches/<name>.patch, if there is one, is applied to the checkout: our fixes
# to upstream bugs (each hunk says what and why). The checkout is reset first.
pin() {  # dir url commit
  if [ ! -d "$1/.git" ]; then
    rm -rf "$1"; git init -q "$1"; git -C "$1" remote add origin "$2"
  fi
  git -C "$1" fetch -q --depth 1 origin "$3"
  git -C "$1" -c advice.detachedHead=false checkout -q -f FETCH_HEAD
  patch="patches/$(echo "$1" | tr / -).patch"
  if [ -f "$patch" ]; then git -C "$1" apply "$PWD/$patch"; echo "$1: applied $patch"; fi
  echo "$1 $(git -C "$1" rev-parse --short HEAD)"
}

pin eurorack         https://github.com/pichenettes/eurorack.git            f4cfa7697095a28d292bff758beb51deb49b5a86
pin eurorack/stmlib  https://github.com/pichenettes/stmlib.git              d18def816c51d1da0c108236928b2bbd25c17481
pin dexed            https://github.com/asb2m10/dexed.git                   fea2f0f0f2c397799f59b27eb11c038cd34b22c5
pin sst-effects      https://github.com/surge-synthesizer/sst-effects.git    987b39dcb2e2e61e6225c3bc390fd00da824d226
pin sst-basic-blocks https://github.com/surge-synthesizer/sst-basic-blocks.git ed7c7cd6cd185aba87c3de9ac2c744d81824e54a
pin sst-filters      https://github.com/surge-synthesizer/sst-filters.git    87a171bcba52c014850848a29174a54f0adc6606
pin sst-waveshapers  https://github.com/surge-synthesizer/sst-waveshapers.git dd12f31a5a9016c9895e52d1a00eee0e1eebe6ce
pin simde            https://github.com/simd-everywhere/simde.git           2e5224981e9123b8fe89a9833a978556d636a565
pin fmt              https://github.com/fmtlib/fmt.git                      123913715afeb8a437e6388b4473fcc4753e1c9a

# axe-core: the npm release (its git repository has no build), checked by its hash
AXE=4.10.2
AXE_SHA=9ea2d2d6917da998122bdacfd3b473e31ead53f4b87146c716a12edf89158100
if [ ! -f axe-core/axe.min.js ] || [ "$(cat axe-core/VERSION 2>/dev/null)" != "$AXE" ]; then
  rm -rf axe-core; mkdir -p axe-core
  curl -sSfL -o axe-core/axe.tgz "https://registry.npmjs.org/axe-core/-/axe-core-$AXE.tgz"
  echo "$AXE_SHA  axe-core/axe.tgz" | shasum -a 256 -c - >/dev/null
  tar -xzf axe-core/axe.tgz -C axe-core --strip-components 1 package/axe.min.js package/LICENSE
  rm axe-core/axe.tgz; echo "$AXE" > axe-core/VERSION
fi
echo "axe-core $(cat axe-core/VERSION)"
