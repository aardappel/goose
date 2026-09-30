// error: cannot write through this reference: non-writable provenance
// default<i64>() is a value built in a temporary, as a literal is (§9.2): a
// format overload taking it by reference is given that temporary, read-only
// (§3.7), as a literal's.
fn format(out: u8[>..]&, v: i64&) { v = 9; out.append("x"); }
fn main() {
    print(default<i64>());
}
