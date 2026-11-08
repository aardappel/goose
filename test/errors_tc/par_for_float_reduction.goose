// error: cannot accumulate total in a par for: a float is no reduction
// §4.6: an integer is accumulated exactly in any order; a float sum depends
// on how the iterations are cut into chunks, so it goes through partials.
fn main() {
    let xs = [0.5, 1.5, 2.5, 3.5];
    var total = 0.0;
    par for x in xs { total += x; }
    print(total);
}
