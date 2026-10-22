// error: the use as a literal relies on K keeping its initializer's value
// error: extern fn take: parameter s of type u8[] cannot cross to C
// error: expected a value of type bool, got i64
// A specialization whose check ends on an error before its body's begins
// is not reused as if it had been checked (docs/design/language_server.md
// §5 D2): an extern's, whose declaration's error stands for every call of
// it, ends the root of a later call silently, uses_take's error unreported,
// while one made for a call whose literal argument relies on what it may
// not is no specialization at all, which uses_id's call makes anew and
// goes on past.
extern fn take(s: u8[]) -> i64;
fn bump(r: i64&) { r += 1000; }
let K = 3;
fn id<T>(x: T) -> T { x }
fn uses_take() -> i64 { let n: i64 = take([3]); let b: bool = n; n }
fn uses_id() -> i64 { let n: i64 = id(5); let b: bool = n; n }
fn main() {
    bump(K);
    print(id(K));
}
