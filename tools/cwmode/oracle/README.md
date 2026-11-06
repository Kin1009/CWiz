# cwmode golden generator

This directory keeps the harness that produced the golden hashes in
`tests/data/cwmode`. It is not part of the Wiz build; it needs a .NET SDK.

The original C# patchers live in `../reference/`. Their `Program.cs` files are
used **unmodified**. Four WPF types are stubbed so they can run on macOS:

- `System.Windows.Int32Rect`
- `System.Windows.Media.Imaging.BitmapImage` (the shim decodes 8-bit,
  non-interlaced PNG files and implements the `CopyPixels` overload the
  reference uses)
- `cw2tools.CasioInternal.Bitmap` with the reference `Set3`/`Set4` loops

`StaticDebug.cs` and `StaticDebugX.cs` are copies of the reference
`CasioInternal/Static.cs`. The only change is that `ConvertHexStringToBytes`
also strips `\r` and tabs: the reference sources have CRLF line endings and on
macOS `Environment.NewLine` is `\n`, so a raw string literal would otherwise
leave a stray `\r` in the hex payload. On Windows the reference behaves the
same, so the patched bytes are identical.

Regenerate the four fixtures by running each oracle in a scratch directory that
contains the input `rom.bin`, feeding the same answers as the C# prompt:

```console
$ dotnet run --project tools/cwmode/oracle/cw2mode_oracle.csproj -- \
      --model 880
```

The programs ignore their arguments and read `rom.bin` from the current
directory, then write `rom_patched.bin`. The inputs used for the repository
fixtures are listed in `tests/data/cwmode/golden_*.txt`.

The output SHA-256 (and the changed spans) are what `tests/test_cwmode.cpp`
compares. Build output goes to `temp/cwmode/oracle-*`, never into this tree.
