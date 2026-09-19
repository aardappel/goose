// copy(x) is a fresh value of the slot it lands in (§4.1, §4.2), and an
// appended copy(x) adds x's elements, as appending x does. A returned
// reference's pointee is built from where it lies in its slot's layout: a
// u8[] as a u8[varint], u8[u8], u8[u16] or u8[..16] element, a varint one as
// a u8[u8] or u8[u16] one, a resizable or fixed one as a u8[] one, u16
// elements behind a varint prefix, and runs past 127 elements, where a
// varint prefix takes a second byte. -O0 builds each through the call, -O2
// through the inlined body, which leaves a reference in the slot; nth and
// pickcopy stay calls at -O2 as well, and pickcopy's copies also land in the
// element runs its appended results are. The appends copy locals, a field,
// an element and returned references, of variable, fixed, limited and
// resizable arrays, into grow-only, grow-shrink, limited, u16 and u8[]-element
// receivers, and append a returned reference without a copy. A pushed
// element, a field, a by-value argument, a return and a branch's value take
// such copies in their own layouts too.
struct T { body: u8[varint], k: i64 }
struct Holder { name: u8[], row: u8[][] }

fn mk(n: i64) -> u8[>..] {
    var out: u8[>..] = [];
    for i in n { out.push(('a' + i % 26) as u8); }
    return out;
}

let GC: u8[] = "circle";
let GL: u8[] = mk(150);
let GV: u8[varint] = mk(140);
let GW: u16[] = [300, 400, 500];
let G3: u8[3] = ['x', 'y', 'z'];
let GF: u8[3][] = [['d', 'e', 'f'], ['g', 'h', 'i']];
let GR: u8[][] = ["ab", "cde", mk(200), "fg"];
var GB: u8[>..] = [];

fn one() -> u8[]& { return GC; }
fn long() -> u8[]& { return GL; }
fn varl() -> u8[varint]& { return GV; }
fn wide() -> u16[]& { return GW; }
fn three() -> u8[3]& { return G3; }
fn rows() -> u8[][]& { return GR; }
fn grown() -> u8[>..]& { return GB; }
// -O2 keeps this and pickcopy calls: too big for this many callers.
fn nth(i: i64) -> u8[]& {
    if i < 0 || i >= GR.len { abort("no such row"); }
    var j = 0;
    for x in GR {
        if j == i { return x; }
        j += 1;
    }
    abort("no such row");
}
fn pickcopy(k: i64) -> u8[] {
    if k == 0 { return copy(one()); }
    if k == 1 { return copy(long()); }
    if k == 2 { return copy(grown()); }
    return copy(three());
}
fn takes(v: u8[varint]) -> i64 { return v.len; }
fn ret() -> u8[u16] { return copy(long()); }

fn line(xs) {
    var out: u8[>..] = [];
    for x in xs { format(out, x, "|"); }
    print(out);
}
fn ends(w) { print(w.len, " ", w[0], " ", w[w.len - 1]); }

fn main() {
    GB.append("grow");
    let s: u8[] = "hey";
    var r: u8[>..] = [];
    r.append("rz");
    var lim: u8[..] = [..8];
    lim.push('L');
    lim.push('M');
    let f: u8[3] = ['a', 'b', 'c'];
    let h = Holder { name: "nm", row: ["p", "qr"] };

    var out: u8[>..] = ['<'];
    out.append(copy(s));
    out.append(copy(r));
    out.append(lim.copy());
    out.append(copy(f));
    out.append(copy(h.name));
    out.append(copy(GF[1]));
    out.append(copy(one()));
    out.append(copy(grown()));
    out.append(one());
    out.push('|');
    out.append(pickcopy(0));
    out.append(pickcopy(2));
    out.append(pickcopy(3));
    out.push('>');
    print(out);
    var gs: u8[>..<] = [];
    gs.append(copy(one()));
    gs.append(copy(r));
    print(gs);
    var lo: u8[..] = [..32];
    lo.append(copy(one()));
    lo.append(copy(r));
    print(lo);
    var ws: u16[>..] = [7];
    ws.append(copy(wide()));
    line(ws);
    var nest: u8[][>..] = [];
    nest.append(copy(rows()));
    nest.append(copy(h.row));
    for x in nest { ends(x); }
    var big: u8[>..] = [];
    big.append(copy(long()));
    big.append(pickcopy(1));
    ends(big);

    let vs: u8[varint][] = [copy(one()), copy(s), copy(long()), copy(grown()), copy(three()),
                            copy(nth(0)), copy(nth(2)), pickcopy(1)];
    for v in vs { ends(v); }
    let us: u8[u8][] = [copy(varl()), copy(long()), copy(one()), copy(nth(2))];
    for u in us { ends(u); }
    let hs: u8[u16][] = [copy(varl()), copy(grown()), copy(nth(3)), copy(nth(1))];
    for u in hs { ends(u); }
    let wv: u16[varint][] = [copy(wide())];
    for w in wv { line(w); }
    let plain: u8[][] = [copy(three()), copy(grown()), copy(varl())];
    for p in plain { ends(p); }
    let caps: u8[..16][] = [copy(one()), copy(nth(0)), copy(three())];
    for c in caps { print(c.len, " ", c); }

    var pushed: u8[varint][>..] = [];
    pushed.push(copy(one()));
    pushed.push(copy(long()));
    for p in pushed { ends(p); }
    let t = T { body: copy(one()), k: 7 };
    print(t.body, " ", t.k);
    print(takes(copy(long())));
    ends(ret());
    var k = 0;
    for i in 2 { k += i; }
    let y: u8[varint] = if k > 0 { copy(one()) } else { copy(long()) };
    print(y);
}
