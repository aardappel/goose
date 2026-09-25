// error: storing a reference rooted at loc, which does not outlive the destination
// The first return may be `b` or `a` (§9.2). A back edge may give `a` the
// deeper argument (§7.8), here its own local, so its result may be rooted
// there, which the store into the global cannot outlive.
var G: u8[>..] = "ggg";
struct K { s: const u8[:] }
var keep: K[>..] = [];

recursive fn f(a: const u8[:], b: const u8[:], n: i64) -> const u8[:] {
    if n == 0 { return if a.len > 5 { b } else { a }; }
    var loc: u8[3] = [108, 111, 99];
    let r = f(loc[..], G[..], n - 1);
    keep.push(K { s: r });
    return b;
}

fn main() {
    var x: u8[>..] = "xxxxxxx";
    {
        var y: u8[>..] = "yyyyyyy";
        print(f(x[..], y[..], 1));
    }
    print(keep[0].s);
}
