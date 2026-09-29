// error: variable n may be used before it is assigned
// The bare guard skips the rest of setn, where n is assigned (§4.4, §6.4).
fn run(c: bool) {
    var n: i64;
    fn setn(d: bool) {
        guard !d;
        n = 5;
    }
    setn(c);
    print(n);
}
fn main() { run(false); run(true); }
