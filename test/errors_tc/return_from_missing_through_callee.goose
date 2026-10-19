// error: call to emit requires an enclosing call of guarded
// A call no enclosing call of guarded reaches is still an error (§7.9)
// when the function making it is called: only one that nothing calls is
// left unchecked (typing/unreached_return_from_callee.goose).
fn emit(n: i64) {
    if n < 0 { return false from guarded; }
}
fn guarded(n: i64) -> bool {
    emit(n);
    return true;
}
fn emit_twice(n: i64) {
    emit(n);
    emit(n + 1);
}
fn main() {
    print(guarded(1));
    emit_twice(-1);
}
