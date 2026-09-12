struct Copies { first: i64[], second: i64 }
fn use(xs: i64[], n: i64) { print(xs, " ", n); }
fn copies(xs: i64[>..<]&) -> i64[], i64 { return copy(xs), xs.pop(); }
fn main() {
    var xs: i64[>..<] = [41, 42];
    use(copy(xs), xs.pop());
    print(Copies { copy(xs), xs.pop() });
    xs.push(43);
    let a, n = copies(xs);
    print(a, " ", n);
}
