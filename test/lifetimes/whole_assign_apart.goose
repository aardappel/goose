// Whole assignments whose right-hand side leaves the array it replaces
// alone (§4.4): its length read first, other arrays, a slice parameter,
// the fields of the value holding it that are not rebuilt, and a rebind of
// a reference to it.
var G: u8[>..] = [];
var H: u8[>..] = [];
struct Text { font: i64, size: u8[2], chars: u8[>..] }
struct Bag { tag: i64, items: i64[>..<] }
fn letters(n: i64) -> u8[>..] {
    var out: u8[>..] = [];
    for i in 0..n { out.push(97 + i as u8); }
    return out;
}
fn collect(s: u8[:]) -> u8[>..] {
    var out: u8[>..] = [];
    out.append(s);
    return out;
}
fn from_h() -> u8[>..] { return copy(H); }
fn set_g(s: u8[:]) { G = collect(s); }
fn refill(b: Bag&) { b.items = [b.tag, b.tag + 1]; }
fn replace(x: i64[>..<]&, y: i64[>..<]&) { x = copy(y); }
fn fresh(tag: i64) -> Bag { return Bag { tag: tag, items: [tag, tag] }; }
fn main() {
    G.append("abcd");
    let n = G.len;
    G = letters(n + 1);
    print(G);
    H.append("xyz");
    G = from_h();
    print(G);
    set_g(H[1..]);
    print(G);
    var x: u8[>..] = [];
    var y: u8[>..] = [];
    y.append("hello");
    x = copy(y);
    print(x);
    fn from_y() -> u8[>..] { return collect(y[1..]); }
    x = from_y();
    print(x);
    var t = Text { font: 3, size: [1, 2], chars: [] };
    t.chars = letters(t.font);
    print(t.chars);
    t.chars = letters(t.size[1] as i64);
    print(t.chars);
    var b = Bag { tag: 5, items: [1] };
    refill(b);
    print(b.items);
    let tag = b.tag + 1;
    b = fresh(tag);
    print(b.tag, " ", b.items);
    var p: i64[>..<] = [1];
    var q: i64[>..<] = [2, 3];
    replace(p, q);
    print(p, " ", q);
    var ga: i64[>..<] = [1, 2];
    var gb: i64[>..<] = [3, 4];
    var r .= ga;
    fn retarget() -> i64[>..<] {
        r .= gb;
        return [7, 8, 9];
    }
    r = retarget();
    print(ga, " ", gb, " ", r);
}
