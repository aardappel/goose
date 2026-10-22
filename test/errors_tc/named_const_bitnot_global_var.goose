// error: expected a value of type u8, got i64 (narrowing and sign changes require an explicit `as`)
// §3.1: a global `var` initialized to `~0` is no named constant but an i64,
// which set makes 1000.

var G = ~0;
fn set() { G = 1000; }

fn main() {
    set();
    let y: u8 = G;
    print(y);
}
