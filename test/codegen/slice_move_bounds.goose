// runtime-debug all-levels
struct Box { view: i64[:] }
fn change(dst: Box[:], src: Box[:]) { dst.move_from(src); }
fn main() {
    var a = [1, 2, 3, 4];
    var boxes = [Box { a[..] }, Box { a[..1] }];
    let before .= boxes[0];
    assert(before.view.len == 4);
    change(boxes[..1], boxes[1..]);
    print(before.view[3]);
}
