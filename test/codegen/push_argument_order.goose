// A push evaluates its receiver, then its argument, then adds the element
// (§2): an argument that itself grows the array pushes first, the element
// follows what it pushed, and the returned reference names the element. A
// literal holding a relative reference is built in its slot, whichever array
// kind the slot belongs to.
struct Node { v: i64, next: Node&<u32>? }
fn grow(a: i64[>..]&) -> i64 { a.push(10); return 20; }
fn grow_lim(a: i64[..8]&) -> i64 { a.push(10); return 20; }
fn grow_gs(a: i64[>..<]&) -> i64 { a.push(10); return 20; }
fn main() {
    var l: i64[..8] = [];
    l.push(l.push(1));
    print(l);
    let lr .= l.push(grow_lim(l));
    print(l, " ", lr);
    var g: i64[>..] = [];
    let gr .= g.push(grow(g));
    print(g, " ", gr);
    g.push(g.push(g.len));
    print(g);
    var s: i64[>..<] = [];
    let sr .= s.push(grow_gs(s));
    print(s, " ", sr);
    var a: Node[..4] = [];
    a.push(Node { v: 1, next: null });
    a.push(Node { v: 2, next: a[0] });
    let n .= a[1];
    let nx = n.next;
    if nx { print(nx.v); }
    var b: Node[>..] = [];
    b.push(Node { v: 3, next: null });
    b.push(Node { v: 4, next: b[0] });
    let m .= b[1];
    let mx = m.next;
    if mx { print(mx.v); }
}
