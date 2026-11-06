import std;

fn main() {
    let xs = [1, 2, 3];
    each_chunk(xs, 0) { c, i => print(c); };
}