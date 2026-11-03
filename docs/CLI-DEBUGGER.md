# casioctl — CLI debugger and control channel for a running CasioEmuMsvc session

`casioctl` attaches to a **live** CasioEmuMsvc process, not to a core
file. The emulator hosts a small local control server; the CLI discovers it,
connects over a UNIX domain socket, and drives the same debugger state that the
ImGui debugger windows use.

- No ptrace, no process injection, no `sudo`.
- No new third-party dependencies (POSIX sockets + a ~600-line JSON reader).
- macOS-first; the server is compiled out on Windows and WebAssembly builds and
  the CLI refuses to configure there.

---

## 1. Architecture

### 1.1 Components

```
+---------------------------------------------------------+
| CasioEmuMsvc process                                    |
|                                                         |
|  UI thread (SDL main loop)     control IO thread        |
|  +-----------------------+     +---------------------+  |
|  | Emulator::Frame()      |     | accept / recv /send|  |
|  |   ctl::OnShutdown() <--+-----+  (sockets only)    |  |
|  |   ctl::OnFrame()       |     |                     |  |
|  |     drain inbound  <---+-----+  inbound queue      |  |
|  |     execute commands   |     |  outbound queue     |  |
|  |     IDebugger / ...    |     +---------------------+  |
|  +-----------------------+                ^             |
+-------------------------------------------|-------------+
                                            | AF_UNIX
                                   ~/Library/Application Support/
                                     CasioEmuMsvc/ctl/
                                       session-<pid>.sock   (0600)
                                       session-<pid>.json   (0600)
                                            ^
                                   +--------+--------+
                                   |  casioctl CLI   |
                                   +-----------------+
```

* **UI thread** — `Emulator::Frame()` is called by the application main loop for
  every rendered frame. Its first action is `casioemu::ctl::OnFrame(*this)`.
  That lazily starts the server, then drains queued requests. All debugger and
  chipset state is touched **only here**. This is the same thread that renders
  the debugger windows and calls into the emulator's `access_mx`.
* **Control IO thread** — owns the listening socket and client sockets. It only
  accepts, reads complete newline-delimited lines into an inbound queue, and
  flushes an outbound queue. It never touches emulator state.
* **Command execution** — happens during `OnFrame`. The dispatcher calls the
  existing in-process debugger interface (`PluginApi::QueryInterface<IDebugger>`)
  and the keyboard/chipset/emulator interfaces. A reply is queued for the IO
  thread.

The server is started on the **first frame** and stopped from
`Emulator::Shutdown()` and `Emulator::~Emulator()`. Both hooks live in
`CasioEmuMsvc/src/Emulator.cpp` (an allowed file); no forbidden file is touched.

### 1.2 Why the debugger interface is reusable in-process

`CasioEmuMsvc/src/Plugin/PluginApi_Impl.cpp` is compiled into the executable and
constructs a process-global `PluginApi* g_pluginapi`. Its `IDebugger`
implementation does not own a second copy of debugger state: it delegates to the
live `Chipset`, `MMU`, `CPU`, `ePSCPU`/`T4xCore`, and the debugger windows
(`code_viewer`, `snapshot_window`, `membp`).

That means the MCP tool surface and this CLI drive exactly the same state as a
human using the GUI: setting a breakpoint from `casioctl` shows up in the Code
window too. The control server only adds a transport.

`IDebugger` is available whenever the desktop debugger windows exist. In the
shipping single-window build (`SINGLE_WINDOW` in `src/Config.hpp`) the windows
are always created (`CreateDebuggerGuiWindows()` in `src/Gui/Ui.cpp`), including
when the user has closed the visible tabs.

### 1.3 Session files and discovery

Both files live in:

```
$CASIOEMU_CTL_DIR                  (override, optional)
  or $HOME/Library/Application Support/CasioEmuMsvc/ctl
```

* `session-<pid>.sock` — `AF_UNIX, SOCK_STREAM`, mode **0600**.
* `session-<pid>.json` — written atomically (`.tmp.<pid>` + `rename`), mode
  **0600**. Fields:

```json
{
  "app": "CasioEmuMsvc",
  "app_version": "<git commit hash>",
  "hardware": "CWX",
  "hardware_id": 4,
  "model": "fx-580VN X",
  "model_path": "/path/to/models/fx-580VN X",
  "pid": 4242,
  "protocol": "casioctl",
  "protocol_version": 1,
  "rom_path": "rom.bin",
  "socket": "/Users/me/Library/Application Support/CasioEmuMsvc/ctl/session-4242.sock",
  "started_at": "2026-11-02T12:34:56Z"
}
```

`casioctl sessions` reads every `session-*.json`, hides entries whose:

