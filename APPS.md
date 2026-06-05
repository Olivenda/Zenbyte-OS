# Writing Zenbite Apps — the .ZBX format

This doc shows how to write windowed Zenbite programs in C, package
them as `.ZBX` executables, and run them from the shell or the
desktop's **Programs** widget. The format is small, the runtime is
small, and the language is a strict subset of C — read this once and
you'll know everything that's available.

## The 5-second version

```c
int main() {
    int k;
    cls(0x11);
    window(6, 18, 44, 8, "Hello window");
    at_puts(9,  21, 0x0F, "This is a .ZBX windowed program.");
    button(13, 32, 11, 0x0F, 1);
    at_puts(13, 35, 0x0F, "OK");
    present();
    k = waitkey();
    return 0;
}
```

Save as `HELLO.C`, then in the Zenbite shell:

```
A:\> mkzbx HELLO.C HELLO.ZBX -f
A:\> run HELLO.ZBX
```

`-f` marks it fullscreen — the shell clears the screen before/after.
Drop the flag if your program prints to stdout instead of drawing.

The same `.ZBX` shows up in the desktop's **Programs** widget (F9 →
Productivity → Programs) and runs from there too.

---

## The file format

```
offset 0   : magic 'Z' 'B' 'X' '1'                (4 bytes)
offset 4   : flags byte                            (1 byte)
                bit 0 = fullscreen (1) / console (0)
                bits 1..7 reserved, must be 0
offset 5   : reserved                              (3 bytes, must be 0)
offset 8.. : C source text                         (rest of file)
```

The interpreter strips the header and parses the source on every run
(no separate compile + link step). Files without the magic are
treated as plain C source — your raw `.c` runs unchanged. The
`mkzbx` command just writes the header bytes and copies the source.

---

## What C looks like in Zenbite

Zenbite ships **zbc**, a tiny tree-walking interpreter for a useful
slice of C. If you stay inside this slice your code runs; if you
reach outside it the parser will tell you.

### What works

- **int variables** (global + local). 32-bit signed.
- **int literals**: decimal `42`, hex `0x1F`.
- **char literals**: `'a'`, `'\n'`, `'\t'`, `'\\'`, `'\''`, `'\0'`.
  They become the byte's int value.
- **String literals**: `"hello"`. Pass them to builtins. There are no
  string variables — you can't assign or modify them.
- **Operators**: `+ - * / %`, `== != < > <= >=`, `&& || !`, `& | ^ << >> ~`,
  `=  +=  -=  *=  /=  %=`.
- **Ternary**: `a ? b : c` — conditional expression.
- **Comma**: `a, b` — evaluates both, returns right.
- **sizeof**: `sizeof(int)` — always returns 4.
- **Control flow**: `if / else`, `while`, `for`, `do/while`, `return`,
  `break`, `continue`.
- **switch/case**: `switch(x) { case 1: ... break; default: ... }`
- **Functions**: `int name(int a, int b) { ... }` or
  `void name(...) { ... }`. Up to 8 arguments.
- **Declarations** with commas: `int a, b = 3, c;`
- **Structs**: `struct Foo { int x; int y; };` (basic support)
- **Arrays**: `int arr[10];` via pool allocator.
- **Pointers**: Basic `*ptr` dereference and `&var` address-of.
- **`#include <...>`** and other `#` lines: silently accepted and
  ignored. The runtime is always available.
- **Comments**: `// ...` and `/* ... */`.

### What's not supported

- No **floats** or **doubles**. (Use scaled ints if you need fractions.)
- No **string variables** — only literals.
- No **function pointers** — no callbacks.
- No **`static` locals**, no **enums**.
- No **multi-file compilation** — one file at a time.
- No **goto**.
- No **multi-file linking** — one source per program.

Hard limits per program: 64 functions, 64 vars per scope, 4096 AST
nodes, ~8 KiB source text.

---

## Runtime API ("mini-libc")

Every function below is built into the interpreter. No `#include`
needed.

### Console I/O

```c
puts ("hello")                  // string + newline
putchar (ch)                    // one character
printf ("fmt", ...)             // %d %i %u %x %c %s %%
print (n)                       // decimal int + newline
getchar ()                      // wait for one key, return its code
waitkey ()                      // alias for getchar
key ()                          // non-blocking; -1 if no key
```

