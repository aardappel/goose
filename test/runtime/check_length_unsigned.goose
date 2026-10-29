// runtime-debug: an unsigned count must not wrap into an accepted signed count.
fn main() {
    var a: i64[>..] = [];
    let n: u64 = 18446744073709551615;
    print(a.check_length(n));
}