1. process is gone (`kill(pid, 0)` fails),
2. socket file is missing or is not a socket,
3. protocol version does not match, or
4. socket does not accept a connection (a non-blocking connect probe).

Stale files are hidden, not deleted (a leftover file is harmless and the owning
process unlinks its own files on shutdown). The control directory is created
with mode 0700.

### 1.4 Lifecycle details

* A session is one `Emulator` object. Closing the calculator window calls
  `Emulator::Shutdown()`; the files disappear immediately and the CLI sees no
  session. Opening another model starts a new session on the same pid and a new
  socket/JSON pair.
* If the app crashes, stale files remain; discovery hides them.
* `Emulator::Frame()` is called for every rendered frame even while the emulator
  is paused (the debugger windows still repaint). Commands therefore keep
  working while paused.
* `--headless` builds produce no frames; the server never starts there (see
  Limitations).

### 1.5 Threading rules enforced by the design

| State | Thread |
| --- | --- |
| sockets, queues | control IO thread |
| request parse/dispatch, reply | UI thread (`OnFrame`) |
| `Chipset`/`MMU`/CPU registers, breakpoints, snapshots, GUI windows | UI thread only |
| emulated instruction execution | emulator tick thread (guarded by `access_mx`) |
| screenshot pixel readback, SDL surface/PNG write | UI thread only |

`IDebugger` methods take `Emulator::access_mx` where needed, so the UI thread
can safely read registers or memory while the tick thread runs.

---

## 2. Wire protocol

### 2.1 Framing

* Transport: UNIX domain stream socket.
* One JSON object per line (`\n`), UTF-8, no BOM.
* Maximum request line: 4 MiB. Longer lines close the connection.
* A client may pipeline requests; replies may be interleaved but each reply
  carries the request `id`.
* All responses are one line.

### 2.2 Handshake

A client should send:

```json
{"id":1,"cmd":"hello","args":{}}
```

Response:

```json
{
  "id": 1,
  "ok": true,
  "result": {
    "protocol": "casioctl",
    "protocol_version": 1,
    "app": "CasioEmuMsvc",
    "app_version": "a1b2c3d",
    "pid": 4242,
    "model": "fx-580VN X",
    "model_path": "/…/fx-580VN X",
    "rom_path": "rom.bin",
    "hardware": "CWX",
    "hardware_id": 4,
    "real_hardware": false,
    "paused": true,
    "debugger": true,
    "debugger_ui": true,
    "snapshot_ui": true,
    "keyboard": true,
    "screenshot": true,
    "capabilities": ["status", "pause", "…"],
    "limits": {
      "read_max": 1048576,
      "write_max": 262144,
      "disasm_max": 4096,
      "breakpoint_range_max": 256,
      "step_max": 10000
    }
  }
}
```

`hello` is optional in principle but the CLI always performs it before a
command and refuses to continue if `protocol_version` does not match.

### 2.3 Request and response envelopes

Request:

```json
{"id": 7, "cmd": "read", "args": {"addr": 59872, "len": 16}}
```

Success:

```json
{"id": 7, "ok": true, "result": {"address": 59872, "length": 16, "data": "…"}}
```

Failure:

```json
{"id": 7, "ok": false, "error": "unknown register 'foo'", "code": "not-found"}
```

* `id` is echoed when present; absent ids are accepted (the response omits it).
* `args` is optional; when present it must be an object.
* Numbers are JSON integers. Address arguments also accept a hex string
  (`"0xE9E0"`) for ad-hoc clients.
* `data` byte strings are lowercase hex without separators. Array-of-byte input
  is also accepted.

### 2.4 Error codes

| code | meaning |
| --- | --- |
| `bad-request` | malformed JSON/envelope |
| `bad-args` | missing/invalid argument |
| `unknown-command` | unknown `cmd` |
| `out-of-range` | address/length/limit violation |
| `not-found` | unknown register/key/snapshot |
| `not-paused` | operation requires a paused TI MultiView core |
| `debugger-unavailable` | `IDebugger` unavailable |
| `unsupported` | model/build does not support the operation |
| `no-frame` | screenshot requested before the first frame |
| `write-failed` / `save-failed` / `reload-failed` | filesystem/ROM failure |
| `step-failed` / `step-timeout` | stepping could not start/finish |
| `busy` | a step is already in flight |
| `internal` | unexpected exception |

### 2.5 Command reference

Addresses are byte addresses unless noted. "Requires pause" means the TI
MultiView core silently ignores writes while running, so the server returns
`not-paused` instead.

