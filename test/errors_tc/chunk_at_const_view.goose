// error: cannot write through this reference: non-writable provenance
// A chunk of a read-only view is read-only too (§9.5), in a callee's
// instantiation as well.
fn zero(s: i64[:]) { for &x in s { x = 0; } }
fn main() {
    var a: i64[4] = [1, 2, 3, 4];
    let view:   const i64[:] = a;
    let bounds: i64[3]       = [0, 2, 4];
    zero(view.chunk_at(bounds, 1));
    print(a);
}
