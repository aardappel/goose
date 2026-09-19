// A callee's shrinks, growths and stores through the references a by-value
// holder parameter carries are the caller's, of the arrays those references
// point into (§5.1, §1.3(4), §9.2), and of nothing else: a view of ys stays
// in use across a call trimming xs, an element of notes is built around a
// call growing xs, and xs pops while the Box a call stored a view of ys into
// is still in use.
struct H { r: i64[>..]& }
struct Box { p: i64[:] }
struct HB { b: Box& }
fn id(h: H) -> H { h }
fn trim(v: H) {
    let r .= id(v).r;
    while r.len > 0 && r[r.len - 1] == 0 { r.pop(); }
}
fn fill(v: H, n: i64) -> u8[] {
    let r .= id(v).r;
    for i in n { r.push(i); }
    "filled"
}
fn put(v: HB, s: i64[:]) { let bb .= v.b; bb.p = s; }
fn main() {
    var xs: i64[>..] = [1, 2, 0, 0];
    var ys: i64[>..] = [7, 8, 9];
    let s = ys[..];
    trim(H { r: xs });
    print(xs, " ", s);
    var notes: u8[][>..] = [];
    notes.push(fill(H { r: xs }, 3));
    for n in notes { print(n, " ", xs); }
    var box = Box { p: ys[0..1] };
    put(HB { b: box }, ys[1..]);
    xs.pop();
    print(box.p, " ", xs);
}
