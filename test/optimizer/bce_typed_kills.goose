// An array is of one type, and a reference reaches only an array of its
// own type, so a growth or shrink through one, or a callee's through an
// argument holding scalars, leaves the lengths of other types' arrays alone
// (§10.5). An array of structs of scalars counts as one holding scalars:
// its elements hold no array and lead to none. Same-typed arrays may be one,
// and keep their checks.

import std;

fn same_type(a: i32[>..<]&, b: i32[>..<]&) -> i32 {
    guard b.len > 0 else { return 0; }
    a.clear();
    return b[0];                               // bce:keep (a may be b)
}

fn other_type(a: u32[>..<]&, b: i32[>..<]&) -> i32 {
    guard b.len > 0 else { return 0; }
    a.clear();
    return b[0];                               // bce:elide (a u32 array is not b)
}

fn other_kind(a: i32[>..<]&, b: i32[>..]&) -> i32 {
    guard b.len > 0 else { return 0; }
    a.clear();
    return b[0];                               // bce:elide (a grow-shrink array is not b)
}

fn drain(a: i32[>..<]&) { a.clear(); }
fn drain_u(a: u32[>..<]&) { a.clear(); }

fn callee_same(a: i32[>..<]&, b: i32[>..<]&) -> i32 {
    guard b.len > 0 else { return 0; }
    drain(a);
    return b[0];                               // bce:keep (a may be b)
}

fn callee_other(a: u32[>..<]&, b: i32[>..<]&) -> i32 {
    guard b.len > 0 else { return 0; }
    drain_u(a);
    return b[0];                               // bce:elide
}

// Returning two values keeps these callees calls (the inliner leaves such a
// function out of line), so their callers see them through their summaries.
struct Tax { rate: i64, amount: f64 }
struct Totals { taxes: Tax[..4], net: f64 }
root struct Ledger { items: f64[>..] }
root struct Books { totals: Totals, lines: f64[>..] }

fn add_tax(taxes: Tax[..4]&, rate: i64) -> i64, bool {
    for &t in taxes { if t.rate == rate { t.amount += 1.0; return taxes.len, true; } }
    taxes.push(Tax { rate: rate, amount: 1.0 });
    return taxes.len, true;
}

fn drop_taxes(taxes: Tax[..4]&) -> i64, bool {
    taxes.clear();
    return 0, true;
}

fn callee_records(t: Tax[..4]&, b: i32[>..<]&) -> i32 {
    guard b.len > 0 else { return 0; }
    add_tax(t, 22);
    return b[0];                               // bce:elide (a Tax array is not b)
}

fn callee_records_same(t: Tax[..4]&, u: Tax[..4]&) -> i64 {
    guard u.len > 0 else { return 0; }
    drop_taxes(t);
    return u[0].rate;                          // bce:keep (t may be u)
}

fn callee_root_field(s: Ledger&, b: Books&, k: i64) -> f64 {
    guard k >= 0 && k < s.items.len else { return 0.0; }
    add_tax(b.totals.taxes, 22);
    return s.items[k];                         // bce:elide (a Tax array is not s.items)
}

fn fresh(v: i32) -> i32[>..<] {
    var x: i32[>..<] = [];
    x.push(v);
    return x;
}

fn main() {
    var u: u32[>..<] = [];
    u.push(1);
    var g: i32[>..] = [];
    g.push(8);
    var x = fresh(7);
    print(other_type(u, x), " ", callee_other(u, x));
    var y = fresh(9);
    print(other_kind(y, g));
    var p = fresh(1);
    var q = fresh(2);
    var r = fresh(3);
    print(same_type(p, q), " ", callee_same(r, q));
    var t: Tax[..4] = [];
    var t2: Tax[..4] = [Tax { rate: 4, amount: 0.0 }];
    var z = fresh(5);
    print(callee_records(t, z), " ", t.len, " ", callee_records_same(t, t2), " ", t.len);
    var s = Ledger { items: [1.5, 2.5] };
    var b = Books { totals: Totals { taxes: [], net: 0.0 }, lines: [] };
    print(callee_root_field(s, b, 1), " ", b.totals.taxes.len);
}
