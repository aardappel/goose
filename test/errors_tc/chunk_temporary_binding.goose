// error: binding c to a slice rooted at a temporary, which does not outlive it
// A chunk of a call's array result views the temporary holding it, which
// ends with its statement, as a slice of it would (§9.2).
fn make() -> i64[4] { [1, 2, 3, 4] }
fn main() {
    let c = make().chunk(0, 2);
    print(c);
}
