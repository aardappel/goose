// error: this return may be rooted where the recursive cycle stores nothing, but the cycle already used a result it could store
// The back edge's result, rooted at the globals its argument may view, was
// stored (§7.8). The return checked after it is rooted at b too, but may be
// `a`, which that back edge passed its own local for.
var G: u8[>..] = "ggg";
var G2: u8[>..] = "hhh";
var base: u8[1] = [48];
var kept: u8[:] = base[..];

recursive fn f(a: u8[:], b: u8[:], n: i64) -> u8[:] {
    if n == 0 { return b; }
    var loc: u8[3] = [108, 111, 99];
    let res = f(loc[..], if n > 5 { G[..] } else { G2[..] }, n - 1);
    kept = res;
    return if a.len > 3 { b } else { a };
}

fn main() {
    var x: u8[>..] = "xxxxxxx";
    {
        var y: u8[>..] = "yy";
        print(f(x[..], y[..], 2));
    }
    print(kept);
}
