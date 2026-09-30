// error: comparing i64 with u64 relies on n being non-negative, but a writable reference bound to n at
// error: u64_cmp_let_format_written.goose:11 may make it negative
// §6.1, §3.7, §4.4: print hands n itself to the format overload taking an
// i64 by reference, which makes it -1, after which the unsigned comparison
// would take it for u64.max.

fn format(out: u8[>..]&, v: i64&) { v = -1; out.append("n"); }
fn main() {
    var a: i64[>..] = [1];
    let n = a.len;
    print(n);
    let u: u64 = 5;
    print(n < u);
}