### Screen (TUI) — coordinates in cells (row, col)

```c
cls (color)                     // fill the whole viewport
putcell (row, col, ch, color)   // one cell
at_puts (row, col, color, "str")// horizontal string
present ()                      // flush draws to the display
scr_rows ()                     // height in cells (25 or 50)
scr_cols ()                     // width in cells  (80)
```

### Windowing

```c
window (r, c, w, h, "Title")    // Mac-style window with title bar +
                                //   3 traffic-light dots + shadow
frame  (r, c, w, h, color)      // single-line CP437 box, no chrome
button (r, c, w, color, hot)    // bracketed [ ... ] cell, hot=1 inverts
```

### Mouse

```c
mouse_x ()                      // current column
mouse_y ()                      // current row
mouse_btn ()                    // bitmask: 1=left, 2=right, 4=middle
```

### Files

```c
fopen   ("path")                // fd >= 0, or -1
fcreate ("path")                // create + open; -1 on failure
fgetc   (fd)                    // byte 0..255, or -1 on EOF
fputc   (fd, ch)
fclose  (fd)
```

Up to 8 program-local fds. They're auto-closed when `main` returns.

### Time

```c
ticks ()                        // PIT tick count (100 Hz)
delay (n_ticks)                 // sleep ~10*n ms; HLTs internally
```

### Color byte format

A single `u8` color packs **fg** in the low nibble and **bg** in the
high nibble:

```
color = (bg << 4) | fg
```

Example: `0x4F` = white text on red. `0x1E` = yellow on blue.
Use the **COLORS.ZBX** sample to see all 256 combinations.

---

## Tutorial 1 — "Hello window"

```c
int main() {
    int k;
    cls(0x11);                        // wallpaper: blue/blue, fills screen
    window(6, 18, 44, 8, "Greetings");// title-bar window at (row 6, col 18),
                                      //   44 cells wide, 8 tall
    at_puts(9,  21, 0x0F, "Hello, world!");
    at_puts(11, 21, 0x0F, "Press any key to exit ...");
    present();                        // flush to the screen
    k = waitkey();
    return 0;
}
```

`cls` paints the desktop. `window` draws the chrome. `at_puts` writes
text. `present` makes it visible (the runtime buffers draws to avoid
flicker — nothing shows until you flush).

Build + run:

```
A:\> mkzbx HELLO.C HELLO.ZBX -f
A:\> run HELLO.ZBX
```

## Tutorial 2 — Click handling

```c
int main() {
    int mx, my, mb;
    cls(0x11);
    window(5, 14, 50, 14, "Click counter");
    int n; n = 0;
    button(11, 30, 10, 0x2F, 0);      // dark green [ ... ] button
    at_puts(11, 32, 0x2F, "Click");
    for (;;) {
        at_puts(8, 18, 0x0F, "clicks: ");
        printf("%d  ", n);
        present();
        mb = mouse_btn();
        if (mb & 1) {                 // left button down
            mx = mouse_x(); my = mouse_y();
            if (my == 11 && mx >= 30 && mx < 40) n = n + 1;
            /* debounce: wait for release */
            while (mouse_btn() & 1) delay(2);
        }
        if (key() == 27) return 0;    // ESC quits
        delay(2);
    }
}
```

Pattern:
1. `present` after every visible change.
2. `key()` for non-blocking keyboard polling, `27` is ESC.
3. `mouse_btn() & 1` for left-click. Always wait for the release
   inside the same handler, or one press will register many times.

## Tutorial 3 — switch/case

```c
int main() {
    int choice;
    cls(0x1B);
    window(5, 15, 50, 12, "Menu");
    at_puts(7, 18, 0x0F, "1. Option One");
    at_puts(8, 18, 0x0F, "2. Option Two");
    at_puts(9, 18, 0x0F, "3. Option Three");
    at_puts(11, 18, 0x0E, "Enter choice (1-3): ");
    present();
    /* Read choice */
    choice = 0;
    for (;;) {
        int k = waitkey();
        if (k >= '1' && k <= '3') {
            choice = k - '0';
            printf("%d", choice);
            break;
        }
        if (k == 27) return 0;
    }
    delay(50);
    cls(0x1B);
    window(6, 20, 40, 8, "Result");
    switch (choice) {
        case 1: at_puts(9, 24, 0x0A, "You picked ONE!"); break;
        case 2: at_puts(9, 24, 0x0B, "You picked TWO!"); break;
        case 3: at_puts(9, 24, 0x0C, "You picked THREE!"); break;
        default: at_puts(9, 24, 0x0E, "Invalid choice"); break;
    }
    present();
    waitkey();
    return 0;
}
```

