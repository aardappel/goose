// error: extern fn put: parameter s of type u8[:] takes a writable value; a read-only one needs the parameter declared const
// A slice loaded through a reference to a slice variable holding a read-only
// slice is read-only, behind a recursive fn's parameter too, and a C
// function's slice parameter takes it only declared const (§7.10, §9.5).
extern "gs_os_write_stdout" fn put(s: u8[:]);
recursive fn out(p: u8[:]&, n: i64) {
    if n > 0 { out(p, n - 1); } else { put(p); }
}
fn text(s: u8[:]) { out(&s, 1); }
fn main() { text("abc\n"); }
