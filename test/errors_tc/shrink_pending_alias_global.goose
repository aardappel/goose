// A local bound by reference stands for whatever it may alias: clearing
// through one named like the global it binds counts as clearing the global.
var g: i64[>..] = [1, 2];
recursive fn step(n: i64) -> i64 {
    if n > 0 {
        let r .= g[0];
        let z = step(n - 1);
        return r + z;
    }
    let g .= g;
    g.clear();
    return 0;
}
fn main() {
    let v = step(1);
    print(v);
}