| `cmd` | `args` | `result` |
| --- | --- | --- |
| `hello` | — | capabilities, model, limits |
| `status` | — | model, hardware, `paused`, `pc`, `registers{sp,lr}`, `rom{base,size}`, `ram{base,size}`, `run_mode`, `cycles_per_second` |
| `pause` / `resume` | — | `{paused}` |
| `reset` | — | `{paused, pc}` |
| `step` | `n` (1–10000, default 1) | `{steps, pc, paused, mode:"into"}` |
| `step.over` | — | `{steps:1, pc, paused, mode:"over"}` |
| `step.out` | — | `{steps:1, pc, paused, mode:"out"}` |
| `regs` | — | `{registers:[{name,value,bits}]}` |
| `reg.get` | `name` | `{name,value,bits}` |
| `reg.set` | `name,value` | `{name,value,bits}` (read back) |
| `read` | `addr,len` (len ≤ 1 MiB) | `{address,length,data}` |
| `write` | `addr,data` (≤ 256 KiB) | `{address,length}` |
| `read-rom` | `addr,len` | `{address,length,rom_size,data}` |
| `write-rom` / `write-code` | `addr,data` | `{address,length}` |
| `read-code` | `addr,count` | `{address,count,words:[u16],hex:[…]}` |
| `disasm` | `addr?` (default PC), `count?` (default 8) | `{address,lines:[{address,text,label,xref}]}` |
| `bp.set` | `addr` | `{address,breakpoints:[…]}` |
| `bp.clear` | `addr` | `{address,breakpoints:[…]}` |
| `bp.clearall` | — | `{breakpoints:[]}` |
| `bp.list` | — | `{exec:[…], mem:[{address,write,break_when_hit,hits,enabled,compare_data,data,mask,skip_count}]}` |
| `bp.hits` | `addr,access?` (`read`/`write`) | `{address,access,hits:[{pc,lr,stack,value,registers}]}` |
| `bp.mem.set` | `addr,len,access?` (`read`/`write`/`both`), `break_when_hit?`, `enabled?`, `compare_data?`, `data?`, `mask?`, `skip?` | `{address,length,access,watched:[…]}` |
| `bp.mem.clear` | `addr,len?,access?` | `{address,length}` |
| `bp.mem.clearall` | — | `{cleared}` |
| `watch.list` | — | `{watches:[{address,value,locked}]}` |
| `watch.set` | `addr,value` (byte), `locked?` | `{address,value,locked}` |
| `watch.delete` | `addr` | `{address,removed}` |
| `watch.clearall` | — | `{cleared}` |
| `call.start` | `caller?`, `callee?` (0 = any) | `{recording,caller,callee}` |
| `call.stop` / `call.clear` | — | `{recording:false}` / `{cleared}` |
| `call.list` | `function?` (0 = all) | `{calls:[{function,caller,xr0,stack}]}` |
| `snapshot.list` | — | `{snapshots:[{id,parent,label,timestamp,preview_bytes,state_bytes}]}` |
| `snapshot.save` | `label?`, `parent?` | `{id,parent,label}` |
| `snapshot.load` | `id` | `{id,paused}` |
| `snapshot.delete` | `id` | `{id,deleted}` |
| `snapshot.export` | `path,id?,subtree?` | `{path,id,subtree}` |
| `snapshot.import` | `path` | `{path,imported}` |
| `key.press` / `key.release` | `name` or `code` | `{name,code,kind,ki?,ko?,pressed}` |
| `key.hold` | `name`, `ms` | `{name,code,…,hold_ms}` |
| `key.releaseall` | — | `{released}` |
| `key.matrix` | `ki` (0–7), `ko` (0–15), `action` | `{ki,ko,action}` |
| `key.list` | — | `{keys:[{name,code,kind,ki,ko}]}` |
| `screenshot` | `path`, `format` (`png`/`rgba`) | `{path,format,width,height,bytes}` |
| `inject` | `addr,data,run?` | `{address,length,running}` |
| `bt` | — | `{frames:[{pc,lr,sp,symbol,lr_pushed,is_jump}],text}` |
| `labels` | `query?`, `limit?` | `{labels:[{address,name}]}` |
| `vars` | — | `{variables:[{name,real_address,real_value,real_hex,has_imaginary,…}]}` |
| `hw` | — | hardware state |
| `hw.cps` | `value` | `{cycles_per_second}` |
| `hw.pd` | `value` (byte) | `{pd_value}` |
| `hw.display` | — | display/fading/residual/audio settings |
| `hw.display.set` | any subset of the display keys | applied settings |
| `hw.interrupt` | `index` (0–255) | `{index}` |
| `hw.reload-rom` | — | `{reloaded,rom_size}` |
| `hw.record` | `start` (bool) | `{requested,recording_active}` |
| `hw.qr` | — | QR capture state and history |

Notes:

