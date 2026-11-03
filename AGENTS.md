# Repository Instructions

## Crucial: no temp folders

Never store anything in a temp folder. That includes `/tmp`, `/private/tmp`,
`/private/var/folders`, `TMPDIR`, and any tool-default cache or scratch path.

- Scratch output — logs, screenshots, build logs, experiments, recovery scripts,
  intermediate files — goes under `/Users/kin/Desktop/Wiz/temp/<area>/`.
- Delete scratch files manually when they are no longer needed. Never rely on the
  OS or a tool to clean them up.
- Permanent files go in their repository: `/Users/kin/Desktop/Wiz` for the
  toolchain, docs and firmware research, `/Users/kin/Desktop/CasioEmuMsvc-Mac`
  for the emulator.
- If a tool defaults to a temp path, override it explicitly.

This rule exists because a previous workspace under a system temp folder was
deleted and took the uncommitted work with it.

## Text encoding

- Treat text files as UTF-8 without BOM unless a file clearly indicates otherwise.
- When reading or writing from a shell, use explicit UTF-8 without BOM APIs.

## Repositories

- Wiz toolchain docs live in `/Users/kin/Desktop/Wiz/docs`; never put toolchain
  documentation in the emulator repository.
- Do not commit or push the upstream Wiz repository unless explicitly asked.
- Emulator commits go to the `preview` branch of
  `Kin1009/CasioEmuMsvc-Mac`, in small subsystem-sized commits.
