// error: a is not fixed-size and is not copied implicitly
// A match's scrutinee is no part of the value the match gives, even where
// that is the function's result: the length it reads is of a copy of `a`,
// which takes copy() (§4.1).
fn pick(c: bool) {
    var a: i64[>..] = [1, 2, 3];
    var b: i64[>..] = [4, 5];
    match (if c { a } else { b }).len { 3 => a, _ => b }
}
fn main() { print(pick(true).len); }
