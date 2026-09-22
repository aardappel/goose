// error: walk calls into its recursive cycle while non-fixed-size local scratch
// A local declared before a loop is in scope for all of it, including the
// call into the cycle its body makes (§7.8); clearing it does not end it.
recursive fn walk(n: i64) -> i64 {
    var scratch: i64[>..] = [];
    var total = 0;
    for i in n {
        scratch.clear();
        scratch.push(i);
        total += scratch[0] + walk(i);
    }
    return total;
}
fn main() { print(walk(4)); }
