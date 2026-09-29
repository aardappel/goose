// error: cannot assign to let x
// init's first call gives x its value; its second finds x assigned, so it
// is checked again, and assigns the let a second time (§4.4, §7.5).
fn main() {
    let x: i64;
    fn init(n: i64) { x = n; }
    init(5);
    print(x);
    init(6);
    print(x);
}
