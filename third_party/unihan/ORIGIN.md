# Unihan Mandarin data

- Source: Unicode 17.0.0, `Unihan_Readings.txt`, `kMandarin` field.
- Archive: https://www.unicode.org/Public/17.0.0/ucd/Unihan.zip
- Archive SHA-256: `f7a48b2b545acfaa77b2d607ae28747404ce02baefee16396c5d2d7a8ef34b5e`.
- License: Unicode License v3, copied from https://www.unicode.org/license.txt to `LICENSE.txt`.
- Generated file: `mandarin_data.hpp`; reproduce with `python scripts/make_pinyin_data.py build/Unihan-17.0.0.zip`.

Only code points and the first recommended Mandarin reading are retained. This is a single-character lookup, not contextual pronunciation analysis. The editor lets the author correct polyphonic characters. The archive and Python are development inputs, not runtime dependencies.
