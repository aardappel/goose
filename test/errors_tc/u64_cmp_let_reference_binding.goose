// error: comparing u64 with i64 relies on k being non-negative, but a writable reference bound to k at
// error: u64_cmp_let_reference_binding.goose:8 may make it negative
// §6.1, §3.8, §4.4: `let r .= k` binds r to k itself, and assigning r writes
// k.

fn main() {
    let k = 5;
    let r .= k;
    r = -1;
    let u: u64 = 3;
    print(u < k);
}
