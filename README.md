# Wiz workspace

Three linked projects. Each directory is self-contained and has its own README.

| Project | What it is | Build |
| --- | --- | --- |
| [`compiler/`](compiler/README.md) | Wiz and CWiz toolchains (C++17, no dependencies) | `./build.sh` |
| [`emulator/`](emulator/README.md) | CasioEmuMsvc fork with fx-580VN X / Graph Light support | `cmake --build emulator/build` |
| [`cwiz_os/`](cwiz_os/README.md) | Standalone CWiz operating ROM for the fx-580VN X | `make -C cwiz_os build` |

## How they link

`cwiz_os` compiles with the `compiler`'s `wizc` binary and packages its payload
into an isolated model image for the `emulator`. The Makefile resolves the other
two projects relative to the repository root, so the three can live in one
workspace or in sibling checkouts.

```sh
./build.sh                  # build the compiler into ./build
make -C cwiz_os build       # compile the OS and package the model
make -C cwiz_os run         # build the emulator if needed and launch the model
```

## Other directories

- `docs/` — language, architecture and editor documentation for the compiler.
- `examples/` — sample `.wz` / `.cwiz` programs.
- `vscode-wiz/` — VS Code language support.
- `ios/`, `tests/`, `tools/` — platform and test helpers.