* `step`, `step.over`, `step.out` are **deferred**: the server pauses the
  emulator (if needed), issues the request, and replies when the instruction
  hook pauses it again. `step n` executes one instruction per UI frame; see
  Limitations.
* `read-rom` reads `chipset.rom_data` directly. For EPS/T4x models `write-code`
  goes through `IDebugger::WriteCode`, which also patches the live code word
  (and the ROM image where the model supports it). Address units follow the
  debugger's code-space convention, which is word-addressed on EPS/T4x models.
* `bp.mem.set` adds one read and/or write breakpoint per byte in the range.
  Ranges are capped at 256 bytes to prevent accidental floods. The CLI flags
  map onto the extended `IDebugger::AddMemoryBreakpoint` parameters:
  `--record` sets `break_when_hit=false` (monitor hits without pausing),
  `--compare <hex>` + `--mask <hex>` watch for a specific value, and
  `--skip <n>` ignores the first `n` hits. Hit records are returned by
  `bp hits`.

---

## 3. CLI

### 3.1 Build

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DBUILD_EXECUTABLE=ON
cmake --build build --target CasioEmuMsvc casioctl -- -j$(sysctl -n hw.logicalcpu)
```

The `casioctl` target is POSIX-only; on Windows/Android/WASM the
`tools/casioctl` subdirectory prints a skip message.

### 3.2 Discovery

```console
$ casioctl sessions
PID     MODEL                           HARDWARE     STARTED               SOCKET
4242    fx-580VN X                      CWX          2026-11-02T12:34:56Z  /Users/me/Library/Application Support/CasioEmuMsvc/ctl/session-4242.sock
(1 stale session file(s) ignored)
```

```console
$ casioctl sessions --json
{"sessions":[{"app":"CasioEmuMsvc","app_version":"a1b2c3d","hardware":"CWX","hardware_id":4,"model":"fx-580VN X","model_path":"…","pid":4242,"protocol":"casioctl","protocol_version":1,"reachable":true,"rom_path":"rom.bin","socket":"…","started_at":"2026-11-02T12:34:56Z"}],"stale":1}
```

`attach` accepts a pid or a model name (exact first, then case-insensitive
substring; an ambiguous substring is an error listing the candidates).

### 3.3 REPL

```console
$ casioctl attach 4242
attached to fx-580VN X (/Users/me/Library/Application Support/CasioEmuMsvc/ctl/session-4242.sock)
type 'help' for commands, 'quit' to leave
casioctl> status
model      fx-580VN X
hardware   CWX (id 4)
path       /Users/me/models/fx-580VN X
rom        rom.bin
state      paused
pc         0xEDC0  sp 0xF9FE  lr 0x1234
rom        262144 bytes @ 0x0
ram        0x9000..0xF000 (24576 bytes)
run mode   halt
cycles/s   2097152
casioctl> read 0xE9E0 16
0000e9e0  12 34 56 78 00 00 00 00  00 00 00 00 00 00 00 00  |4Vx.............|
casioctl> step 3
stepped 3 instruction(s), pc=0xEDC4
casioctl> bp set 0xEDC4
exec breakpoints: 0xEDC4
casioctl> resume
running
casioctl> status
...
state      paused
casioctl> quit
```

Non-interactive REPL input works too (prompt is printed only for a TTY):

```sh
printf 'status\nquit\n' | casioctl attach "fx-580VN"
```

### 3.4 One-shot mode and `--json`

```console
$ casioctl -s 4242 read 0xE9E0 16
0000e9e0  12 34 56 78 00 00 00 00  00 00 00 00 00 00 00 00  |4Vx.............|

$ casioctl -s 4242 --json status | jq '.result.pc'
60992

$ casioctl -s 4242 step-over
stepped over, pc=0xEDC6

$ casioctl -s 4242 screenshot ./calc.png
wrote 236x392 png image to ./calc.png
```

`--json` prints the complete response envelope (including `ok`, `error`,
`code`) as one line on stdout, suitable for scripts and `jq`.

Relative paths passed to `screenshot`, `snapshot export` and `snapshot import`
are resolved to absolute paths by the CLI before sending, so the emulator's
working directory does not matter.

There is also a direct-socket escape hatch, useful for tests or wrappers:

```sh
casioctl --socket /path/to/session.sock read 0x100 16
casioctl attach --socket /path/to/session.sock
```

### 3.5 Exit codes

| code | meaning |
| --- | --- |
| 0 | success |
| 1 | usage / local command-line error |
| 2 | session not found, ambiguous, or connect refused |
| 3 | protocol error, transport failure, or the debugger rejected the command |

### 3.6 Command examples

```sh
# execution
casioctl -s 4242 pause
casioctl -s 4242 step 10
casioctl -s 4242 step-over
casioctl -s 4242 step-out

