// error: binding s to a slice rooted at a temporary, which does not outlive it
// An if's value is a temporary of the statement that makes it, even where
// its branch names a variable: a slice of it bound to a variable would
// outlive it (§9.2).
fn main() {
    let a: i64[3] = [1, 2, 3];
    let c = a.len > 2;
    let s = (if c { a } else { a })[..];
    print("the statement after the binding prints this line");
    print(s[0], " ", s.len);
}
