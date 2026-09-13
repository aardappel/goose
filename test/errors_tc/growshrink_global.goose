// error: cannot call shrink, which shrinks gq while s
// error: is still used: it may refer into gq
// A call that shrinks a global grow-shrink array while a slice of it is in
// scope (§5.2).
var gq: i64[>..<] = [1, 2, 3];
fn shrink() { gq.pop(); }
fn main() {
    let s = gq[0..2];
    shrink();
    print(s[0]);
}
