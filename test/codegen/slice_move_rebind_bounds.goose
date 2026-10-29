// runtime-debug all-levels
struct Box { view: i64[:] }
var values = [1, 2, 3, 4];
var first = [Box { values[..] }];
var second = [Box { values[..] }];
var replacement = [Box { values[..1] }];
var target: Box[:] = first[..];
fn source() -> Box[:] { target = second[..]; replacement[..] }
fn main() {
    let held .= first[0];
    assert(held.view.len == 4);
    target.move_from(source());
    assert(second[0].view.len == 4);
    print(held.view[3]);
}
