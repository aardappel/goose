# Call location: `src_file()` and `src_line()`

Status: implemented.

A log line wants the file and the line of the call that wrote it. The
parser already stores that on the call (`Line`, from `Parser::CurLine`).
Codegen emits it as a constant, so inlining the function that received
the value cannot move it. There is no macro and no stack walk.

## Defaults

A default is parsed in the function that declares it. `AddParamDefaults`
clones that expression into the call, and the clone keeps the
declaration's `Line`. A `src_file()` written as a default would then
name the function's own file.

The clone is checked in `InParamDefault`, whose frame records the call
that left the argument out (`Frame::callline`). When that check resolves
a call to the `src_file` or `src_line` builtin, `CheckBuiltin` sets the
call's `Line` from the innermost parameter-default frame
(`EachDefaultInPlace` follows where the call was written, not a body
checked from the default). A function of the same spelling is not the
builtin, so its line stays where it was parsed. `CheckCall` drops the
inserted defaults and clones them again on the next check, so this runs
on every insertion.

```goose
fn info(text: u8[:], file: u8[:] = src_file(), row: i64 = src_line()) {}
```

`info("filled")` on line 4 of `fill.goose` is
`info("filled", "fill.goose", 4)`. An argument the caller writes is
kept.

## The file string

`src_file()` is the base name, the last path segment. Runtime aborts
print the path `CodeGen::FileRef` stored, one static per source file.
A log line does not: that path is whatever the compiler loaded, a home
directory or a checkout, and it would be baked into the binary. The
base name is one static per distinct string (`StrRaw`), shared by every
call that names it. Two source files with the same base name are not
distinguished. The column stays in the compiler.
