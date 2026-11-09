// runtime-debug: a full-width unsigned index prints as the value that was used.
fn main() {
    var a: i64[..10] = [0; 10];
    let i: u64 = 18446744073709551615;
    print(a[i]);
}
