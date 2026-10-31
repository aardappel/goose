// runtime-define: GS_CONTRACTS=1
// A loop invariant is checked at the top of every iteration, so the
// iteration after the one that breaks it aborts. Expected to abort after
// the third line.

fn main() {
    let xs = [4, 2, -9, 1];
    var sum: i64 = 0;
    for i in xs.len invariant sum >= 0 {
        sum += xs[i];
        print(sum);
    }
}
