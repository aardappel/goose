// index_of takes a reference rooted where its receiver is to be one of the
// receiver's elements wherever that storage holds the element type only as
// that array's elements (§3.3): a field of a struct, a parameter's class the
// caller's storage answers for, or a class passed on.
struct Pile { n: u8, items: i64[>..] }
struct Cell { v: i64 }
fn at(p: Pile&, r: i64&) -> i64 { p.items.index_of(r) }
fn whole(a: i64[>..]&, r: i64&) -> i64 { a.index_of(r) }
fn passed_on(a: i64[>..]&, r: i64&) -> i64 { whole(a, r) }
fn cells(c: Cell[3]&, i: i64) -> i64 { c.index_of(c[i]) }
fn unused(a: i64[>..]&) -> i64 { a.index_of(a[0]) }
fn main() {
    var p = Pile { n: 1, items: [10, 20, 30] };
    print(p.items.index_of(p.items[1]));              // 1
    print(at(p, p.items[2]));                         // 2
    var t: i64[>..] = [1, 2, 3, 4];
    print(whole(t, t[3]), " ", passed_on(t, t[1]));   // 3 1
    print(passed_on(p.items, p.items[0]));            // 0
    var g: Cell[3] = [Cell { v: 1 }, Cell { v: 2 }, Cell { v: 3 }];
    print(g.index_of(g[2]), " ", cells(g, 1));        // 2 1
}
