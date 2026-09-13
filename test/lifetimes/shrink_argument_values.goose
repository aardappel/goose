fn sum(x: i64, y: i64) -> i64 { x + y }
fn read(r: i64&, unused: i64) { print(r); }
fn consume(r: i64&, a: i64[>..<]&) { print(r); a.pop(); }
struct Holder { r: i64& }
fn read_holder(value: Holder, unused: i64) { print(value.r); }
fn main() {
    var a: i64[>..<] = [42, 7];
    // The earlier scalar is copied; its source may then be removed.
    print(sum(a[1], a.pop()));
    var other: i64[>..<] = [9];
    read(a[0], other.pop());
    // A reference used by a completed call does not stay live afterward.
    a.pop();
    print(a.len);
    // Binary scalar/fixed aggregate operands are values, not live views.
    var scalar: i64[>..<] = [42];
    print(scalar[0] + scalar.pop());
    var rows: i64[2][>..<] = [[1, 2]];
    print(rows[0] == rows.pop());
    // A completed expression releases its temporary view.
    var values: i64[>..<] = [8, 9];
    print(values[..] == [8, 9]);
    values.pop();
    print(values.len);
    // Bounds may shrink unrelated arrays. The receiver's own header is
    // also stable across calls that consume a reference before shrinking.
    var indices: i64[>..<] = [0];
    print((values[..])[indices.pop()]);
    var lengths: i64[>..<] = [1];
    print((values[..])[..lengths.pop()]);
    consume(values[0], values);
    print(values.len);
    var fixed = 27;
    var disposable: i64[>..<] = [0];
    read_holder(Holder { fixed }, disposable.pop());
}
