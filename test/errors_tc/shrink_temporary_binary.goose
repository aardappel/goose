fn main() {
    var a: i64[>..<] = [42];
    print(a[..] == (block { a.pop(); break [42]; }));
}
