# Call location: `file()` and `line()`

Status: implemented. `file()` and `line()` read the source location the
parser already stores on the call. A default that calls either one is
stamped with the call that left the argument out.

## 1. What is missing

A log line wants the file and the line of the call that wrote it. Goose has
no macro, and a function receives only the arguments the caller wrote. The
body of `info` is in the logger's file, so a location read there names that
file. Rust's `info!` pastes `file!()` and `line!()` at the call. Go reads
the caller off the stack. Neither is available here.

The compiler already has the fact. Every node stores a `Line`: the file,
the 1-based line, and the 1-based byte column (`Parser::CurLine`). A call
is anchored on its callee. Diagnostics name that anchor, and a debug check
pastes it into the abort. The value is not a Goose value.

## 2. The builtins

`file() -> const u8[:]` is the base name of the file that contains the
call, a view of static data, the same kind of value as a string literal.
The base name is the last path segment. The compiler's `Line` holds the
path it loaded, and a log line wants `fill.goose`, not that path.

`line() -> i64` is the 1-based line of that call. The column stays in the
compiler.

Both are zero-argument builtins. Codegen emits a constant from the call
node's `Line`. A later pass sees an ordinary value, so inlining the
function that received it cannot move the location.

A call written in the program names its own line:

```goose
print(file(), ":", line());
```

A call inside the body of `info` names the line in the logger, which is the
wrong line for a log. The location has to be taken from the call the user
wrote.

## 3. Defaults

A trailing default is parsed in the function that declares it.
`AddParamDefaults` clones that expression into the call. The clone keeps
the declaration's `Line`, so a `file()` written as a default would report
the function's own file.

After the clone, `StampLocBuiltins` walks it and sets the `Line` of each
`file()` and `line()` call, written `file`, `line`, `::file`, or `::line`,
to the `Line` of the call that left the argument out. `CheckCall` drops
the inserted defaults and `AddParamDefaults` clones them again on the next
check, so the stamp runs on every insertion.

```goose
fn info(text: u8[:], file: u8[:] = file(), row: i64 = line()) {
    // file and row are the caller's.
}
```

`info("filled")` is `info("filled", "fill.goose", 4)` when that call is
written on line 4 of `fill.goose`. An argument the caller writes is kept:
`info("filled", "other.goose", 3)` does not consult the builtins.

The defaults are then ordinary arguments. The body of `info` does not know
that they were filled in.

A local named `file` or `line` hides the builtin, as a local hides any
function. A global of that name does not: a call skips a global variable.

## 4. What this does not do

There is no macro and no stack walk. A release build has nothing to walk
once the call has been inlined, and the compile-time stamp does not depend
on that.

`file()` does not return the directory. `line()` does not return the
column. A call through a function value has no default (`§7.6`), so a
location default is not available there. The same is true of a `thread_fn`
and of tag dispatch.
