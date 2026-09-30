// error: cannot write through this reference: non-writable provenance
// Through a `const T&`, a self-relative link leads back into the pointee
// (§3.9), which stays read-only however it is reached (§9.5).
struct Ring { next: Ring&<u8>, v: i64 }
fn inc(p: i64&) { p = p + 1; }
fn bump(r: const Ring&) { inc(r.next.v); }
fn main() {
    var r = Ring { next: self, v: 1 };
    bump(r);
    print(r.v);
}
