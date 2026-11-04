// error: argument 1: cannot pass u8 as const cptr (C would take its value for an address
// A `void *` is a cptr: an integer other than a u64 would reach C as an
// address, so `b[0]` meant as the byte's is an error; pass `b` (spec §7.10).
import c "c_import_cptr_narrow.h";
fn main() {
    let b: u8[4] = [1, 2, 3, 4];
    cpn_sum(b[0], 4);
}
