// error: cannot clear bs: global GS may hold a reference into it
// error: cannot clear cs: global GT may hold a reference into it
// error: cannot call mk, which grows y: the run appended to x is still under construction
// error: cannot call mk, which grows q: the run appended to p is still under construction
// error: argument 1 of h: constant 300 does not fit u8
// error: argument 1 of k: constant 200 does not fit i8
// error: argument 1 of k: constant 201 does not fit i8
// The whole-program fixups run once every root is checked, and go on past
// an error as the checker does past a root's (docs/design/language_server.md
// §5 D2): each shrink of a global array, each growth conflict the call sites
// settle, and each literal argument is an item of its own, whose error ends
// it alone. An item checked more than once -- k(201) in each specialization
// of twice -- reports its error once.
struct B { name: u8[..8] }
var bs: B[>..] = [];
var cs: B[>..] = [];
var GS: const u8[:][>..] = [];
var GT: const u8[:][>..] = [];
fn reload_b() { bs.clear(); }
fn reload_c() { cs.clear(); }
fn keep() {
    GS.push(bs[0].name[..]);
    GT.push(cs[0].name[..]);
}

struct Holder { r: i64[>..]& }
fn mk(y: i64[>..]&) -> i64[>..] { y.push(9); return [1, 2]; }
fn f(x: i64[>..]&, y: i64[>..]&) { x.append(mk(y)); }
fn g(p: i64[>..]&, q: i64[>..]&) { p.append(mk(q)); }

fn h(x) { let y: u8 = x; print(y); }
fn k(x) { let y: i8 = x; print(y); }
fn twice<T>(t: T) { k(201); }

fn main() {
    bs.push(B { name: "a" });
    cs.push(B { name: "c" });
    reload_b();
    reload_c();
    bs.push(B { name: "a" });
    cs.push(B { name: "c" });
    keep();
    var a: i64[>..] = [1];
    var b: i64[>..] = [];
    let hs = [Holder { r: a }, Holder { r: b }];
    let hd = hs[0];
    let r .= hd.r;
    f(b, r);
    g(a, r);
    h(300);
    k(200);
    twice(1);
    twice(true);
}
