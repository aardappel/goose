// error: variable n may be used before it is assigned
// Only the break assigns n: the while also exits where its condition is
// false (§4.4, §6.4).
fn run(k: i64) {
    var n: i64;
    var i = 0;
    while i < k {
        if i == 2 { n = i; break; }
        i++;
    }
    print(n);
}
fn main() { run(3); run(1); }
