// error: array size K names a local variable, not a constant
// A block's body names what is in scope where it is written (§7.6): main's
// K, which hides the global.
let K = 3;

fn twice<F>(x: i64) -> i64 { F(x) + F(x) }

fn main() {
    let K = 5;
    print(twice(1) {
        let a: i64[K] = [it, it, it];
        a.len
    });
}
