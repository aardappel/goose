// A resizable value evaluated only for its effects still gets the checks on
// its path: a frame object's tail reached through an indexed reference
// aborts out of range, as a read of its fixed field there would.

struct Fo { n: i64, items: u8[>..] }     // a frame object

fn at(refs: Fo&[:], i: i64) {
    refs[i].items;
    print("in range ", i);
}
fn main() {
    var a = Fo { n: 1, items: [1] };
    var b = Fo { n: 2, items: [2] };
    let refs: Fo&[2] = [a, b];
    for i in 3 { at(refs, i); }
    print("not reached");
}
