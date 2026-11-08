// A builtin whose result is fixed-size -- the slice chunk, chunk_at or
// bytes_of gives, the fixed array of default -- builds nothing where the
// call's result is constructed: its value comes back, and the destination is
// built from it (CodeGen::EmitCall, ConstructCall). A function returning one
// as a variable array (i64[]) gives it as an element run where its result is
// appended to a grow-only array, initializes one or is assigned to one, and
// as a value with its length in front where a variable array holds it.

struct Rec { id: i64, items: i64[] }

fn part(xs: const i64[:], k: i64, n: i64) -> i64[] { xs.chunk(k, n) }

fn part_at(xs: const i64[:], k: i64) -> i64[] {
    let bounds: i64[4] = [0, 1, 1, 4];
    chunk_at(xs, bounds, k)
}

fn grown(xs: const i64[:]) -> i64[>..] { xs.chunk(1, 2) }

fn raw(x: i64[1]) -> u8[] { x.bytes_of() }

fn zeros() -> i64[] { default<i64[3]>() }

fn main() {
    let a: i64[5] = [1, 2, 3, 4, 5];
    var out: i64[>..] = [];
    out.append(part(a, 0, 2));
    out.append(part_at(a, 2));
    out.append(part_at(a, 1));
    out.append(zeros());
    print(out);
    var g: i64[>..] = part(a, 1, 2);
    print(g);
    g = part_at(a, 0);
    print(g, " ", grown(a));
    let v: i64[] = part(a, 2, 3);
    let r = Rec { id: 7, items: part_at(a, 2) };
    print(v, " ", r);
    let x: i64[1] = [258];
    var bytes: u8[>..] = [];
    bytes.append(raw(x));
    print(bytes.len, " ", bytes[0], " ", bytes[1]);
}
