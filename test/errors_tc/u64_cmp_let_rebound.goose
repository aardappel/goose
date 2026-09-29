// error: comparing u64 with i64 relies on k being non-negative, but a writable reference bound to k at
// error: u64_cmp_let_rebound.goose:10 may make it negative
// §6.1, §3.8, §4.4: rebinding r to k makes assigning r write k.

fn main() {
    var x = 7;
    let k = 5;
    var r: i64& = x;
    print(r);
    r .= k;
    r = -1;
    let u: u64 = 3;
    print(u < k);
}
