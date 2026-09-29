// error: storing a reference rooted at loc, which does not outlive the destination
// Every iteration of the outer loop declares its own v and loc. The inner
// loop's store stores v through h, then bind points v into loc, so its
// second iteration would store a reference into a loc that ends with the
// outer iteration. store reads h as the null it is in the outer loop's
// first pass (§9.5). The second pass, where h is bound, shares bind's check,
// which leaves v bound there as the first pass's did, and the inner loop's
// next pass sees that as it would the check's own rebind (§7.5, §9.2).
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
        for j in 2 {
            store();
            bind();
        }
        h .= &outer;
    }
    let r = outer.r;
    if r { print(r.key); }
}
