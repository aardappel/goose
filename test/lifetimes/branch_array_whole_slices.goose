// A construct whose branches each make a new array is an array, a temporary
// of the statement around it, so a slice of the whole construct lives until
// that statement ends (§9.2), where a slice taken in each branch would not
// outlive the branch (errors_tc/branch_temp_string_argument).
import std;

fn take(s: u8[:]) { print(s); }

fn sum(xs: const i64[:]) -> i64 {
    var t = 0;
    for x in xs { t += x; }
    return t;
}

fn go(k: i64, n: i64) {
    take((if k > 0 { str("a", n) } else { str("b") })[..]);
    take((match k { 0 => str("c", n), _ => str("d") })[..]);
    take(({ let m = n + 1; str("e", m) })[..]);
    take((if k > 0 { str("f", n) } else if k < 0 { str("g") } else { str("h", k) })[..]);
    let s = if k > 0 { str("i", n) } else { str("j") };
    take(s);
    print(sum((if k > 0 { [k, n] } else { [n, n + 1] })[..]), " ",
          sum((if k == 0 { [1, 2, 3] } else { [k; 3] })[..]));
}

fn main() {
    go(0, 3);
    go(1, 4);
    go(-1, 5);
}
