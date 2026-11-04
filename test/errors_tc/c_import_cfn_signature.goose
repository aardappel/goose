// error: cfn(cb) is fn(i32) -> i32, where C calls fn(u64, i32) -> i32
// C calls a callback with the parameters its function pointer type names:
// an export fn of another signature would read them wrongly (spec §7.10).
import c "c_import_cfn_signature.h";
export fn cb(v: i32) -> i32 { v }
fn main() { cfs_each(cfn(cb), 0); }
