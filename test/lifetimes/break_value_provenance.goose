fn block_view(a: i64[:]) -> i64[:] { block { break a; } }
fn loop_view(a: i64[:]) -> i64[:] { loop { break a; } }
fn choose(a: i64[:], first: bool) -> i64[:] {
    block { if first { break a; } a }
}
fn main() {
    var a: i64[2] = [42, 43];
    let x = block_view(a[..]);
    let y = loop_view(a[..]);
    x[0] = 44;
    print(y[0]);
    print(choose(a[..], false)[1]);
    let n = block { if a[0] == 44 { break 7; } 9 };
    print(n);
}
