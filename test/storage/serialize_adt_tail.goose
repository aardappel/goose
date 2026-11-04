// The image of a fixed-mode enum is the value's whole slot, as wide as its
// largest variant: a smaller variant leaves a tail nothing wrote, and those
// bytes must be zeros -- whatever the C stack held before -- or a save file
// carries native stack residue out of the process.
enum Shape { Circle { r: f32 }, Rect { w: f32, h: f32 }, Dot }

fn main() {
    var shapes: Shape[>..] = [];
    for i in 3 { shapes.push(Shape.Circle { r: 1.0 + i }); }
    shapes.push(Shape.Dot {});
    let image = to_bytes(shapes);
    var nonzero = 0;
    for b in image { if b != 0 { nonzero++; } }
    print("bytes ", image.len, " nonzero ", nonzero);
}
