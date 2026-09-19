// A by-value parameter whose references all point into one array holds
// exactly that array: a link between two views read back out of it through
// a call is a store within one root, and popping another array leaves what
// it holds valid (§9.2, §3.9, §5.1).
struct N { v: i64, next: N&<u32>? }
struct W2 { a: N[:], b: N[:] }
struct Cur { text: const u8[:] }
fn id(w: W2) -> W2 { w }
fn link(w: W2) {
    let a = id(w).a;
    let b = id(w).b;
    a[0].next .= b[1];
}
fn trim(xs: i64[>..]&, c: Cur) { xs.pop(); print(c.text, " ", xs.len); }
fn main() {
    var ns: N[>..] = [N { v: 1 }, N { v: 2 }];
    link(W2 { a: ns[..], b: ns[..] });
    let n = ns[0].next;
    if n { print(n.v); }
    var xs: i64[>..] = [1, 2, 3];
    var text: u8[>..] = [];
    text.push('h');
    text.push('i');
    trim(xs, Cur { text: text[..] });
}
