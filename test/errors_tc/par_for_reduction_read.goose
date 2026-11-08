// error: count is a reduction of this par for
// error: so the body may only accumulate it, never read it or write it otherwise
// §4.6: a reduction is kept per chunk, so no iteration sees its total.
fn main() {
    var count = 0;
    var seen: i64[4] = [0, 0, 0, 0];
    par for k in 4 {
        count++;
        seen[k] = count;
    }
    print(count, seen);
}
