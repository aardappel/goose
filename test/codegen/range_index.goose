// runtime-debug: checked full ranges, empty slices, root fields and argument order.
root struct Buffers { first: i64[>..], second: i64[>..] }
struct View { s: i64[:] }
fn locate(a: i64[>..]&, v: View) -> i64 { a.range_index(v.s) }
fn main() {
    var a = Buffers { [10, 20, 30, 40], [50] };
    assert(a.first.range_index(a.first[1..3]) == 1);
    assert(locate(a.first, View { a.first[2..] }) == 2);
    assert(a.first.range_index(a.second[..0]) == 0);
    assert(a.first.range_index(default<i64[:]>()) == 0);
    var fixed = [1, 2, 3];
    var limited: i64[..8] = [4, 5, 6];
    assert(fixed.range_index(fixed[1..]) == 1);
    assert(limited.range_index(limited[2..]) == 2);
    var other = Buffers { [1], [2, 3] };
    var receiver .= a;
    var count = 0;
    fn slice_arg() -> i64[:] { count++; receiver .= other; return a.first[1..]; }
    assert(receiver.first.range_index(slice_arg()) == 1);
    assert(count == 1);
    fn length_arg() -> i64 { count++; return 17; }
    assert(a.first.check_length(length_arg()) == 17 && count == 2);
    assert(a.first.len == 4);
    print("checked ranges");
}
