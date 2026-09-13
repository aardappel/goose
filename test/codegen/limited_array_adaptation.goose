// Arrays and slices of every kind constructing a static-capacity limited
// array (§4.2) from sources that are not plain variables -- call results,
// str(), slice expressions, copies, reference-returning calls -- in every
// position: a local, an argument, a field of a fixed and of a variable-size
// struct, an assignment, a return, a branch.
fn mk() -> u8[>..] { var r: u8[>..] = []; r.push('h'); r.push('i'); return r; }
fn mk2() -> u8[] { return "yo"; }
fn pick(a: u8[>..]&) -> u8[>..]& { return a; }
fn take(s: u8[..16]) -> i64 { return s.len; }
fn wrap() -> u8[..16] { return mk(); }
fn wrap2() -> u8[..16] { return str("w", 2); }
struct Named { name: u8[..16] }
struct Rec { tag: u8[], s: u8[..8], after: i64 }
fn main() {
    let a: u8[..16] = mk();
    print(a.len, " ", a);
    let b: u8[..16] = mk2();
    print(b.len, " ", b);
    let c: u8[..16] = str("ab", 1);
    print(c.len, " ", c);
    print(take(mk()), " ", take(str("xyz")), " ", take(mk2()));
    let n = Named { name: str("x", 42) };
    print(n.name.len, " ", n.name);
    var g: u8[>..] = [];
    g.push('a'); g.push('b'); g.push('c');
    let d: u8[..16] = g[..];
    print(d.len, " ", d);
    let e: u8[..16] = g[1..3];
    print(e);
    print(take(g[..]), " ", take(g), " ", take(g[..2]));
    let r = Rec { tag: "t", s: mk(), after: 77 };
    print(r.s.len, " ", r.s, " ", r.after);
    let r2 = Rec { tag: "t", s: g[1..], after: 78 };
    print(r2.s, " ", r2.after);
    let r3 = Rec { tag: "t", s: mk2(), after: 79 };
    print(r3.s, " ", r3.after);
    var s: u8[..16] = [];
    s = mk();
    print(s.len, " ", s);
    s = g[..];
    print(s);
    let f: u8[3] = ['p', 'q', 'r'];
    let h: u8[..16] = f;
    print(h.len, " ", h);
    let k: u8[..4] = "kl";
    let m: u8[..16] = k;
    print(m.len, " ", m);
    let cp: u8[..16] = copy(g);
    print(cp);
    let p: u8[..16] = pick(g);
    print(p);
    print(wrap(), " ", wrap2());
    let q: u8[..16] = if g.len > 2 { mk() } else { mk2() };
    print(q);
}
