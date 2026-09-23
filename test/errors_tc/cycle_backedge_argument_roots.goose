// error: storing a reference rooted at loc, which does not outlive the destination
// A back edge reuses f's body whatever it passes (§7.8): the entry call gives
// `a` and `b` views of the same x, so one parameter class stands for both,
// while the back edge passes a global for `a` and its own local for `b`, the
// one f returns. The class maps back through both arguments.
var G: u8[>..] = "ggg";
struct K { s: const u8[:] }
var keep: K[>..] = [];

recursive fn f(a: const u8[:], b: const u8[:], n: i64) -> const u8[:] {
    if n == 0 { return b; }
    var loc: u8[3] = [108, 111, 99];
    let r = f(G[..], loc[..], n - 1);
    keep.push(K { s: r });
    return b;
}

fn main() {
    var x: u8[>..] = "xxxxxxx";
    print(f(x[..], x[..], 1));
    print(keep[0].s);
}
