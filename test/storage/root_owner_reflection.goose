// runtime-debug: reflected access borrows independent components without copying the owner.
root struct S {
    first: i64[>..],
    code: varint,
    text: u8[],
    second: i64[>..<]
}
fn visit<T, F>(s: T&) {
    const for <N, A> in fields<T> { F(N, &s.A); }
}
fn show(s: const S&) {
    visit(s) { name, field => print(name, "=", field); }
}
fn main() {
    var s = S { [1], 1234, "hello", [2] };
    show(s);
    s.first.push(3);
    print(s);
}
