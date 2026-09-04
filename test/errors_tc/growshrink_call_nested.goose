// The shrink is two calls down; the summary carries it up (§5.2).
fn shrink(q: i64[>..<]&) { q.pop(); }
fn trim(q: i64[>..<]&) { shrink(q); }
fn main() {
    var gs: i64[>..<] = [1, 2, 3];
    let s = gs[0..2];
    trim(gs);
    print(s[0]);
}
