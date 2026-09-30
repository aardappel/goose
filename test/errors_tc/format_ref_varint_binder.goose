// error: cannot write through this reference: non-writable provenance
// A varint is written only at construction (§3.6): one a reference names
// loads as the i64 it decodes to, which a format overload taking an i64 by
// reference is handed read-only (§3.7), even through a `for` binder over a
// `var` array of varints.
fn format(out: u8[>..]&, v: i64&) { v = 5; out.append("n"); }
fn main() {
    var a: varint[] = [1, 2];
    for e in a { print(e); }
}