# registers
casioctl -s 4242 regs
casioctl -s 4242 reg get pc
casioctl -s 4242 reg set pc 0xEDC0

# memory / code
casioctl -s 4242 read 0xE9E0 64
casioctl -s 4242 write 0xE9E0 deadbeef
casioctl -s 4242 read-code 0x100 8
casioctl -s 4242 read-rom 0 32
casioctl -s 4242 write-rom 0x100 0000
casioctl -s 4242 disasm 0x100 6

# breakpoints
casioctl -s 4242 bp set 0x100
casioctl -s 4242 bp mem 0xE9E0 8 both
casioctl -s 4242 bp list
casioctl -s 4242 bp hits 0xE9E0 write

# address overrides
casioctl -s 4242 watch set 0xE9E0 42
casioctl -s 4242 watch list

# keyboard
casioctl -s 4242 key list
casioctl -s 4242 key press 7
casioctl -s 4242 key release 7
casioctl -s 4242 key hold Space 150
casioctl -s 4242 key matrix 3 4 press
casioctl -s 4242 key releaseall

# snapshots
casioctl -s 4242 snapshot save "after boot"
casioctl -s 4242 snapshot list
casioctl -s 4242 snapshot export ./snaps.bin 0
casioctl -s 4242 snapshot import ./snaps.bin

# analysis / hardware
casioctl -s 4242 bt
casioctl -s 4242 labels sin
casioctl -s 4242 vars
casioctl -s 4242 call start 0x100 0x200
casioctl -s 4242 call list
casioctl -s 4242 hw
casioctl -s 4242 hw display set flashing_threshold 24
casioctl -s 4242 hw record start
casioctl -s 4242 hw reload-rom
casioctl -s 4242 hw qr
```

### 3.7 Headless / CI usage

The desktop app can run without a display or WindowServer. Two mechanisms work
together:

* `CASIOEMU_HEADLESS=1` is read by a static initializer
  (`src/Ctl/CtlHeadless.cpp`) that runs **before** `main()` calls `SDL_Init`;
  it selects SDL's `dummy` video driver.
* `headless=` in the app's argv switches the Emulator itself to offscreen
  rendering: SDL video is re-initialized on the dummy driver if it was not
  already, the debugger window objects stay alive (so `IDebugger` keeps
  working), but all debugger windows are closed after the first frame so the
  software renderer does not spend its time drawing ImGui. `Emulator::Frame()`
  still renders the calculator into the offscreen composite target, which is
  what `screenshot` reads back.

The app's arguments use `key=value` (a bare word is parsed as `model=`). Use:

```sh
CASIOEMU_HEADLESS=1 \
  CasioEmuMsvc.app/Contents/MacOS/CasioEmuMsvc \
  "model=/path/to/models/fx-580VN X" "headless=" "paused="
