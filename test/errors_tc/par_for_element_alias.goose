// error: xs is partitioned by this par for, each iteration owning the element of xs the iteration's index names
// R2 (§4.4): `par for &x in xs` gives iteration i xs[i]; xs[0] is another
// iteration's.
fn main() {
    var xs = [1, 2, 3, 4];
    par for &x in xs { x = xs[0] + 1; }
    print(xs);
}
