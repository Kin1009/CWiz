# HEXD RSC reverse-engineering notes

This document records the behavioral reconstruction of
`tools/rac-compiler/rsc_ropchain/hexd.rsc`. It is intentionally separate from
the eventual CWiz implementation; the RSC is not used as an implementation
backend.

## Entry and state

The chain is copied by the launcher from the backup section (`0xDF60`) to its
execution section (`0xDDE0`). `address` is a word containing `0xD550`, the
RAM location used as the editor's current address/value state. The text
objects `address_text` and `value_text` are mutable display buffers.

The chain is a self-restoring event loop: `loop` restores the payload and
jumps back through the launcher path after each event. This is necessary
because the keyboard/display firmware calls clobber the ROP stack and working
registers.

## Reconstructed control flow

* `program`/`display` read the current address and byte, convert both to two
  hexadecimal characters, update `address_text`/`value_text`, and render the
  display line.
* `process_val_key` obtains the raw key word through `getkey`, dispatches it
  through `val_table`, applies the selected address/value delta, and restores
  the event stack.
* `set_sp` switches to the small-font/display stack and reloads the current
  address pointer.
* `check_func_key` converts the raw key word and dispatches by the function-key
  table. The table routes ordinary type/navigation processing to
  `process_type_key`, injection to `inject`, and the default path back to the
  event loop.
* `process_type_key` updates the current pointer/type state at `0xD150`, then
  dispatches through `type_table`.
* `inject` converts the editable hexadecimal text to a byte with
  `string_to_byte`, writes the result to the address held at `0xD550`, and
  refreshes the pointer/value state.
* `loop` performs the payload restore/copy operation and re-enters the event
  loop.

## Tables and data

`val_table` contains six-byte dispatch records. Four records represent the
direction/value deltas, one selects `set_sp`, and the zero/default record
returns to function-key processing. `func_table` selects the type handler or
the loop. `type_table` selects injection or loop. The `-2` target bias is a
property of the `sp=er6,pop er8` branch gadget, not application logic.

The raw table words decode as follows (little endian):

| raw key | table delta | target | verified interpretation |
|---:|---:|---|---|
| `0x0440` | `-1` | `check_func_key` | left/decrement |
| `0x0880` | `+1` | `check_func_key` | right/increment |
| `0x0840` | `+0x0100` | `check_func_key` | down/next row |
| `0x0480` | `-0x0100` | `check_func_key` | up/previous row |
| `0x1004` | `+1` | `set_sp` | EXE/type transition |
| `0x0000` | `0` | `check_func_key` | no-key/default |

## Observable behavior to preserve

The implementation must preserve the editable address/value state, raw
keyboard semantics, hexadecimal conversion, memory writes, display refreshes,
payload restoration between events, and the boundary behavior encoded by the
dispatch tables. The compiler may use different storage and control flow, but
these effects are part of the program contract.

## Translation boundary

The direct RSC statements (`pop`, `eval`, `adr`, register assignments, and
tables) are implementation details of the original chain. They are not the
CWiz implementation. The eventual source must express the reconstructed
state machine and use compiler/runtime support only for genuinely specialized
keyboard, display, memory, and ROP-launch operations.

## Operation mapping

| RSC sequence | Verified role | CWiz-side design |
|---|---|---|
| `er0=[er2],r2=9,rt` / `r0=[er0]` / `hex_byte` | Read the byte at the current address and format two hex characters | Generic runtime `read_u8` plus `format_hex8` lowering |
| `eval(adr(address_text)+...)` / `smart_printline` | Update and render the address/value text | Normal text-buffer render operation |
| `getkey` + `cmp_ea` | Read the raw KI/KO word and dispatch exact key values | Generic raw-key read and key-table lowering |
| `qr0=[ea]...` / `[er8]+=er2` | Load a table delta and update the current state word | Generic table dispatch plus `add_u16` lowering |
| `cvt_keycode` / `func_table` | Convert raw key word to function-key class | Generic key conversion and dispatch lowering |
| `string_to_byte` / `[er2]=r0` | Convert the edited hexadecimal text and write one byte | Generic `parse_hex8` plus `write_u8` lowering |
| `memcpy_auto_jump` | Restore the payload and resume the event loop | ROP launcher/runtime support, not application logic |

The first six rows are application semantics. The final row is a ROP
execution constraint and can be implemented by the backend without exposing
the original instruction sequence in the application source.
