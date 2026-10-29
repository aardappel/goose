// runtime-debug all-levels
// Root references a callee only projects or forwards travel as the
// descriptor's members; one that is returned, stored or captured keeps the
// descriptor's address. Multiple results keep the helpers out of line.
root struct S { first: i64[>..] = [], second: i64[>..] = [], count: i64 = 0 }
struct H { s: S& }
var global = S { .. };

fn fill(s: S&, n: i64) -> i64, i64 {
    for i in n { s.first.push(i); s.second.push(10 * i); }
    s.count += n;
    return s.first.len, s.second.len;
}
fn forward(s: S&, n: i64) -> i64, i64 { let a, b = fill(s, n); return a + 1, b; }
fn aliased(s: S&) -> i64, i64 {
    let t = s;
    t.first.push(7);
    return t.first.len, s.count;
}
recursive fn deep(s: S&, n: i64) -> i64 {
    s.second.push(n);
    if n > 0 { return deep(s, n - 1) + 1; }
    return s.second.len;
}
// Two owners or one, through the same pair of parameters.
fn pair(a: S&, b: S&) -> i64, i64 {
    a.first.push(1); b.first.push(2); a.second.push(a.first.len);
    return a.first.len, b.first.len;
}
fn same(s: S&) -> S& { return s; }
fn via_same(s: S&) -> i64, i64 {
    same(s).first.push(3);
    return s.first.len, 0;
}
fn hold(s: S&) -> H { return H { s }; }
fn stored(s: S&, n: i64) -> i64, i64 {
    var hs: H[>..] = [];
    hs.push(H { s });
    hs[0].s.first.push(n);
    return hs[0].s.first.len, s.first.len;
}
fn captured(s: S&) -> i64, i64 {
    fn bump() { s.first.push(9); s.count += 1; }
    bump();
    bump();
    return s.first.len, s.count;
}
fn holder_arg(h: H&, n: i64) -> i64, i64 { return fill(h.s, n); }

fn main() {
    var s = S { .. };
    let a, b = fill(s, 3); print(a, " ", b);
    let c, d = forward(s, 2); print(c, " ", d);
    let e, f = aliased(s); print(e, " ", f);
    print(deep(s, 3));
    let g, i = pair(s, global); print(g, " ", i);
    let j, k = pair(s, s); print(j, " ", k);
    let l, m = via_same(s); print(l, " ", m);
    hold(s).s.second.push(99);
    let n, o = stored(s, 42); print(n, " ", o);
    let q, r = captured(s); print(q, " ", r);
    var h = H { global };
    let t, u = holder_arg(h, 2); print(t, " ", u);
    let v, w = fill(global, 1); print(v, " ", w);
    print(s.first, " ", s.second, " ", s.count);
    print(global.first, " ", global.second, " ", global.count);
}
