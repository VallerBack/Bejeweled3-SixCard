# Build guide

See the build and run sections in [Chinese](../README.md) or [English](../README.en.md).

Run `tools/build.ps1` from Windows PowerShell with Visual Studio 2022 C++ tools installed. Outputs are placed in `out/`; intermediate files and offline test executables are placed in `build/`.

The launcher is generated with hashes of the newly built DLL and helper. The source copies retain the original component pins. Provide matching game files under `out/game/` to assemble a local runtime directory.
