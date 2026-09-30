// Algebraic data types, the only dynamic polymorphism in the language: no
// inheritance, no vtables. The classic shapes example, with the two things
// that make Goose's version different -- an enum value can be stored
// *fixed* (every value the size of the largest variant, indexable and
// overwritable) or *variable* (each value exactly its variant's size, text
// included, walked sequentially or reached through an index kept beside the
// array) -- and case functions, which are `match` written as an overload
// set: the virtual-call idiom.
//
// Build and run (from this directory):
//   goose -o shapes.c 05_shapes.goose && cl shapes.c && shapes

import math;

enum Shape {
    Circle { r: f64 },
    Rect { w: f64, h: f64 },
    Tri { a: f64, b: f64, c: f64 },
    Dot,
}

// Case functions: one overload per variant, called with the enum value.
// The call dispatches on the tag; the checker requires every variant to
// have exactly one applicable overload, so a new variant is a compile error
// here until it gets its case.
fn area(s: Shape.Circle) -> f64 { PI * s.r * s.r }
fn area(s: Shape.Rect) -> f64 { s.w * s.h }
fn area(s: Shape.Tri) -> f64 {
    let p = (s.a + s.b + s.c) / 2.0;     // Heron's formula
    sqrt(p * (p - s.a) * (p - s.b) * (p - s.c))
}
fn area(s: Shape.Dot) -> f64 { 0.0 }

fn sides(s: Shape.Circle) -> i64 { 0 }
fn sides(s: Shape.Rect) -> i64 { 4 }
fn sides(s: Shape.Tri) -> i64 { 3 }
fn sides(s: Shape.Dot) -> i64 { 0 }

// `match` is the inline form; `_` covers the rest.
fn name(s: Shape) -> u8[:] {
    match s {
        Circle c => "circle",
        Rect r => if r.w == r.h { "square" } else { "rectangle" },
        _ => "other",
    }
}

// Scaling a fixed-mode value in place: the whole value is replaced, which
// is allowed exactly because nothing can point inside a fixed-mode payload.
fn scaled(s: Shape, k: f64) -> Shape {
    match s {
        Circle c => Shape.Circle { r: c.r * k },
        Rect r => Shape.Rect { w: r.w * k, h: r.h * k },
        Tri t => Shape.Tri { a: t.a * k, b: t.b * k, c: t.c * k },
        Dot => Shape.Dot,
    }
}

// A drawing's marks: shapes, and labels with text. A Label holds its text
// itself, a varint length and then the bytes, right after the tag, so the
// marks in a variable-mode array are one flat run of bytes, strings
// included, with no pointer anywhere. (An enum in Rust or C++ has one fixed
// size: its string variant would be a fixed-size string object, and every
// element would be as big as the largest variant.) The text makes Mark a
// variable-size type, so it is only ever stored in variable mode.
enum Mark {
    Circle { r: f64 },
    Rect { w: f64, h: f64 },
    Label { text: u8[varint] },
    Dot,
}

// Packed marks have no fixed stride, so `marks` cannot be indexed, but an
// index beside it can be: each entry is one mark's offset from the base of
// `marks` (`in marks`, 4 bytes), and loading one is an add. The array such
// offsets are measured from is a global; a local array can be indexed the
// same way with plain 8-byte references, `(Mark..&)[>..]`.
var marks: Mark..[>..] = [];
var mark_at: (Mark..&<u32 in marks>)[>..] = [];

fn main() {
    // --- fixed mode: an ordinary array of enum values ----------------------
    // Each element is 1 tag byte + the largest payload (three f64s): 25 bytes,
    // the layout a Rust enum or a C++ variant also has; `match` reads the
    // payload in place, with no boxing and no virtual call.
    var shapes: Shape[>..] = [];
    shapes.push(Shape.Circle { r: 1.0 });
    shapes.push(Shape.Rect { w: 2.0, h: 3.0 });
    shapes.push(Shape.Tri { a: 3.0, b: 4.0, c: 5.0 });
    shapes.push(Shape.Dot);
    shapes.push(Shape.Rect { w: 2.0, h: 2.0 });
    var total = 0.0;
    for s in shapes {
        print(name(s), " ", s, " area ", area(s), " sides ", sides(s));
        total += area(s);
    }
    print("total area ", total);
    // Indexable and replaceable: element 3 becomes a different variant.
    shapes[3] = Shape.Circle { r: 0.5 };
    for &s in shapes { s = scaled(s, 2.0); }
    print(shapes);

    // --- variable mode: each element as big as its own variant -------------
    // A Dot is 1 byte, a Circle 9, a Rect 17: half the memory of the fixed
    // array for this mix. The price is sequential access: no indexing (short
    // of keeping an index, below), and an element can never change variant,
    // since its neighbors are packed right behind it. In exchange, references
    // into a payload are allowed.
    var packed: Shape..[>..] = [];
    packed.push(Shape.Circle { r: 1.0 });
    packed.push(Shape.Dot);
    packed.push(Shape.Rect { w: 2.0, h: 3.0 });
    packed.push(Shape.Dot);
    packed.push(Shape.Tri { a: 3.0, b: 4.0, c: 5.0 });
    var dots = 0;
    var widest = 0.0;
    for s in packed {                    // variable-size elements bind by reference
        match s {
            Dot => block { dots++; },
            Rect &r => block { r.w += 1.0; widest = r.w; },   // edits the payload in place, inside the array
            _ => block {},
        }
        print(name(s), " ", area(s));
    }
    print(dots, " dots, widest rect now ", widest);
    print(packed);

    // --- variable mode with an index: random access ------------------------
    // push builds each mark in place at the end of `marks` and returns a
    // reference to it, which the index keeps as that mark's offset.
    mark_at.push(marks.push(Mark.Label { text: "hello, goose" }));
    mark_at.push(marks.push(Mark.Circle { r: 1.5 }));
    mark_at.push(marks.push(Mark.Dot));
    mark_at.push(marks.push(Mark.Rect { w: 2.0, h: 3.0 }));
    mark_at.push(marks.push(Mark.Label { text: "x" }));
    for i in [3, 0, 4, 1, 2] {           // any order; each lookup is one add
        match mark_at[i] {
            Circle c => print(i, ": circle ", c.r),
            Rect r => print(i, ": rect ", r.w, " by ", r.h),
            Label l => print(i, ": label \"", l.text, "\""),
            Dot => print(i, ": dot"),
        }
    }
    // A Label is its tag, a 1-byte length and its text: 14 + 9 + 1 + 17 + 3.
    print(marks.len, " marks in ", bytes_of(marks).len, " bytes");
}
