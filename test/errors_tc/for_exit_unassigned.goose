// error: variable n may be used before it is assigned
// Only the break assigns n: the for also exits once its iterations run out
// (§4.4, §6.5).
fn run(k: i64) {
    var n: i64;
    for i in k {
        if i == 2 { n = i; break; }
    }
    print(n);
}
fn main() { run(3); run(1); }
