// A variable whose type is inferred binds a non-fixed lvalue by reference
// (§4.1), and a reference to a non-fixed value denotes one (§3.8): `let x =
// r;` is then r itself, as `let x .= r;` is, wherever the reference comes
// from -- a parameter, a reference field reached through one, a call
// returning a reference, each result of a multi-value call, a narrowed
// optional, an untyped parameter -- and `var x = r;` is a reference that can
// be rebound. Pushes and writes through the binding reach the original, and
// a function may return it. A reference to a fixed-size value, a varint's
// included, still decays to a copy of its pointee.
struct Ctx { args: i64[>..<]&, other: i64[>..<]&, opt: i64[>..<]? }
struct Named { name: u8[], n: i64 }            // variable-size
struct Frame { depth: i64, marks: i64[>..] }   // a frame object
struct P { x: i64, y: i64 }

fn viaparam(r: i64[>..<]&) { let a = r; a.push(1); }
fn viafield(c: Ctx&) { let a = c.args; a.push(2); }
fn viavar(c: Ctx&) {
    var a = c.args;
    a.push(3);
    a .= c.other;
    a.push(4);
}
fn pick(c: Ctx&) -> i64[>..<]& { return c.args; }
fn both(c: Ctx&) -> i64[>..<]&, i64[>..<]& { return c.args, c.other; }
fn viacalls(c: Ctx&) {
    let a = pick(c);
    a.push(5);
    let x, y = both(c);
    x.push(6);
    y.push(7);
}
fn viaoptional(c: Ctx&) {
    let o = c.opt;
    if o {
        let a = o;
        a.push(8);
    }
}
fn untyped(r) { let a = r; a.push(9); }
fn returned(c: Ctx&) -> i64[>..<]& {
    let a = c.args;
    return a;
}
fn rename(p: Named&) {
    let a = p;
    a.n = a.n + 1;
    a.name[0] = 'N';
}
fn deepen(f: Frame&) {
    var a = f;
    a.marks.push(a.depth);
    a.depth = a.depth + 1;
}

fn firstof(xs: varint[:]) -> varint& {
    for x in xs { return x; }
    abort("empty");
}
fn copies(n: i64&, p: P&, vs: varint[:]) -> i64 {
    var a = n;
    a = 100;
    var q = p;
    q.x = 200;
    var v = firstof(vs);
    v = v + 1;
    return a + q.x + v;
}

fn main() {
    var s: i64[>..<] = [];
    var t: i64[>..<] = [];
    var c = Ctx { args: s, other: t, opt: s };
    viaparam(s);
    viafield(c);
    viavar(c);
    viacalls(c);
    viaoptional(c);
    untyped(s);
    returned(c).push(10);
    print(s, " ", t);                        // [1, 2, 3, 5, 6, 8, 9, 10] [4, 7]
    var nm = Named { name: "name", n: 1 };
    rename(nm);
    var fr = Frame { depth: 3, marks: [] };
    deepen(fr);
    deepen(fr);
    print(nm.name, " ", nm.n, " ", fr.depth, " ", fr.marks);   // Name 2 5 [3, 4]
    var n = 1;
    var p = P { x: 2, y: 3 };
    var vs: varint[>..] = [300];
    print(copies(n, p, vs[..]), " ", n, " ", p.x, " ", vs);    // 601 1 2 [300]
}
