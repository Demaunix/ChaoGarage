# zlib 1.3.1 in ChaoGarage

The `.c`/`.h` files here are the zlib compression library, version 1.3.1 (https://www.zlib.net/), © 1995-2024 Jean-loup Gailly and Mark Adler, under the zlib license (the text is at the top of `zlib.h`).

* Copied on 2026-10-23 from the zlib subset that Ghidra 12.1.4 ships (`Ghidra/Features/Decompiler/src/decompile/zlib`). Ghidra states those files are unchanged zlib 1.3.1 apart from a three-line comment added at the top of each file (`IP: zlib License`, `NOTE: from zlib 1.3.1`); that comment is kept. No other change.
* Only the files needed to pack and unpack zlib streams in memory are here (no gzip file code; built with `NO_GZIP`).
* ChaoGarage 1.3.0 uses them to read and write the menu scenes a member patch changes (`src/host/zlib_codec.h`). Python's `zlib` module is the same version, so the same level gives the same bytes as the mod build tools.
