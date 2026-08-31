// parse-only standalone (no main); imported and typechecked by parser_all.goose.
struct Vec2 { x: f64, y: f64 }

fn add2(a: Vec2, b: Vec2) -> Vec2 {
    Vec2 { x: a.x + b.x, y: a.y + b.y }
}
