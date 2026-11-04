// error: ev calls into its recursive cycle through ev while case parameter n holds a by-value copy of non-fixed-size E.Leaf
// The dispatch copies f.t for the by-value case before it reads the tag and
// holds the copy across the other cases, one of which calls back (§7.8).
enum E { Leaf { s: u8[] }, Name { t: E..&<u32 in pool> } }
var pool: E..[>..] = [];
var outs: i64[>..<] = [];
recursive fn ev(n: E.Leaf, x: i64) { outs.push(n.s.len); }
recursive fn ev(f: E.Name&, x: i64) { ev(f.t, x); outs.push(1); }
fn main() {
    let l .= pool.push(E.Leaf { s: "abc" });
    ev(pool.push(E.Name { t: l }), 7);
    print(outs.len);
}
