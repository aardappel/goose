// error: cannot write through this reference
// k tells the iterations apart, each owning out[k] by it: a function the
// body hands a reference to k writes it no more than the body may, or two
// iterations would own out[0] (docs/design/child_threads.md §4.3 R2).
fn set0(x: u8&) { x = 0; }
fn main() {
    var out: i64[256] = [0; 256];
    par for k: u8 in 0..256 { set0(k); out[k] += 1; }
    print(out[0]);
}
