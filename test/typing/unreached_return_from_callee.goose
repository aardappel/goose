// A function nothing calls, whose calls reach a `return ... from` it does
// not enclose, is left to the calls that would reach it: checked on its own
// it could only be rejected, though a call of it under guarded is valid
// (§7.9). Nested and transitive callees count, and so does a function value.
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

fn emit_through(n: i64) {
    fn inner(m: i64) { emit_twice(m); }
    inner(n);
}

fn apply<F>(n: i64) { F(n); }

fn emit_value(n: i64) {
    apply(n, emit);
}

fn main() {
    print(guarded(1), " ", guarded(-1));
}
