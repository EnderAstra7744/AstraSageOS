# AstraScript — Engine 2

Files that start with `start;` run on this engine (tree-walking interpreter, C, no libc, no libgcc). Older files without `start;` still run on the legacy line interpreter (`sys/astrascript.c`) exactly as before.

## Layout
- `include/` — `astrascript.h` (public API), `as_types.h`, `as_memory.h`, `as_host.h` (what the host must provide), `lib/*.ash` (built-in modules, e.g. `color.ash`)
- `src/` — lexer, parser, interpreter, types, memory (4 MiB arena), strings, collections, functions, classes, builtins, modules, errors, engine
- `tests/` — `t_*.as` programs, `host_main.c` (runs them on a normal computer), `run_tests.py`; `make -C AstraScript test`
- `cpp/` — empty on purpose: nothing was moved to C++ because no measurement showed a benefit.
- Kernel side: `sys/as_host_kernel.c`; routing in `sys/astrascript.c` (`asc_run`, `asc_check`).

## Which interpreter runs a file
- `start;` first line: engine 2 (strict layout rules, `main()` starts by itself).
- No `start;`: a file that uses the new language (True/False/None, `vr bool/list/dict/...`, classes, methods `x.f()`, `[ ]`, `#import`, `yield`, `with`, `lambda` ...) also runs on engine 2, in relaxed mode: a line break ends a statement, `;` is optional, `main()` does not start by itself. If such a file has a syntax error, the message comes from engine 2.
- Every other old file keeps running on the old line interpreter, exactly as before.

## Language (implemented)
True/False/None, and/or/not, in/not in, is/is not, if/elif/else, while/for/break/continue/pass, mtch/cse, functions (defaults, `*args`, `**kwargs`, keyword args, lambda, closures, `nonlocal`/`global`), classes with `init(self,…)` and inheritance (`super()`), `ask { } error_for Type as e { }`, `push_error`, `assert`, `with` (enter/exit), `import`, `#import <x.ash> as x`, `#from <x.ash> import Name`, `use Package;`, `as <command>;`.
Types: int (32-bit, overflow → float), flo, bool, strt, None, bytes, bytearray, list, tuple, set, frozenset, dict, range. Methods: see the spec (`bigger`, `smalest`, `rear`, `clear_end` …). Interpolation: `%strt%inter"Hi {x}"`, `%flo"…*/x*"`.
Graphics: `Init(Name=…, Size=(w,h), Icon=…)`, `gui_*`.
Modules are looked up in your folder, then built in (`include/lib`).

## Added after the first release
Chained comparisons (`1 < x <= 5`, middle value evaluated once), comprehensions (`[x*x for x in items if c]`, `{x for …}`, `{k: v for …}`, several `for`/`if` clauses, own scope), `classmethod(f)`, `staticmethod(f)`, `property(getter, setter)` (call form inside a class body; there is no decorator syntax), `open(name, "r"|"w"|"a")` with `read/readline/readlines/write/close` and `with`, generators with `yield`, `Init(Icon="file.bmp")` (BMP up to 4096 bytes; shown in the dock and on the desktop).

## Known limits (honest list)
- `yield` runs eagerly: the generator function runs to its end when called and the yielded values are collected (at most 100000). Endless generators and lazy interleaving are not possible; errors inside a generator appear when it is created.
- No decorators (`@` starts a comment), `complex`, `memoryview`, `slice`, `compile`, `breakpoint`; `open()` has no `+`/binary modes and files are text of at most 4096 bytes.
- Set/dict lookups are linear (O(n)); memory is a 4 MiB arena without garbage collection; a run stops after 20 million steps.
- Names `fr`, `vr`, `end` and the other keywords cannot be used as variable names.
- Legacy-style `ast_do(var, "cmd")` / `file_read(var, name)` out-parameter forms are not supported in engine 2; use `r = ast_do("cmd")`, `r = file_read(name)`. `gui_wait(k, ms)`, `gui_mouse(x, y)`, `inputc(var)` keep the old form.
- `as2_check` (editor live check) finds syntax errors only, not unknown names.
