// A typed for loop's range end or count is one past its binder type's
// largest value only as a literal (§6.5), and a name in it is the local of
// the name wherever one is in scope (§11.1), whatever the global it hides
// holds. Each end here names locals whose globals would not compute in
// its place: -K would negate a u8, M * M and -N overflow, N / -1 divide
// the least i64 by -1, 10 / Z and 12 / Z divide by zero, and W + V add a
// u64 to an i64. The locals are a let, a var, a parameter, variables around
// a nested function and a block, a block's parameter, an enclosing loop's
// binder and a local in a global's initializer.
let K: u8 = 200;
let M = 1 << 40;
let N = -9223372036854775807 - 1;
let Z = 0;
let W: u64 = 5;
let V = 7;

let G = block {
    let K: i8 = 2;
    var s = 0;
    for i: i8 in -4..-K { s += i; }
    s
};

fn apply<F>(x: i8) -> i64 { F(x) }

fn tenths(Z: u8) -> i64 {
    var n = 0;
    for i: u8 in 10 / Z { n += 1; }
    n
}

fn main() {
    let K: i8 = 5;
    var s = 0;
    for i: i8 in -9..-K { s += i; }
    var M: u8 = 3;
    var n = 0;
    for i: u8 in M * M { n += i; }
    print(s, " ", n, " ", tenths(5), " ", tenths(3), " ", G);   // -30 36 2 3 -7

    let N: i8 = -3;
    fn upto() -> i64 {
        var t = 0;
        for i: i8 in 0..-N { t += i; }
        for i: i8 in N / -1 { t += 10 * i; }
        t
    }
    let W: u8 = 2;
    let V: u8 = 1;
    let viablock = apply(4) { x: i8 =>
        var c = 0;
        for i: u8 in W + V { c += x; c += i; }
        c
    };
    let viaparam = apply(3) { K: i8 =>
        var c = 0;
        for i: i8 in -9..-K { c += 1; }
        c
    };
    var visits = 0;
    for Z: u8 in 1..4 {
        for i: u8 in 12 / Z { visits += 1; }
    }
    print(upto(), " ", viablock, " ", viaparam, " ", visits);   // 33 15 6 22
}
