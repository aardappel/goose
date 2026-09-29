// error: variable n may be used before it is assigned
// A bare guard in a block is a break out of it, taken here before n is
// assigned (§4.4, §6.4).
fn run(c: bool) {
    var n: i64;
    block {
        guard !c;
        n = 5;
    }
    print(n);
}
fn main() { run(false); run(true); }
