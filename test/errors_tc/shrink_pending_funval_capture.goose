// error: cannot call step, which shrinks buf while r
// error: is still used: it may refer into buf (§5.2)
// A recursive function declared in a function value's body reaches the
// body's locals: a call to it applies what its first round recorded, which
// may pop the element r refers to.
fn apply<F>(n: i64) -> i64 { F(n) }
fn main() {
    print(apply(1) {
        var buf: i64[>..<] = [1, 2];
        recursive fn step(n: i64) -> i64 {
            if n > 0 {
                let r .= buf[1];
                let z = step(n - 1);
                return r + z;
            }
            buf.pop();
            return 0;
        }
        let v = step(it);
        v + buf.len
    });
}
