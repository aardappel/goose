// error: cannot call pop_first, which may shrink a while v is still used
// pop_first pops the array a reference in the slice its parameter's slot
// holds points at: read out of that slice, the reference names no array
// the call can tell, only one the slot's storage outlives, so a may shrink
// while v views it (§5.1).
fn pop_first(p: (i64[>..]&)[:]&) {
    let r = p[0];
    r.pop();
}
fn main() {
    var a: i64[>..] = [1, 2, 3];
    var refs: (i64[>..]&)[1] = [a];
    var s = refs[..];
    let v = a[2..3];
    pop_first(s);
    print(v[0]);
}