```

No window server, focus, `osascript`, or `screencapture` is involved.
`screenshot` is composited internally from the SDL render target and encoded
with `IMG_SavePNG` (or written as raw RGBA with `--raw`).

`hello` and `status` report `offscreen_rendering: true`.

### 3.8 Self-test

`casioctl selftest` needs no emulator. It starts a fake server in a temporary
directory and exercises the JSON codec, the CLI parser, session discovery
(including a stale-pid case), the handshake, request/response framing, and
malformed-request handling.

```console
$ casioctl selftest
casioctl selftest: 59 checks passed
$ casioctl selftest --json
{"checks":59,"failures":[],"ok":true}
```

Under CMake:

```sh
cmake --build build --target casioctl
ctest --test-dir build/tools/casioctl --output-on-failure
```

---

## 4. Coverage vs the MCP debugger

The Windows MCP plugin (`McpPlugin/README.md`) is the reference surface. Parity:

| MCP family | MCP capability | `casioctl` |
| --- | --- | --- |
| Code | status, pause/resume/reset, step into/over/out, disassembly, code read/write, execution breakpoints | `status`, `pause`, `resume`, `reset`, `step`, `step-over`, `step-out`, `disasm`, `read-code`, `write-code`, `read-rom`, `write-rom`, `bp set/clear/clearall/list` |
| Watch | register list/read/write, stack frames, textual backtrace, memory read/write | `regs`, `reg get`, `reg set`, `bt`, `read`, `write` |
| Breakpoints | memory read/write monitors, break-on-hit, hit count, hit PC/LR/call stack | `bp mem`, `bp list`, `bp hits` |
| Labels | label search | `labels` |
| Variables | decoded real/complex calculator variables | `vars` |
| Addrs | address watches and fixed-value overrides | `watch list/set/delete/clearall` |
| Call Analysis | start/stop/clear recording, recorded calls | `call start/stop/clear/list` |
| Snapshot | list/save/load/delete/import/export | `snapshot list/save/load/delete/export/import` |
| Hardware | CPS, PD, interrupt, display/fading/residual/buffer/audio, screenshot, recording, ROM hot reload | `hw`, `hw cps`, `hw pd`, `hw interrupt`, `hw display`, `hw display set`, `screenshot`, `hw record`, `hw reload-rom` |
| Hex editors / Injector | generic MMU/ROM reads and writes, payload injection | `read`, `write`, `read-rom`, `write-rom`, `read-code`, `write-code`, `inject` |
| Bitmap | raw memory reads; rendering left to the client | `read` (raw bytes); no image renderer (see below) |
| Keyboard | KI/KO matrix, raw key codes, release all | `key matrix`, `key press/release/hold`, `key releaseall`, `key list` |
| QR capture | (extra in `IDebugger`) | `hw qr` |
| Theme editing, file dialogs, plugin logs, ROP compiler | explicitly not debugger state in the MCP README | intentionally not exposed |

Everything in the reference MCP surface is reachable. The only deliberate
omission is **bitmap image rendering**: MCP also returns only raw memory and
leaves rendering to the client; `casioctl read` provides the same bytes. The
`screenshot` command covers the rendered-screen use case, which MCP delegates to
its hardware tool.

---

## 5. How the emulator internals are used

### `Emulator` (`src/Emulator.{hpp,cpp}`)

* `Frame()` — frame-cadence hook. `ctl::OnFrame(*this)` is the first statement.
  It starts the server once and drains requests. This guarantees the UI thread
  and the same frame timing as the debugger windows.
* `Shutdown()` / `~Emulator()` — `ctl::OnShutdown(*this)` closes the listening
  socket, joins the IO thread, and unlinks `session-<pid>.sock` and
  `session-<pid>.json`.
* `Paused` — the authoritative run/pause flag. `step`/`resume`/`pause` read it
  directly and `SetPaused` is used where `CodeViewer::RequestStep` would.
* `chipset.t4x` / `chipset.epscpu` — selects the synchronous TI stepping path
  versus the asynchronous EPS/legacy paths.
* `composite_texture` (single-window builds) — pixel source for `screenshot`.
* `recording_active` — reported by `hw record`.

### `Chipset` (`src/Chipset/Chipset.hpp`)

* `run_mode` (`RM_STOP/HALT/RUN/DEEP_HALT/HALT_H`) — `status`/`hw` `run_mode`.
* `rom_data` — `read-rom` (via `IChipset::GetRom/GetRomSize`).
* `Tick()`, `RunEpsFrame()`, `RunTiMultiViewFrame()`, `StepTiMultiView()` — the
  execution paths that make stepping observable; the server only issues
  requests through `IDebugger` and observes `Paused`.
* `Reset()` — `reset` (through `IDebugger::Reset`, which preserves the
  paused/running state).
* `QueryInterface<IKeyboardAutomation>` — keyboard automation.
* `RaiseMaskable(index)` — `hw interrupt` (through `IChipset::RaiseInterrupt`).

### `MMU` (`src/Chipset/MMU.hpp`)

`IDebugger::ReadMemory/WriteMemory` use `mmu.ReadData/WriteData` for legacy
models and direct `ReadDebugMemory/WriteDebugMemory` for EPS/T4x. This is the
same path as the hex editors and the injector.

### `CPU`

`IDebugger::GetRegisters/ReadRegister/WriteRegister` walk
`cpu.register_proxies`, special-case `pc` and `lr`, and take `access_mx`.
EPS register names come from the ePS6800/ePS6009 alias tables in
`PluginApi_Impl.cpp`; T4x registers are nibbles exposed as `r0`…`rN`.

### `Keyboard` (`src/Peripheral/Keyboard.cpp`)

`key press/release/hold` resolves a model button name against
`ModelDefinition.buttons[].keyname` (or accepts a raw kiko code /
`power` / `reset`) and calls `IKeyboardAutomation::PressCode`. `key matrix`
calls `IKeyboardAutomation::Key(ki, ko, pressed)`. `key releaseall` calls
`ReleaseAll`, which does not unstuck deliberately stuck keys — the same
behaviour as the MCP tool and the UI.

### `PluginApi_Impl.cpp`

The process-global `g_pluginapi` is the single integration point:

```
IDebugger   run control, registers, memory, code, breakpoints, backtrace,
            labels, variables, snapshots, address locks, call analysis,
            display settings, QR, recording, ROM reload
