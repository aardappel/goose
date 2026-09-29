// error: variable n may be used before it is assigned
// The break leaves the block before n is assigned: code after a block runs
// in what holds at every exit of it, the end of its body and each break
// (§4.4, §6.4).
fn run(c: bool) {
    var n: i64;
    block {
        if c { break; }
        n = 5;
    }
    print(n);
}
fn main() { run(false); run(true); }
