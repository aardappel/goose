// error: expected a value of type u8, got i64 (narrowing and sign changes require an explicit `as`)
// §3.1: a local `let` is a value of its type, not a named constant: x is
// ~K computed at run time, after bump made K 1004, and so is no `~4` a u8
// could hold.

fn bump(r: i64&) { r += 1000; }
let K = 4;

fn main() {
    bump(K);
    let x = ~K;
    let y: u8 = x;
    print(y, " ", x);
}