IKeyboard   key matrix / raw codes / release all
IChipset    run mode, ROM image, interrupts
IEmulator   model metadata, battery/solar
```

No emulator global state is duplicated by `Ctl/`.

---

## 6. Security model

* The socket is a UNIX domain socket in a **0700** directory, socket mode
  **0600**. Only the owning user can connect. There is no TCP listener.
* Peer credentials are not checked beyond filesystem permissions; any process
  running as the same user can attach, which is also true of `lldb`/`gdb`
  attaching to the same-user process.
* The protocol can write arbitrary memory, patch code, reboot the model, and
  write files (`screenshot`, snapshot export). Treat an attached session as
  equivalent to shell access as that user.
* The CLI resolves output paths locally and the server writes them as the
  emulator user; no path is executed.
* The server accepts at most 4 MiB per request line, caps reads at 1 MiB,
  writes at 256 KiB, disassembly at 4096 lines, breakpoint ranges at 256 bytes,
  and step counts at 10000.
* `CASIOEMU_CTL_DIR` can relocate the sockets (for example to a short path if
  `HOME` is deeply nested). Changing it changes discovery for both server and
  CLI.

---

## 7. Limitations

1. **Frame-driven.** Requests are serviced in `Emulator::Frame()`. Offscreen
   runs (`headless=`) still render into the composite target and therefore keep
   servicing requests. Only an `Emulator` created through the unit-test
   constructor (`headless=true`, no window, no frame loop) has no server. On a
   desktop, a window that SDL stops delivering frame events for delays command
   servicing.
2. **Step throughput.** `step n` proceeds one instruction per UI frame because
   the emulator must run between steps. At 60 fps that is roughly 60
   instructions/second; `n` is capped at 10000. `step-over`/`step-out` are one
   request each and are fast.
3. **Debugger UI dependency.** `disasm`, execution breakpoints on EPS/T4x, and
   snapshots go through `IDebugger`, which delegates to `code_viewer` and
   `snapshot_window`. In the desktop single-window build these always exist.
   If a future build disables the debugger UI, `hello` reports
   `debugger_ui:false` and those commands return `unsupported` or empty results
   instead of pretending to work.
4. **Screenshot.** Requires a `SINGLE_WINDOW` build with a composited render
   target and at least one rendered frame (otherwise `no-frame`). Raw RGBA
   output is available with `--raw` for clients that do not decode PNG.
5. **Headless needs the env hint only before SDL_Init.** A process with no
   WindowServer at all must start with `CASIOEMU_HEADLESS=1`, because the
   normal `main()` calls `SDL_Init` before the Emulator sees `headless=`. With
   a logged-in session (display asleep) `headless=` alone switches the video
   driver to dummy inside the Emulator constructor.
6. **Code-space addressing.** `read-code`/`write-code` and execution
   breakpoints use the debugger's code-space convention. EPS and T4x cores are
   word-addressed; legacy CPU breakpoints are byte offsets in the disassembly.
   `read-rom`/`write-rom` operate on the raw ROM image.
7. **TI MultiView writes require pause.** The T4x core ignores memory and
   register writes while running; the server returns `not-paused` rather than
   failing silently.
8. **Recording** is requested from the UI thread and performed by
   `ScreenOutput` on the render path (the same as the F12/UI recording toggle).
   `hw record` reports the request and the current `recording_active` flag; it
   does not wait for the encoder to finish. The macOS backend pipes frames to
   `ffmpeg`; when `ffmpeg` is absent the existing encoder still reports success
   and the process dies on the first pipe write (SIGPIPE). The control server
   therefore preflights `ffmpeg` on `PATH` and returns
   `unsupported: screen recording needs ffmpeg on PATH` instead of crashing.
   The root cause is outside the files this change owns; the one-line defensive
   fix for `CasioEmuMsvc/src/Peripheral/ScreenOutput.cpp` is to ignore SIGPIPE
   (`signal(SIGPIPE, SIG_IGN);`) before starting the encoder or to check for
   the executable before `encoder.Start(command)`. `screenshot` is the safe
   headless capture path.
9. **Platforms.** The server is macOS-only in this change (`#if __APPLE__`).
   The tool target configures only on POSIX desktop platforms. Windows uses the
   existing MCP plugin.
10. **Multiple sessions.** One process runs one emulator at a time; closing the
   calculator and opening another model creates a new session (same pid, new
   socket file). There is no session history.

No forbidden file needed a patch. The only hooks are in
`CasioEmuMsvc/src/Emulator.{hpp,cpp}`, which this change owns. The exact hook
surface is:

```cpp
// Emulator.cpp — Frame(), first statement
ctl::OnFrame(*this);
// Emulator::Shutdown() and ~Emulator()
ctl::OnShutdown(*this);
```

---

## 7a. Build blockers in the preview tree (exact patches)

