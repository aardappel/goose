// A case function in a recursive cycle may take a fixed-size variant of a
// variable-mode enum by value (§7.8): the dispatch copies only that variant,
// not the whole value onto a data stack held across the call (EmitDispatch).
enum E { Leaf, Num { v: i64 }, Name { t: E..&<u32 in pool> } }
var pool: E..[>..] = [];
var outs: i64[>..<] = [];
recursive fn ev(n: E.Leaf, x: i64) { outs.push(x); }
recursive fn ev(n: E.Num, x: i64) { outs.push(x + n.v); }
recursive fn ev(f: E.Name&, x: i64) { ev(f.t, x); outs.push(1); }
fn main() {
    let l .= pool.push(E.Leaf);
    let n .= pool.push(E.Num { v: 3 });
    ev(pool.push(E.Name { t: l }), 7);
    ev(pool.push(E.Name { t: n }), 7);
    print(outs[0], " ", outs[1], " ", outs[2], " ", outs[3]);
}
