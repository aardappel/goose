// A holder returned by a recursive call still being checked may hold a byte
// view, which is stored like any reference.
struct Holder { bytes: const u8[:] }
fn store(out: Holder&, h: Holder) { out.bytes = h.bytes; }
recursive fn rec(out: Holder&, a: i64[>..]&, n: i64) -> Holder {
    if n > 0 { store(out, rec(out, a, n - 1)); }
    return Holder { a.bytes_of() };
}
fn fill(out: Holder&) {
    var a: i64[>..] = [42, 43];
    rec(out, a, 1);
}
fn main() {
    var out = Holder { "" };
    fill(out);
    print(out.bytes.len);
}
