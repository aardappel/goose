// runtime-debug all-levels
struct Box { view: i64[:] }
struct Cell { value: i64 }
var cells: Cell[>..] = [Cell { 10 }, Cell { 20 }, Cell { 30 }];
var first: i64[4] = [1, 2, 3, 4];
var second: i64[4] = [5, 6, 7, 8];
var target: i64[:] = first[..];
fn source() -> i64[:] { target = second[..]; return second[..]; }
fn move<T>(dst: T[:], src: T[:]) { dst.move_from(src); }
fn main() {
    var a = [1, 2, 3, 4, 5];
    a[1..].move_from(a[..4]);
    assert(a == [1, 1, 2, 3, 4]);
    move(a[..4], a[1..]);
    assert(a == [1, 2, 3, 4, 4]);
    a[..].move_from(a[..]);
    a[..0].move_from(default<i64[:]>());
    target.move_from(source());
    assert(first == second);
    var refs: i64&[2] = [a[0], a[1]];
    refs[1..].move_from(refs[..1]);
    assert(refs[1] .== a[0]);
    var boxes = [Box { a[..4] }, Box { a[..1] }];
    assert(boxes[0].view.len == 4);
    boxes[..1].move_from(boxes[1..]);
    assert(boxes[0].view.len == 1 && boxes[0].view[0] == 1);
    var bytes: u8[3] = [0; 3];
    bytes[..].move_from("abc");
    assert(bytes == "abc");
    var compact: (Cell&<u32 in cells>)[3] = [cells[0], cells[1], cells[2]];
    compact[1..].move_from(compact[..2]);
    assert(compact[1] .== cells[0] && compact[2] .== cells[1]);
    print("slice move: ok");
}
