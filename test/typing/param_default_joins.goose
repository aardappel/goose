// A parameter's default that joins branches as a slice (§6.4, §7.1): a
// literal and an array, or arrays of two types at a parameter declared a
// slice, view the arrays themselves, as an argument's branches would.
var gname: u8[..8] = "global";
var gflag = true;
var ga3: i64[3] = [1, 2, 3];
var ga2: i64[2] = [10, 20];

fn label(s: const u8[:] = if gflag { gname } else { "none" }) -> const u8[:] { s }
fn total(xs: i64[:], extra: i64 = 0) -> i64 {
    var t = extra;
    for x in xs { t += x; }
    t
}
fn sum_default(xs: i64[:] = if gflag { ga3 } else { ga2 }) -> i64 { total(xs) }
fn bump_default(xs: i64[:] = if gflag { ga3 } else { ga2 }) { xs[0] += 100; }

fn main() {
    print(label());                                              // global
    gflag = false;
    print(label(), " ", label("given"));                         // none given
    var a: i64[3] = [1, 2, 3];
    var b: i64[2] = [4, 5];
    let c = a[0] > 0;
    print(total(if c { a } else { b }), " ", total(if c { a } else { b }, 10));   // 6 16
    print(sum_default(), " ", sum_default(b));                   // 30 9
    bump_default();
    gflag = true;
    bump_default();
    print(sum_default(), " ", ga2[0]);                           // 106 110
}
