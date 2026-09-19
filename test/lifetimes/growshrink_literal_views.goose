// What a literal may hold wherever it lands, an argument or a result (§5.2):
// a slice read out of a grow-shrink list of strings, which points at the
// text and not into the list, and a view of a grow-only array. A view of a
// grow-shrink array itself is passed down and returned as it is.
struct Name { s: const u8[:] }
struct Nums { v: i64[:] }
var names: const u8[:][>..<] = ["ab", "cd"];
fn keep(n: Name) -> Name { n }
fn first(i: i64) -> Name { Name { names[i] } }
fn total(v: i64[:]) -> i64 {
    var t = 0;
    for x in v { t += x; }
    return t;
}
fn sum(n: Nums) -> i64 { total(n.v) }
fn same(n: Nums) -> Nums { n }
fn tail(v: i64[:]) -> i64[:] { v[1..] }
fn main() {
    let a = keep(Name { names[1] });
    let b = first(0);
    names.pop();
    names.pop();
    print(a.s, " ", b.s, " ", names.len);
    var go: i64[>..] = [1, 2, 3];
    print(sum(Nums { go[..] }), " ", sum(same(Nums { go[1..] })));
    var gs: i64[>..<] = [4, 5, 6];
    print(total(tail(gs[..])));
    gs.pop();
    print(gs.len);
}
