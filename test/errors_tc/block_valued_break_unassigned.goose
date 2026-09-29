// error: variable n may be used before it is assigned
// A break with a value leaves the block as a bare one does: the second
// break leaves it with n unassigned, however the others leave it (§4.4,
// §6.4).
fn run(c: i64) -> i64 {
    var n: i64;
    let v = block {
        if c == 0 { n = 1; break 1; }
        if c == 1 { break 2; }
        if c == 2 { n = 3; break 3; }
        n = 4;
        4
    };
    return v + n;
}
fn main() { print(run(0)); print(run(1)); print(run(2)); print(run(3)); }
