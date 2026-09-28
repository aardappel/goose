// runtime-define: GS_DEBUG=1
// A checked `as` that would change its value aborts a debug build (§6.3),
// naming the value, the target type and the line of the cast itself, which
// is inside `level` even where -O2 inlines it into main.
fn level(x: i64) -> u8 {
    x as u8
}

fn main() {
    var xs: i64[>..] = [7, 255, 256];
    for x in xs {
        print(level(x));
    }
}
