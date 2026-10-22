// error: expected a value of type u8, got i64 (narrowing and sign changes require an explicit `as`)
// §3.1: only a global `let` with no written type is a named constant; a
// `var` initialized to `~0` is an i64 holding whatever was assigned last,
// here 1000, which no u8 holds.

fn main() {
    var x = ~0;
    x = 1000;
    let y: u8 = x;
    print(y);
}
