// error: cannot assign to let x, which may be assigned already
// A while loop's next iteration finds x assigned by the one before (§4.4,
// §6.5).
fn run(n: i64) {
    let x: i64;
    var i = 0;
    while i < n {
        x = i;
        i += 1;
    }
}
fn main() { run(3); }
