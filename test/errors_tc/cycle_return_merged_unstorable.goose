// error: storing a reference rooted at loc, which does not outlive the destination
// The back edge's result may be b, or `a`, which that back edge passed its
// own local for (§7.8): it may not be kept in the global kept.
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
