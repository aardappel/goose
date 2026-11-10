// error: cannot assign to let x
// A block of a call among another call's arguments assigned x, once however
// often that argument is checked, and the statement after assigns it again
// (§4.4).
fn id<T>(v: T) -> T { v }
fn once<F>() -> i64 { F(); return 1; }
fn main() {
    let x: i64;
    print(id(once() { x = 1; }));
    x = 2;
    print(x);
}