The preview branch (`ba04342`) is developed by several agents at once. During
this pass two transient issues in files this change does not own blocked the
desktop configure/build; both were fixed by their owners later the same day.
Exact one-line fixes (kept here for anyone hitting the same preview state):

1. `CasioEmuMsvc/src/Ext/MacWindow.mm:33` uses an AppKit constant that does not
   exist (`NSWindowTitlebarSeparatorStyleNone`). Change it to
   `NSTitlebarSeparatorStyleNone`:
   ```diff
   -		ns_window.titlebarSeparatorStyle = NSWindowTitlebarSeparatorStyleNone;
   +		ns_window.titlebarSeparatorStyle = NSTitlebarSeparatorStyleNone;
   ```
   Local workaround without editing the file (used for this pass):
   `-DCMAKE_OBJCXX_FLAGS=-DNSWindowTitlebarSeparatorStyleNone=NSTitlebarSeparatorStyleNone`.
2. `CasioEmuMsvc/CMakeLists.txt` calls `set_tests_properties(plugin_loader_smoke …)`
   and `set_tests_properties(plugin_registry_headless …)` **before** their
   `add_test` lines; CTest rejects properties for a test that does not exist
   yet. Move each `set_tests_properties` call below its `add_test`, or configure
   with `-DCASIOEMU_PLUGIN_TESTS=OFF` (used for this pass).

`Gui/Shell/*`, `Terminal/*` and `StartupUi/*` were also being actively edited by
other agents during the same window and intermittently failed to compile; they
are not part of this change. When they settled, the whole app built. The Ctl
translation units compiled throughout
(`CtlServer.cpp.o`, `CtlCommands.cpp.o`, `CtlJson.cpp.o`, `CtlSession.cpp.o`),
and `casioctl` builds and its CTest suite passes independently.

All scratch data used by `casioctl selftest` and the end-to-end test lives under
`temp/casioctl` in the workspace root (override with `CASIOCTL_TEMP_ROOT`);
nothing is written to `/tmp` or `TMPDIR`. The test removes its subdirectory on
exit.

## 8. Testing

* `casioctl selftest` — 59 checks covering JSON, CLI parsing, discovery,
  handshake, framing and error handling. No emulator or display needed.
* `casioctl-e2e` — a real end-to-end test: starts the actual emulator with
  `CASIOEMU_HEADLESS=1 model=… headless= paused=`, waits for
  `casioctl sessions`, then scripted operations: hello/status, register read,
  memory read/write round trip, disassembly, breakpoint set/list/clear,
  `step 2`, `step-over`, KI/KO press/release, `releaseall`, an internal PNG
  screenshot (verified by magic bytes), snapshot save/list/load/delete,
  address watch set/list/delete, call recording start/resume/pause/list/stop,
  ROM and code reads, hardware/display state, labels, variables, backtrace and
  memory breakpoints. The emulator is always terminated and its session files
  removed.
* CTest:

```sh
cmake --build build --target CasioEmuMsvc casioctl
ctest --test-dir build/tools/casioctl --output-on-failure
```

  `casioctl-e2e` exits with 77 (CTest "skipped") when no model directory is
  available. Set `-DCASIOCTL_E2E_MODEL=/path/to/model` (a cache variable) or
  the `CASIOCTL_TEST_MODEL` environment variable at configure time.

* Manual live smoke test: run the app with a model, then

```sh
casioctl sessions
casioctl -s <pid> hello
casioctl -s <pid> status
casioctl -s <pid> pause
casioctl -s <pid> regs
casioctl -s <pid> read 0xE9E0 16
casioctl -s <pid> disasm 0x0 4
casioctl -s <pid> key list
casioctl -s <pid> screenshot ./calc.png
casioctl -s <pid> resume
```

---

## 9. Troubleshooting

| symptom | cause / fix |
| --- | --- |
| `no live CasioEmuMsvc sessions` | app not running, headless build, or a second user. `ls "$HOME/Library/Application Support/CasioEmuMsvc/ctl"` |
| session listed but connect refused | app is shutting down or crashed; wait for the window to close and reopen the model |
| `protocol handshake failed` | version mismatch (old app, new CLI); rebuild both |
| `debugger-unavailable` | debugger windows disabled in this build |
| `not-paused` | `pause` before writing on a TI MultiView model |
| `no-frame` | wait for the calculator to render once, then retry `screenshot` |
| socket path too long | set `CASIOEMU_CTL_DIR` to a shorter directory for both app and CLI |
| commands feel slow | `step n` is one instruction per frame; use `resume` + `bp set` for long runs |

---

*Protocol version 1. Implemented in `CasioEmuMsvc/src/Ctl/` (server) and
`tools/casioctl/` (CLI).*
