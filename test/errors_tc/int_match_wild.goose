// Integer matches require a _ arm (§8.1).
fn main() {
    let v = match 3 { 0 => 1, 1..10 => 2 };
    print(v);
}