Key points:
- `switch(expr)` evaluates the expression once.
- `case N:` matches when the value equals N.
- Always use `break` to prevent fall-through.
- `default:` handles unmatched values.

## Tutorial 4 — Ternary operator

```c
int main() {
    int age, result;
    age = 25;
    /* Ternary: condition ? value_if_true : value_if_false */
    result = (age >= 18) ? 1 : 0;
    if (result) {
        puts("You are an adult.");
    } else {
        puts("You are a minor.");
    }
    return 0;
}
```

The ternary operator is a compact if/else for expressions:
`condition ? expr_if_true : expr_if_false`

## Tutorial 5 — Working with files

```c
int main() {
    int f, c;
    f = fopen("README.TXT");
    if (f < 0) { puts("not found"); return 1; }
    for (;;) {
        c = fgetc(f);
        if (c < 0) break;
        putchar(c);
    }
    fclose(f);
    return 0;
}
```

That's `CAT.ZBX`. Drop the `-f` flag when building (it's console, not
fullscreen).

---

## Packaging

```
mkzbx <source.c> <output.zbx> [-f]
```

`-f` sets the fullscreen flag in the header. The shell uses the flag
to decide whether to clear the screen before/after the run.

The output is your source with an 8-byte header prepended. You can
inspect it with `cat output.zbx` — readable.

To **distribute** a program: copy the `.ZBX` to another Zenbite disk
or include it in the build (drop it into `$STMP/SYSTEM/BIN/` in
`scripts/mkdisk.sh` so it ships with the installer).

---

## Cross-compile from host

A plain `.c` file (no header) also runs as a .ZBX — the loader
treats missing magic as "this is just source". So you can author on
your Mac/Linux box with your editor of choice and `scp` / `mcopy`
the file in. Either of these is valid input to `run`:

```
NICE.ZBX           (ZBX1 + flags + source)
NICE.C             (raw source, no header)
```

For ZBX format with the fullscreen flag set, prepend the header
yourself:

```bash
printf 'ZBX1\1\0\0\0' | cat - nice.c > NICE.ZBX
```

Then `mcopy -i zenbite_target.img@@0 NICE.ZBX ::/NICE.ZBX`.

---

## Launching

**From the shell:** `run NICE.ZBX` runs it; the exit code is shown.
Add a path if it's not in CWD: `run \SYSTEM\BIN\NICE.ZBX`.

