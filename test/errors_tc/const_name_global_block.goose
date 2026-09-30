// error: array size K names a local variable, not a constant
// A block in a global's initializer has locals of its own, which hide the
// global K there as anywhere (§3.3, §11.1).
let K = 3;
let LEN = {
    let K = 5;
    let a: i64[K] = [0; K];
    a.len
};

fn main() { print(LEN); }
