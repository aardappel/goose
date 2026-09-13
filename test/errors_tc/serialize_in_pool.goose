// error: to_bytes cannot write Cell[>..] out: Cell&<u32 in gpool>? is measured from gpool, not from the image
// An `in pool` offset is measured from a named global's base, not from the
// image, so neither direction of serialization admits one.
var gpool: Cell[>..] = [];
struct Cell { k: i64, next: Cell&<u32 in gpool>? }

fn main() {
    var a: Cell[>..] = [];
    var img: u8[>..] = a.to_bytes();
    print(img.len);
}
