// error: the match arm's value is a slice rooted at p, which does not outlive it
// A by-value binder's array is the arm's own copy, which ends with the arm
// (§8.1, §9.2).
enum Opt { None, Some { name: u8[..8] } }
fn main() {
    var o: Opt = Opt.Some { name: "abc" };
    let s = match o { Some p => p.name, None => "none" };
    print(s);
}
