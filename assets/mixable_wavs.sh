#!/bin/bash
# Converts the MIXABLE SFX to 16kHz mono PCM WAV headers.
#
# Mixable SFX play through the AudioCodec overlay (summed on top of media /
# ambience in ConsumeSample), which consumes raw PCM16 mono - so these assets
# must be WAV, never MP3. The idle path plays the very same WAV through the
# decoder (AudioGeneratorWAV), one asset serving both worlds.
#
# The original MP3s are not kept in the repo; this script extracts the bytes
# straight out of the existing generated headers, converts with afconvert
# (macOS built-in) and regenerates the headers with the _wav symbol.
#
# Usage: bash assets/mixable_wavs.sh
set -euo pipefail
cd "$(dirname "$0")/.."

TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT

for name in click camera thunder; do
  hdr="include/sounds/sfx_${name}.h"
  # Extract the byte array from the C header back into a binary file.
  python3 - "$hdr" "$TMP/$name.bin" <<'EOF'
import re, sys
src, dst = sys.argv[1], sys.argv[2]
text = open(src).read()
body = text[text.index('{') + 1 : text.rindex('}')]
data = bytes(int(t, 16) for t in re.findall(r'0x[0-9A-Fa-f]{2}', body))
open(dst, 'wb').write(data)
EOF
  # Already a WAV (re-run of this script)? afconvert reads it fine either way.
  afconvert -f WAVE -d LEI16@16000 -c 1 "$TMP/$name.bin" "$TMP/$name.wav"
  python3 assets/mp3_to_header.py "$TMP/$name.wav" "sfx_${name}_wav" "$hdr"
  ls -la "$TMP/$name.wav"
done
echo "OK: mixable SFX regenerated as 16kHz mono WAV headers"
