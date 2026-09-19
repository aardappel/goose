// An array literal given to append is the run it adds (§4.2): its elements
// are the receiver's, so they adapt to its element type as a declared
// array's do, `[]` included, and the literal is a T[k] of them, or a T[]
// where they are not fixed-size. Such elements, and ones holding relative
// references, are built where they stay: at a grow-only or grow-shrink
// array's top, or in a limited one's free slots, so the references in them
// may point into the receiver. Other elements are evaluated first and
// copied, so evaluating one may still grow the receiver.
struct Node { v: i64, next: Node&<u16>? }
struct PN { key: i64, link: PN&<u32 in P> }

var P: PN[>..] = [];
let N = 2;

fn noisy(b: u8[>..]&) -> u8 {
    b.push(32);
    return 46;
}
fn mk(n: i64) -> u8[>..] {
    var out: u8[>..] = [];
    for i in n { out.push(('a' + i) as u8); }
    return out;
}
fn names(ws: u8[][>..]&) { ws.append([str("by"), "ref"]); }
fn twice<T>(xs: T[>..]&, x: T) { xs.append([x, x]); }
fn link(n: Node&) -> i64 {
    let nx = n.next;
    guard nx else { return 0; }
    return nx.v;
}
fn chain(ns: Node[:]) {
    var out: u8[>..] = [];
    for &n in ns { format(out, n.v, ">", link(n), ";"); }
    print(out);
}

fn main() {
    var b: u8[>..] = [];
    b.append([72, 105]);
    b.append([]);
    b.append([33; N]);
    b.append([noisy(b), 63]);          // The space noisy pushes lands first.
    b.append("!");
    print(b);                          // Hi!! .?!

    var ws: u8[][>..] = [];
    ws.append([str("ab"), str("c", 99)]);
    ws.append(["lit", mk(3), [104, 105]]);
    ws.append([]);
    ws.append([str("x"); 2]);
    names(ws);
    var out: u8[>..] = [];
    for w in ws { format(out, w, "|"); }
    print(ws.len, " ", out);           // 9 ab|c99|lit|abc|hi|x|x|by|ref|

    var lim: i16[..4] = [];
    lim.append([-1, 2]);
    lim.append([3]);
    var gs: u16[>..<] = [];
    gs.append([1, 65535]);
    var rows: i16[2][>..] = [];
    rows.append([[1, -2], [3, 4]]);
    var wide: i16[>..] = [];
    twice(wide, 7);
    print(lim, " ", gs, " ", rows, " ", wide);

    var x = 10;
    let y = 20;
    var refs: (i64&)[>..] = [];
    refs.append([x, y]);
    x = 11;
    var sum = 0;
    for r in refs { sum += r; }
    print(sum);                        // 31

    var ns: Node[>..] = [];
    ns.push(Node { v: 1, next: null });
    ns.append([Node { v: 2, next: ns[0] }, Node { v: 3, next: ns[0] }]);
    ns.append([Node { v: 4, next: ns[2] }; 2]);
    ns.append([]);
    chain(ns);                         // 1>0;2>1;3>1;4>3;4>3;

    var ln: Node[..4] = [];
    ln.push(Node { v: 5, next: null });
    ln.append([Node { v: 6, next: ln[0] }; 2]);
    ln.append([Node { v: 7, next: ln[2] }]);
    chain(ln);                         // 5>0;6>5;6>5;7>6;

    P.push(PN { key: 1, link: self });
    P.append([PN { key: 2, link: self }, PN { key: 3, link: P[0] }]);
    var slots: (PN&<u32 in P>)[>..] = [];
    slots.append([P[2], P[1], P[0]]);
    out = [];
    for s in slots { format(out, s.key, ">", s.link.key, ";"); }
    print(out);                        // 3>1;2>2;1>1;

    var pending = [];
    pending.push(72 as u8);
    pending.append([105, 33]);
    var nums = [];
    nums.append([4, 5]);
    nums.append([]);
    print(pending, " ", nums);
}
