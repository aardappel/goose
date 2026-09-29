// error: comparing u64 with i64 relies on G being non-negative, but a writable reference bound to G at
// error: u64_cmp_let_address.goose:9 may make it negative
// §6.1, §4.4: spoil writes the global `let` G through &G, so no comparison
// may rely on G being non-negative, in whichever function it is.

let G = 5;
fn fresh() -> bool { let u: u64 = 3; u < G }
fn spoil() {
    let r = &G;
    r = -1;
}
fn main() {
    spoil();
    print(fresh());
}
