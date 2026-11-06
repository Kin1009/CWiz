# cwmode

C++17 port of the C# firmware patchers `cw2mode` (fx-880BTG) and `cwxmode`
(fx-580VN X). It adds modes (icon, names or text images, entry point) to a
firmware image and keeps the reference byte layout.

- `reference/` original C# sources, kept for provenance.
- `oracle/` WPF shim and .NET projects that regenerate the golden hashes.
- `integration/` exact `Gui/WizToolchain.cpp` patch for the "Add Modes..." menu
  item in the Wiz Compiler.

See `docs/CWMODE.md` for the CLI, the ROM tables and hooks, validation, the
debugger panel and limitations.
