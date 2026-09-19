// Assigning to a runtime-capacity limited array `T[..]` (§4.4) from values
// that construct one without being one (§4.2): a slice field, variable and
// reference parameter, copies of a grow-only and a variable array, a fixed
// and a static-capacity limited array, a fixed-array element, a view into
// the destination itself, and an empty slice whose data is null -- directly
// and through a reference to the destination.
struct Ints { xs: const i32[:] }

fn from_field(n: Ints, l: i32[..]&) { l = n.xs; }
fn from_ref(r: const i32[:]&, l: i32[..]&) { l = r; }

fn main() {
    let arr: i32[] = [1, 2];
    let n = Ints { arr[..] };
    let e = default<Ints>();
    var l: i32[..] = [..4];

    l = n.xs;
    print(l, " ", l.cap);                       // [1, 2] 4
    let s: const i32[:] = arr[1..];
    l = s;
    print(l);                                   // [2]
    var g: i32[>..] = [3, 4, 5];
    l = copy(g);
    print(l);                                   // [3, 4, 5]
    let v: i32[] = [6, 7];
    l = copy(v);
    print(l);                                   // [6, 7]
    let f: i32[3] = [8, 9, 10];
    l = f;
    print(l);                                   // [8, 9, 10]
    var m: i32[..8] = [];
    m.push(11);
    l = m;
    print(l);                                   // [11]
    let grid: i32[2][>..] = [[12, 13], [14, 15]];
    l = grid[1];
    print(l);                                   // [14, 15]
    l = e.xs;
    print(l, " ", l.len);                       // [] 0

    // A view into the destination is copied before the destination changes.
    l = f;
    let tail = l[1..];
    l = tail;
    print(l);                                   // [9, 10]

    from_field(n, l);
    print(l);                                   // [1, 2]
    from_ref(s, l);
    print(l);                                   // [2]
    from_ref(e.xs, l);
    print(l.len, " ", l.cap);                   // 0 4
}
