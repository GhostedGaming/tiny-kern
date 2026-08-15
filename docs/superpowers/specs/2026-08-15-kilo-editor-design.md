# Design: Port kilo as the tiny-kern editor

Date: 2026-08-15

## Goal

Replace the broken custom editor (`userspace/edit.c`) with a port of the
canonical antirez `kilo` editor (~1100 lines, public domain). `kilo` becomes
the editor shipped in `/ram/bin/`, and remains compilable in-guest with tcc
from `/ram/usr/src/`.

## Context

The existing editor has accumulated defects (raw-mode ESC hangs, `:q` needing
two keypresses, long-line corruption). Kilo is a small, battle-tested terminal
editor and a clean fit for the tiny-kern syscall surface.

Verified available (no kernel changes required):

- **VTIME timed reads**: `tty_read()` (`kernel/src/tty.c:466`) applies a
  per-read deadline when `c_cc[VTIME] != 0` and returns 0 on timeout. This
  replaces kilo's `ioctl(FIONREAD)` for lone-ESC detection.
- **Terminal size**: `SYS_TTYINFO` returns `struct ttyinfo { rows, cols }`
  (`kernel/include/tty.h:86`). The direct-syscall wrapper pattern already
  exists in `userspace/edit.c:48`.
- **ANSI escapes** kilo emits are handled by `tty_esc_finish` (`tty.c:285`):
  `[2J`, `[H`, `[%d;%dH`, `[K`, `[A/B/C/D`, `[%dC` (tabs), SGR `m`
  (7 = reverse, 0 = reset). `[?25l` / `[?25h` (hide/show cursor) are parsed
  and ignored — a clean no-op.
- **Key codes** delivered as sequences matching kilo's parser (`input.c:103`):
  arrows `[A/B/C/D`, Home `[H`, End `[F`, PageUp `[5~`, PageDown `[6~`,
  Insert `[2~`, Del `[3~`; Ctrl+letter becomes a control char (`input.c:128`).
- **libc**: `fstat`, `snprintf`, `realloc`, `strdup`, `memmove`, `atexit`
  available in mlibc.

## The port

Single file `userspace/kilo.c` based on antirez `kilo` with these
platform adaptations:

1. **Raw mode** (`enableRawMode`): set `c_cc[VMIN] = 0`, `c_cc[VTIME] = 1`
   (kilo's own settings) so every `read` gets a ~0.1 s timeout window.
   Disable `ICANON | ECHO | ISIG | IEXTEN | IXON`, set `CS8`, as kilo does.
   `atexit(editorAtExit)` restores the terminal on exit (fallback to a direct
   restore call if mlibc `atexit` is unavailable).
2. **Key reading** (`editorReadKey`): same structure as kilo. ESC handling
   drops the `ioctl(FIONREAD)` check — a continuation `read` that returns 0
   (or < 1) means "lone ESC". All reads run under the VTIME window, so arrow
   sequences (bytes already queued) return instantly.
3. **Window size** (`getWindowSize`): call `tty_getinfo()` via `SYS_TTYINFO`
   instead of `ioctl(TIOCGWINSZ)`. Drop the `\x1b[6n` cursor-position-report
   fallback (the tty does not answer it); return -1 if `tty_getinfo` fails.
   Remove `getCursorPosition()`.
4. **SIGWINCH**: not registered (tty is fixed-size).
5. Full canonical feature set retained: arrows, PageUp/PageDown, Home/End,
   Backspace/Del, Enter, insert-at-cursor, Ctrl-Q quit, Ctrl-S save, Ctrl-F
   search, dirty flag, status bar, tab rendering, `[?25l/h` emitted (no-op).
6. Clean under `-Wall -Wextra` (no unused-parameter warnings; only the
   parameters the port uses).

## Layout and build

- `userspace/kilo.c` — the port. Built to `bin/kilo` by the generic
  `$(BINDIR)/%: %.c` rule (`userspace/GNUmakefile:34`).
- `userspace/edit.c` — deleted.
- `bin/edit` — becomes a copy of `bin/kilo` so `/ram/bin/edit` keeps working
  (`userspace/GNUmakefile`: `bin/edit: bin/kilo ; cp $< $@`).
- `userspace/usr/src/kilo.c` — copy of `kilo.c` for in-guest tcc builds
  (`tcc /ram/usr/src/kilo.c` then `./a.out`). Deleted: `userspace/usr/src/edit.c`.
- `bin/edit` and `usr/src/*` are packaged into `userspace.tar` and the ISO by
  the existing top-level `make` flow.

## Testing (in QEMU)

For both the gcc-built `/ram/bin/kilo` and the tcc-built `./a.out`, on a test
file under `/ram`:

- open file; status bar shows filename/line count
- type characters (insert-at-cursor); Enter adds a line
- arrows, Home/End, PageUp/PageDown move/view correctly
- Backspace/Del edit the buffer; dirty flag (`*`) appears
- lone ESC does nothing (no hang, no garbage)
- Ctrl-F search finds a literal string; Enter/Ctrl-Q exits search
- Ctrl-S saves; `cat` on the host side confirms content
- Ctrl-Q quits and restores the terminal
- confirm `/ram/bin/edit` runs the same editor
- confirm old `edit.c` references are gone from the guest

## Out of scope

- Colors beyond reverse-video SGR 7 / reset (tty limitation, cosmetic).
- Cursor hide/show (no-op on this tty, harmless).
- Resize handling / SIGWINCH.
- Anything beyond the canonical antirez kilo feature set.
