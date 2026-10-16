# Third-party notices

The source files in this repository are original work released under the MIT license (see `LICENSE`).

## Microsoft C/C++ runtime and C++ Standard Library in the DLLs

`ChaoGarage.dll` and `dinput8.dll` are built with the Microsoft C/C++ compiler (Visual Studio 2022 Build Tools, MSVC 14.42.34433) and are **statically linked** (`/MT`) with Microsoft's C/C++ runtime libraries. Each DLL therefore contains, besides this project's compiled code, machine code that Microsoft wrote: parts of the C runtime (start-up, memory, strings, exception handling) and of the Microsoft C++ Standard Library (for example `std::string`, `std::vector`, `std::map`). Not every byte of the DLLs is this project's code, and the MIT license in `LICENSE` covers only this project's own code.

* The C runtime code is Microsoft's and comes with Microsoft's build tools under Microsoft's license terms for them; it is not covered by this project's MIT license.
* The Microsoft C++ Standard Library is © Microsoft Corporation and licensed under the Apache License 2.0 with LLVM Exceptions. The notice installed with the build tools (`Licenses\1033\ThirdPartyNotices.txt`, entry *Microsoft C++ Standard Library*) reads in part:

  ```
  Microsoft C++ Standard Library
  Copyright (c) Microsoft Corporation
  Licensed under the Apache License, Version 2.0 (the "License"); ...
  ---- LLVM Exceptions to the Apache 2.0 License ----
  As an exception, if, as a result of your compiling your source code, portions
  of this Software are embedded into an Object form of such source code, you
  may redistribute such embedded portions in such Object form without complying
  with the conditions of Sections 4(a), 4(b) and 4(d) of the License.
  ```
  Upstream: https://github.com/microsoft/STL (LICENSE.txt).

The DLLs import functions from Windows system files (`kernel32.dll`, `bcrypt.dll`, and for `ChaoGarage.dll` `user32.dll`); `dinput8.dll` forwards DirectInput calls to the Windows system `dinput8.dll`. None of these Windows files is distributed.

## Not distributed

No game file, game executable, Steam or console SDK, font or image from another project is in this repository or in a release. Building needs Microsoft's build tools and Windows SDK, obtained from Microsoft under their own terms.

## Inspiration, not copying

The structure of this README (banner, download button, quick start, compatibility table) was inspired by community mod-manager projects for other Sonic games, such as SA Mod Manager (X-Hax) and HedgeModManager. No code, text, image or other asset from them was copied.

## Trademarks

See [docs/PROVENANCE.md](docs/PROVENANCE.md#trademarks).
