// error: n is bound to a writable reference here, through which it may become negative, while the comparison with a u64 at
// error: u64_cmp_let_format_written_after.goose:13 relies on its value being non-negative
// §6.1, §3.7, §4.4: the comparison comes first, but str hands n itself to
// the format overload taking an i64 by reference, and the loop's second
// comparison would see the -1 it writes.

fn format(out: u8[>..]&, v: i64&) { v = -1; out.append("n"); }
fn main() {
    var a: i64[>..] = [1];
    let n = a.len;
    let u: u64 = 5;
    for i in 2 {
        print(n < u);
        let s = str(n);
        print(s);
    }
}
