// error: array size K names a local variable, not a constant
// A name in a size resolves as any name does (§3.3, §11.1): the local hides
// the global constant, and a local is no constant. Taking the global instead
// made a 3 elements long while K read 5.
let K = 3;

fn main() {
    let K = 5;
    var a: i64[K] = [0; K];
    print(a.len, " ", K);
}
