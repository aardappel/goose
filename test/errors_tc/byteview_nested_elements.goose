// A byte view can cover an image-safe array nested anywhere in a value: here
// the flat points of a record in a struct's resizable tail, which a pop frees.
struct Pt { x: i32, y: i32 }
struct Shape { name: const u8[:], pts: Pt[2] }
struct Scene { title: const u8[:], shapes: Shape[>..<] }
var scene = Scene { title: "scene", shapes: [] };
fn main() {
    scene.shapes.push(Shape { name: "s", pts: [Pt { 1, 2 }, Pt { 3, 4 }] });
    let v = scene.shapes[0].pts.bytes_of();
    scene.shapes.pop();
    print(v.len);
}
