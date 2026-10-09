// A global variable and functions may share a name (§11.1): no variable can
// be called, so a call by that name names the functions, from the module
// declaring the variable and from every other one, and any other use of the
// name is the variable.
import std;
import ns.tally;

// As the benchmarks' helper module once kept its RNG state, beside std's
// last(xs).
var last: i32 = 5;
var count = 0;
var print_count = 0;

fn bump() -> i64 {
    count += 1;
    return count;
}

fn print_count() {
    print_count += 1;
    print("print_count ", print_count);
}

fn main() {
    let xs = [1, -2, 3];
    print(last(xs), " ", last);
    last = 7;
    print(xs.last(), " ", last);
    print(count(xs) { it > 0 }, " ", bump(), " ", count);
    print_count();
    print_count();
    tally::add(xs);
    tally::add(xs[..2]);
    print(tally::count, " ", tally::last);
}
