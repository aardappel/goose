// Zero-length arrays are values of their own (§3.4): built from [] and from
// fills, copied, passed, returned, chosen by branches, compared, sliced and
// printed. C gives each one element nothing writes, so the temporary a
// literal is built in starts zeroed rather than copied out uninitialized;
// no output can show that slot, only MSVC's C4700 or clang's
// -Wuninitialized on the generated C.
var calls = 0;
fn tick() -> i64 { calls++; return calls; }

var g: i64[0] = [];
var h: u8[..0] = [];

fn none() -> i64[0] { [] }
fn same(a: i64[0]) -> i64[0] { a }
fn pick(c: bool) -> f64[0] { if c { [] } else { [tick(); 0] } }
fn lens(a: i64[0]&, b: i64[..0]) -> i64 { a.len + b.len + b.cap }
fn view(s: i64[:]) -> i64 { s.len }

fn main() {
    let e: i64[0] = [];
    let f: i64[0] = [tick(); 0];
    let c = e;
    var v = none();
    v = same(c);
    let d = default<i64[0]>();
    let l: i64[..0] = [];
    print(e.len, " ", f.len, " ", c.len, " ", v.len, " ", d.len, " ", l.len, " ", l.cap,
          " calls ", calls);
    print(e == f, " ", same(e) == none(), " ", v == d, " ", lens(e, l), " ", view(e),
          " ", view(f));
    print(pick(true).len, " ", pick(false).len, " calls ", calls, " ", g.len, " ", h.cap);
    print(e, " ", l, " ", pick(true));
    for x in e { print(x); }
}
