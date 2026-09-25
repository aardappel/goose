// error: cannot call step, which grows a: the element pushed into a is still under construction
// A recursive function declared in a function value's body reaches the
// body's locals: a call to it applies what its first round recorded, here
// growing the array the element it returns is being built in (§1.3(4)).
fn apply<F>(n: i64) -> i64 { F(n) }
fn main() {
    print(apply(1) {
        var a: u8[][>..] = [];
        recursive fn step(n: i64) -> u8[] {
            if n > 0 { a.push(step(n - 1)); }
            a.push("x");
            return "y";
        }
        step(it);
        a.len
    });
}
