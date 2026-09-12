// A cached helper's literal must also fit the second target specialization.
fn finish() { return 300 from choose; }
fn relay() { finish(); }
fn choose<T>(x: T) -> T { relay(); return x; }
fn main() {
    var wide: i64 = 1;
    var small: u8 = 1;
    print(choose(wide));
    print(choose(small));
}
