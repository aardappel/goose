// A value constructed into a slot is built as the slot's type (§4.2, §7.3).
// str() has no layout of its own for a u8[] slot, so it writes its text
// straight into the element or field behind the slot's length prefix
// (§3.7); a call's result takes the slot's length storage; a reference
// stores a relative slot's offset (§3.9). Every kind of slot is here: the
// elements of array literals (of variable and grow-only arrays, filled,
// nested, a field's, returned, pushed, assigned, chosen by an if or a
// match, of other length storages), an if's or match's value in a local,
// a field or a pushed element, globals, a dispatched call's argument, the
// temporary an inlined result is printed from, an appended run of a
// function that returns str(), and a function value's result that nothing
// receives. The values are str(), to_bytes(), functions returning u8[>..]
// and u8[], and copies of a returned reference.
struct N { v: i64 }
var pool: N[>..] = [];

struct Txt { body: u8[] }
struct Row { names: u8[][], k: i64 }
enum E { A { s: u8[] }, B }
enum Shape { Circle { r: i64 }, Sq { s: i64 } }

fn mk(n: i64) -> u8[>..] {
    var out: u8[>..] = [];
    for i in n { out.push(('a' + i % 26) as u8); }
    return out;
}
fn cut(n: i64) -> u8[] { return "yo!"[0..n]; }
fn text(n: i64) -> u8[] { return str("r", n); }
fn hello(n: i64) -> u8[] { return str("hi", n); }
fn wide(n: i64) -> u8[varint] { return cut(n); }
fn same(s: u8[]&) -> u8[]& { return s; }
fn lit() -> u8[][] { return [str("p"), mk(2)]; }
fn filled() -> u8[][] { return [str("q", 1); 2]; }
fn describe(c: Shape.Circle, label: u8[varint]) -> i64 { print(label, " circle ", c.r); label.len }
fn describe(q: Shape.Sq, label: u8[varint]) -> i64 { print(label, " sq ", q.s); label.len }
fn tell(c: Shape.Circle, label: u8[]) -> i64 { print(label, " circle ", c.r); label.len }
fn tell(q: Shape.Sq, label: u8[]) -> i64 { print(label, " sq ", q.s); label.len }
fn run<F>(n: i64) { F(n); print("ran ", n); }
fn line(xs) {
    var out: u8[>..] = [];
    for x in xs { format(out, x, "|"); }
    print(out);
}

let G: u8[] = str("g", 1);
let GV: u8[varint] = cut(3);
let GS: u8[][] = [str("a"), mk(2)];
var GR: u8[>..] = hello(3);

fn main() {
    let xs: u8[][] = [str("a"), str("bc")];
    var r = 0;
    for x in xs { r += x.len; }
    print(r);
    var ys: u8[][>..] = [str("d", 1), mk(3)];
    ys.push(str("e"));
    line(ys);
    let fill: u8[][] = [str("f", 2); 3];
    line(fill);
    let nest: u8[][][] = [[str("n")], [mk(2), str("m", 3)]];
    for row in nest { line(row); }
    let row = Row { names: [str("x", 1), mk(1)], k: 5 };
    line(row.names);
    print(row.k);
    line(lit());
    line(filled());
    let ts: Txt[] = [Txt { body: str("t", 1) }, Txt { body: mk(2) }];
    for t in ts { print(t.body); }
    let es: E..[] = [E.A { s: str("v", 2) }, E.B, E.A { s: mk(1) }];
    for e in es { match e { A a => print(a.s), B => print("B") } }
    var rows: u8[][][>..] = [];
    rows.push([str("p", 1), mk(2)]);
    for rr in rows { line(rr); }
    var re: u8[][>..] = [];
    re = [str("re", 1), mk(4)];
    line(re);
    let pick: u8[][] = [if r > 2 { mk(2) } else { str("s") }, match r { 3 => str("t"), _ => mk(1) }];
    line(pick);
    let v: i32[] = [1, 2, 3];
    let images: u8[][] = [to_bytes(v), str("z")];
    for im in images { print(im.len); }
    // Past 127 elements a varint prefix needs a second byte.
    let vs: u8[varint][] = [str(mk(150)), cut(2), mk(130), str("w")];
    for w in vs { print(w.len, " ", w[w.len - 1]); }
    let us: u8[u8][] = [str("u", 5), cut(3)];
    line(us);

    let chosen: u8[] = if r > 2 { mk(2) } else { str("s") };
    print(chosen);
    let named = Txt { body: match r { 3 => mk(3), _ => str("m") } };
    print(named.body);
    ys.push(if r > 2 { mk(4) } else { mk(1) });
    line(ys);
    let kept: u8[] = "kept";
    let copied: u8[] = if r > 2 { copy(same(kept)) } else { copy(kept) };
    print(copied);

    print(G, " ", GV, " ", GV.len);
    line(GS);
    GR.push('!');
    print(GR);

    let a: Shape = Shape.Circle { r: 2 };
    let b: Shape = Shape.Sq { s: 5 };
    print(tell(a, str("first", 1)));
    print(tell(b, mk(4)));
    print(describe(a, cut(2)));
    print(describe(b, str("second", 22)));

    print(text(12));
    print(wide(3));
    var out: u8[>..] = [];
    out.append(text(4));
    out.append(str("s", 3));
    print(out);

    run(3) { n => str("x", n) };
    run(2) { n => mk(n) };
    run(4) { n => text(n) };

    pool.push(N { v: 10 });
    pool.push(N { v: 20 });
    let links: (N&<u32 in pool>)[] = [pool[1], pool[0]];
    for l in links { print(l.v); }
    let thrice: (N&<u32 in pool>)[] = [pool[1]; 3];
    for l in thrice { print(l.v); }
    let maybe: (N&<u16 in pool>?)[] = [null, pool[0]];
    for m in maybe { if m { print(m.v); } else { print("null"); } }
    let none: (N&<u16 in pool>?)[] = [null; 2];
    for m in none { if m { print(m.v); } else { print("null"); } }
}