**From the desktop:** open the **Programs** widget (F9 → Productivity
→ Programs). It lists every `.ZBX` in the current drive root **and**
every `.ZBX` in `\SYSTEM\BIN`. Highlight one, press ENTER. Fullscreen
apps run with `tui_end` bracketing (you'll see them, then "press a
key" returns to the desktop).

---

## Common gotchas

1. **Forgetting `present()`** — your draws are buffered. Without
   `present`, the screen never updates. Call it once per frame, after
   you finish a logical batch of writes.

2. **Tight loops without `delay()` or `key()`** — burn 100% CPU and
   make the system unresponsive. Always include `delay(n)` or a
   blocking `waitkey()` in the loop body.

3. **Hitting the source-size limit** — programs are capped at ~8 KB
   of source. Refactor into more functions if you grow past it.

4. **No string variables means** you can't load text from a file into
   a buffer and pass that to `at_puts`. You can read bytes and
   `putchar` them, or `at_puts(r, c, color, "literal")`.

5. **Char literals** lex as ints. `if (c == 'q')` is fine; `c = 'q';`
   is fine; assigning to a "string" isn't a thing.

6. **`return` from main** is the exit code. `return 0;` for success.

---

## Reference shipped programs

These live in `\SYSTEM\BIN\` on every install and are also good
reading material. The Programs widget lists them automatically.

| File          | Style       | Demonstrates                              |
|---------------|-------------|-------------------------------------------|
| `CAT.ZBX`     | console     | `fopen` / `fgetc` / `fclose`              |
| `CLOCK.ZBX`   | fullscreen  | `cls` + `putcell` + tick math + `key()`   |
| `COLORS.ZBX`  | fullscreen  | 16x16 palette grid, color byte layout     |
| `COUNT.ZBX`   | console     | `waitkey` keyboard scan codes             |
| `GUESS.ZBX`   | console     | LCG PRNG, digit parsing, control flow     |
| `INFO.ZBX`    | console     | `scr_rows` / `scr_cols` / `ticks`         |
| `WHELLO.ZBX`  | fullscreen  | minimal `window()` + `button()`           |
| `ADDER.ZBX`   | fullscreen  | window + multi-step input                 |
| `WCLOCK.ZBX`  | fullscreen  | window + animated content + `delay`       |

If your program is small and self-contained, drop it into
`scripts/mkdisk.sh` inside the `$STMP/SYSTEM/BIN/` block — it ships
with every install after the next `make`.

---

## Honest limits — what you CAN'T build (yet)

Because zbc has no pointers / arrays / structs, you can't write the
kind of programs that need:

- A large in-memory text buffer (Editor).
- Multi-cell game state (Tetris's board, Minesweeper's grid).
- Parsed structured data (HTML, ELF, FAT directory entries).
- Linked lists, trees, queues.

The desktop's built-in apps (Files, Editor, Tetris, etc.) stay in
the kernel for that reason — they need pointers + structs that the
interpreter doesn't have. Future work: when zbc grows arrays, more
of them will become portable to `.ZBX`.

For now, `.ZBX` is the right home for **single-function tools** —
calculators, clocks, viewers, demos, file utilities, palette
visualisers, system probes. The shipped programs are sized to fit
that envelope.

---

# Zenbite 3.1 — windowed apps with mkzbx, end-to-end

This is the walkthrough version of "make a windowed app on Zenbite".
It uses the same zbc subset, but builds a complete two-button
calculator screen so you can see every piece working together. Drop
the source into `\SAMPLES\CALC.C`, `mkzbx` it, run it.

## 1. Where your app runs

Zenbite has two desktops; your `.ZBX` works in both, plus from the
shell:

| Where        | How to launch                       | What your draws hit                  |
| ------------ | ----------------------------------- | ------------------------------------ |
| Shell        | `run YOURAPP.ZBX`                   | The screen directly (fullscreen)     |
| Classic UI   | F9 → Productivity → Programs        | Same — bracketed with `tui_end`      |
| Slate UI     | (gdesk launcher coming in 3.2)      | Same — bracketed with `vga_set_text` |

There's no "windowed app inside a host window" right now. Your `.ZBX`
takes the whole screen when it runs. You draw your own window chrome
with `window(...)` and `frame(...)` — that's why those builtins exist.

## 2. The Hello-world skeleton

```c
int main() {
    int k;
    cls(0x11);                                /* solid slate-blue fill */
    window(5, 12, 56, 14, "My app");          /* title-bar window */
    at_puts( 8, 16, 0x0F, "Hello, Zenbite 3.1!");
    at_puts(10, 16, 0x0F, "Press any key to exit.");
    present();                                /* flush to screen */
    k = waitkey();
    return 0;
}
```

Save as `HELLO.C`, then:

```
A:\> mkzbx HELLO.C HELLO.ZBX -f      ; -f marks fullscreen
A:\> run HELLO.ZBX
```

Always end with `present()` — drawing primitives buffer into a
shadow plane until you flush, so a missing `present()` shows you
the *previous* frame.

## 3. The calculator example

A real interactive app. Two inputs, four operators, ENTER computes.

```c
int read_signed() {
    int v = 0, sign = 1, k;
    for (;;) {
        k = waitkey();
        if (k == 27)           return 0;      /* ESC = give up */
        if (k == 13 || k == 10) return v * sign;
        if (k == '-' && v == 0) { sign = -1; putchar('-'); present(); }
        if (k >= '0' && k <= '9') {
            v = v * 10 + (k - '0');
            putchar(k); present();
        }
    }
}

int compute(int a, int b, int op) {
    if (op == '+') return a + b;
    if (op == '-') return a - b;
    if (op == '*') return a * b;
    if (op == '/') return b ? a / b : 0;
    return 0;
}

int main() {
    int a, b, op, k;
    cls(0x11);
    window(5, 14, 50, 16, "Calc");
    at_puts( 8, 18, 0x0F, "Two-number calculator. ESC = quit.");
    at_puts(11, 18, 0x0F, "first  : ");
    /* The cursor is wherever putchar last drew. */
    a = read_signed();
    at_puts(12, 18, 0x0F, "op (+-*/): ");
    op = waitkey(); putchar(op); present();
    at_puts(13, 18, 0x0F, "second : ");
    b = read_signed();
    at_puts(15, 18, 0x4F, " = ");           /* yellow on red strip */
    printf("%d", compute(a, b, op));
    present();
    waitkey();
    return 0;
}
```

Build + run:

```
A:\> mkzbx CALC.C CALC.ZBX -f
A:\> run CALC.ZBX
```

`mkzbx` packages the source with the ZBX header. The interpreter
(zbc) parses it on every `run`; there's no separate compile step.

## 4. The runtime cheat-sheet

Console:

```c
puts("hi")              putchar('a')           getchar()      waitkey()
printf("%d %s\n", ...)  print(n)               key()    /* non-blocking, -1 if none */
```

Screen drawing — coordinates are 25-row / 80-col cells:

```c
cls(0x11)                 /* fill with color byte (bg<<4 | fg) */
putcell(row, col, ch, color)
at_puts(row, col, color, "literal")
present()                 /* flush draws */
scr_rows()                scr_cols()
```

Windowing (3.1):

```c
window(r, c, w, h, "Title")   /* Mac/Win-style chrome with title bar */
frame (r, c, w, h, color)     /* plain single-line box */
button(r, c, w, color, hot)   /* [   ] cell button; hot=1 inverts */
```

Mouse:

```c
mouse_x()    /* current column                   */
mouse_y()    /* current row                      */
mouse_btn()  /* bitmask: 1=left, 2=right, 4=mid  */
```

Files:

```c
int fd  = fopen("NAME.TXT");   /* >=0, or -1 */
int fd2 = fcreate("OUT.TXT");
int byte = fgetc(fd);          /* 0..255, -1 EOF */
fputc(fd2, 'X');
fclose(fd);
```

Time:

```c
int t = ticks();   /* PIT @ 100 Hz */
delay(50);         /* sleep 50 ticks; HLTs internally */
```

Color byte: `(bg << 4) | fg` over 4-bit VGA colours.

## 5. Common gotchas

1. **Missing `present()`** — your draws stay invisible until you flush.
2. **No string variables** — you can `at_puts(..., "literal")` but
   can't assign a string to a variable. Use `putchar` for dynamic chars.
3. **No arrays / structs / pointers yet** — keep state in scalar ints.
   For a list of values, use loops + recompute, not an array.
4. **Char literals** lex as ints; `if (c == 'q')` works fine.
5. **`return` from main** is the exit code (shows as `[exit N]`).

## 6. Distributing your app

Drop the `.ZBX` somewhere on the disk and `run PATH\YOURAPP.ZBX`. To
have it ship with the install image, add it to the SYSTEM/BIN tree in
`scripts/mkdisk.sh` — the script copies everything in `$STMP/SYSTEM/BIN/`
to all build outputs (floppy, CD, USB, install disks).

## 7. Shipped reference programs (3.1)

| File                | Notes                                                  |
| ------------------- | ------------------------------------------------------ |
| `HELLO.ZBX`         | minimal puts() + return                                |
| `CALC.ZBX`          | two-number calculator (this walkthrough)               |
| `CLOCK.ZBX`         | live HH:MM:SS, polls `ticks()`                         |
| `COLORS.ZBX`        | the 16-color palette in a 16×16 grid                   |
| `COUNT.ZBX`         | demo of `waitkey()` + numeric input                    |
| `GUESS.ZBX`         | tiny number-guessing game                              |
| `INFO.ZBX`          | prints `scr_rows()/scr_cols()/ticks()`                 |
| `WHELLO.ZBX`        | minimal `window()` demo                                |
| `ADDER.ZBX`         | two-input adder using `frame()`                        |
| `WCLOCK.ZBX`        | windowed clock                                         |
| `BOXES.ZBX`         | TUI primitive showcase                                 |

All of these are good starting points — read the source, copy what
you need.
