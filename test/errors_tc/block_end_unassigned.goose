// error: variable n may be used before it is assigned
// Only the break assigns n: the block also exits where its body ends
// (§4.4, §6.4).
fn run(c: bool) {
    var n: i64;
    block {
        if c { n = 5; break; }
    }
    print(n);
}
fn main() { run(true); run(false); }
