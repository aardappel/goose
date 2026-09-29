// error: cannot write through this reference: non-writable provenance
// The elements a `for` walks through a writable reference to a const-typed
// slice are read-only, as the slice is (§6.5, §9.5).
struct Box { s: const u8[:] }
fn zap(p: const u8[:]&) { for &e in p { e = 65; } }
fn main() {
    var b = Box { s: "abc" };
    zap(&b.s);
}
