// runtime-debug: a resizable ADT binds its payloads by value only, a whole
// assignment being able to replace its variant (§4.4, §8.1), so a payload
// that is not itself resizable links by plain or `in pool` references, which
// a copy keeps (§3.5, §3.9): a binder's copy still reads them after its arm
// has replaced the variant, and they follow what they point at across whole
// assignments. Self-relative links in a resizable payload, which the C
// backend does not bind at all yet, are left alone.

var cells: i64[>..] = [];

struct H { x: i64, r: R.. }
enum R { A { v: i64, p: i64&, q: i64&<u32 in cells> }, B { items: i64[>..] } }

fn show(h: H&) {
    match h.r {
        A a => print("A ", a.v, " ", a.p, " ", a.q),
        B => print("B"),
    }
}

struct N { v: i64, next: N&<u16>? }
enum Tree { Empty, Nodes { ns: N[>..] } }

fn main() {
    cells.push(10);
    cells.push(20);
    var y = 7;
    var h = H { x: 5, r: R.A { v: 1, p: y, q: cells[0] } };
    show(h);
    match h.r {
        A a => {
            h.r = R.B { items: [1, 2, 3] };
            print("copy ", a.v, " ", a.p, " ", a.q);
        },
        B => {},
    }
    show(h);
    h.r = R.A { v: 2, p: h.x, q: cells[1] };
    h.x = 6;
    cells[1] = 21;
    show(h);
    print(h);

    var t: Tree.. = Tree.Nodes { ns: [N { v: 1, next: null }, N { v: 2, next: null }] };
    match t { Nodes => print("nodes"), Empty => print("empty") }
    print(t);
    t = Tree.Empty;
    print(t);
}
