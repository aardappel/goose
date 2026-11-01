// A case function in a recursive cycle takes a variable-size variant by
// reference: the dispatch copies nothing, so no data stack is held across
// the call back into the cycle (§7.8).
enum E { Leaf { s: u8[] }, Name { t: E..&<u32 in pool> } }
var pool: E..[>..] = [];
var outs: i64[>..<] = [];
recursive fn ev(n: E.Leaf&, x: i64) { outs.push(x + n.s.len); }
recursive fn ev(f: E.Name&, x: i64) { ev(f.t, x); outs.push(1); }
fn main() {
    let l .= pool.push(E.Leaf { s: "abc" });
    ev(pool.push(E.Name { t: l }), 7);
    print(outs[0], " ", outs[1]);
}
