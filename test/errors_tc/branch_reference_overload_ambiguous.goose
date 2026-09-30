// error: ambiguous call to set: multiple overloads match equally well
// A construct whose branches all name storage matches a reference parameter
// as that storage does (§4.1), so where a value and a reference overload
// both take a variable, they both take the construct.
fn set(v: i64&) { v = 7; }
fn set(v: i64) { print(v); }
fn main() {
    var a = 1;
    var b = 2;
    set(if a < b { a } else { b });
}
