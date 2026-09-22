// Views passed to functions that shrink an array while still using them,
// where the caller passes a view of some other array (§5.1, §5.2).
var G: i64[>..<] = [1, 2, 3];
var B: u8[>..] = [];
struct H { s: u8[:] }
struct P { y: i64[>..<]& }

fn after(s: i64[:]) {
    G.clear();
    G.push(9);
    print(s);
}
// A view used for the last time before the shrink may be of the global.
fn before(s: i64[:]) {
    print(s);
    G.clear();
    G.push(8);
}
fn pass(t: i64[:]) { after(t[1..]); }
// The callee's own view of the global, and a shrink of the array passed in.
fn mirror(a: i64[>..<]&) {
    let s = G[..];
    a.clear();
    a.push(7);
    print(s);
}
fn consume(r: i64&, a: i64[>..<]&) {
    a.clear();
    a.push(6);
    print(r);
}
fn holder(h: H) {
    B.clear();
    B.append("zz");
    print(h.s);
}
// G's elements are no u8s, whatever a read-back argument may point into.
fn text_after(s: u8[:]) {
    G.clear();
    G.push(4);
    print(s);
}
// What pp.y refers to is some grow-shrink array, which a fixed one is not.
fn through(pp: P&, s: i64[:]) {
    pp.y.pop();
    pp.y.push(3);
    print(s);
}
fn each<F>(s: i64[:]) {
    F();
    print(s);
}
// A recursive call counts as shrinking what the cycle's text shrinks, which
// B is not.
recursive fn count(s: u8[:]) -> i64 {
    if s.len == 0 { return 0; }
    let rest = count(s[1..]);
    print(s);
    return rest + 1;
}
fn main() {
    var local: i64[>..<] = [4, 5];
    after(local[..]);
    before(G[..]);
    pass(local[..]);
    mirror(local);
    var other: i64[>..<] = [3];
    consume(other[0], local);
    var text: u8[>..] = [];
    text.append("ab");
    holder(H { text[..] });
    let held = H { text[..] };
    text_after(held.s);
    var target: i64[>..<] = [1, 2];
    let p = P { target };
    var fixed: i64[2] = [7, 8];
    through(p, fixed[..]);
    print(target);
    each(local[..]) { G.clear(); G.push(5); };
    fn nested(s: i64[:]) {
        local.clear();
        local.push(2);
        print(s);
    }
    nested(other[..]);
    B.append("xy");
    print(count(B[..]));
    print(G);
    print(local);
    print(B);
}
