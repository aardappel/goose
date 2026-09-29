// error: comparing u64 with i64 relies on j being non-negative, which rests on k, but a writable reference bound to k at
// error: u64_cmp_let_through_let.goose:12 may make it negative
// §6.1, §4.4: j is only known non-negative because k was when j was bound;
// in the loop, setneg has made k -1 by the second iteration's `let j = k`.

fn setneg(r: i64&) { r = -1; }
fn main() {
    let k = 5;
    let u: u64 = 3;
    for i in 2 {
        let j = k;
        setneg(k);
        print(u < j);
    }
}
