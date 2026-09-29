// error: variable n may be used before it is assigned
// An else block that does not leave runs instead of the rest of its block,
// where n is assigned (§4.4, §6.4).
fn run(c: bool) {
    var n: i64;
    {
        guard c else { print("no"); }
        n = 5;
    }
    print(n);
}
fn main() { run(true); run(false); }
