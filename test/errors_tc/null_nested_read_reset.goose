// error: storing a reference rooted at loc, which does not outlive the destination
// store reads v, which bind has pointed into loc by then (§9.5). Every
// iteration declares its own v and loc; the loop's later passes find their
// v as the first found its own and share bind's check, which leaves v bound
// into loc there too. So store's check for the second iteration, where h is
// bound, finds v bound, and the reference would outlive the loc it points
// into (§7.5, §9.2).
struct Cell { key: i64 }
struct K { r: Cell&? }
fn pick(c: bool, a: Cell&) -> Cell&? {
    if c { return a; }
    return null;
}
fn main() {
    var outer = K { r: null };
    var h: K&? = null;
    for i in 2 {
        var loc: Cell[>..] = [Cell { key: 7 + i }];
        var v: Cell&? = null;
        fn bind() { v .= pick(true, loc[0]); }
        fn store() {
            let x = h;
            if x { x.r .= v; }
        }
        bind();
        store();
        h .= &outer;
    }
    let r = outer.r;
    if r { print(r.key); }
}
