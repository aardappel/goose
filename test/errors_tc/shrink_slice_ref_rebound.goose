// error: cannot clear gs2 while p
// error: is still used: the slice it refers to may point into gs2
// p was bound while s viewed gs, but a `var` slice rebinds at its root's
// depth, so what p refers to may now view gs2 (§5.2, §9.2).
fn main() {
    var gs: i64[>..<] = [1];
    var gs2: i64[>..<] = [2];
    var s = gs[..];
    let p: i64[:]& = s;
    s = gs2[..];
    gs2.clear();
    print(p[0]);
}
