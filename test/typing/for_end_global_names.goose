// A typed for loop's range end or count is one past its binder type's
// largest value only as literals (§6.5). An end naming constants is a value
// of the binder's type, computed at run time as the same expression is
// anywhere (§6.2): a signed overflow wraps in a release build, a division
// by zero aborts. Each end here names globals whose arithmetic would fail
// as a constant: J * J, L + L and J * 16 overflow an i8, -N negates i8.min
// and N / -1 divides it by -1, SQ's initializer overflows an i16, ::J * ::J
// overflows naming the global J past a local one, and 10 / Z divides by
// zero, in a loop that does not run.
let J: i8 = 17;
let L: i8 = 100;
let N: i8 = -128;
let H: i16 = 257;
let SQ: i16 = H * H;
let Z: u8 = 0;

fn tenths(run: bool) -> i64 {
    var n = 0;
    if run {
        for i: u8 in 10 / Z { n += 1; }
    }
    n
}

fn main() {
    var a = 0;
    for i: i8 in 0..J * J { a += 1; }
    var b = 0;
    for i: i8 in -60..L + L { b += 1; }
    var c = 0;
    for i: i8 in N..-N + 3 { c += 1; }
    var d = 0;
    for i: i8 in N..N / -1 + 5 { d += 1; }
    print(a, " ", b, " ", c, " ", d);   // 33 4 3 5
    print(J * J, " ", L + L, " ", -N + 3, " ", N / -1 + 5);   // 33 -56 -125 -123

    var e = 0;
    for i: i8 in J * 16 { e += 1; }   // 272 wraps to 16
    var f = 0;
    for i: i16 in SQ { f += 1; }   // 66049 wraps to 513
    let J: i8 = 2;
    var g = 0;
    for i: i8 in ::J * ::J { g += J; }   // 33 iterations of the local J
    print(e, " ", f, " ", g, " ", tenths(false));   // 16 513 66 0
}
