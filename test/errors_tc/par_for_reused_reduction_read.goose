// error: total is a reduction of this par for
// §4.6: a function checked for one par for and reused in another is judged
// by the second too: run calls show, which reads total, which the second
// loop reduces.
fn run<F>() -> i64 { return F(); }

fn main() {
    var total = 0;
    var seen: i64[4] = [0, 0, 0, 0];
    fn show() -> i64 { return total; }
    par for k in 4 { seen[k] = run(show); }
    par for k in 4 {
        total += 1;
        seen[k] = run(show);
    }
    print(total, seen);
}
