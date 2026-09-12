fn make() -> i64[2] { return [1, 2]; }
fn main() {
    var indices: i64[>..<] = [0];
    print(make()[indices.pop()]);
    var lengths: i64[>..<] = [1];
    print(make()[..lengths.pop()]);
    var xs: i64[>..<] = [0];
    var other: i64[2] = [1, 2];
    fn replace() -> i64[:] { xs.pop(); return other[..]; }
    print(make() == replace());
    // A resizable receiver's header remains alive while its contents change.
    var items: i64[>..<] = [3, 4];
    items.push(items.pop());
    print(items);
}
