// error: extern fn put: parameter s of type u8[:] takes a writable value; a read-only one needs the parameter declared const
// A slice loaded through a reference to a const-typed slice is read-only
// however writable the reference is, and a C function's slice parameter
// takes it only declared const (§7.10, §9.5).
extern "gs_os_write_stdout" fn put(s: u8[:]);
struct Box { s: const u8[:] }
fn out(p: const u8[:]&) { put(p); }
fn main() {
    var b = Box { s: "abc\n" };
    out(&b.s);
}
