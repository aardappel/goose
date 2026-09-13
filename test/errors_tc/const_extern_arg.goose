// error: extern fn put: parameter s of type u8[:] takes a writable value; a read-only one needs the parameter declared const
// A C function's parameters are what they say (§7.10): a literal needs the
// parameter declared const.
extern "gs_os_write_stdout" fn put(s: u8[:]);
fn main() { put("abc"); }
