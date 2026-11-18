// runtime-debug: zero is a valid non-optional self-relative offset.
struct Ring { next: Ring&<u8> }
struct Wide { value: i64, next: Wide&<u64>? }
var pool: i64[>..] = [7, 7];
struct PoolLinks {
    a: i64&<u8 in pool>?, b: i64&<u16 in pool>,
    c: i64&<u32 in pool>?, d: i64&<u64 in pool>?
}
struct VarPool { next: i64&<varint in pool>? }
struct Limited { refs: (i64&<u32 in pool>?)[..4] }
enum Fixed { Empty, Link { next: i64&<u32 in pool>? } }

fn main() {
    var rings: Ring[>..] = [];
    rings.push(Ring { next: self });
    rings.push(Ring { next: self });
    rings.push(Ring { next: rings[0] });
    print("zero targets ", rings[0].next .== rings[1].next, " ", rings[0].next .== rings[2].next);
    print("zero records ", rings[0] == rings[1], " ", rings[0] == rings[2], " ", rings[0] == rings[0]);
    var wide: Wide[>..] = [];
    wide.push(Wide { value: 2, next: null });
    wide.push(Wide { value: 2, next: wide[0] });
    wide.push(Wide { value: 2, next: wide[0] });
    wide.push(Wide { value: 2, next: wide[2] });
    print("wide ", wide[1] == wide[2], " ", wide[1] == wide[3]);

    let a = PoolLinks { a: pool[0], b: pool[0], c: pool[0], d: pool[0] };
    let b = PoolLinks { a: pool[0], b: pool[0], c: pool[0], d: pool[0] };
    let c = PoolLinks { a: pool[1], b: pool[0], c: pool[0], d: pool[0] };
    let d = PoolLinks { a: pool[0], b: pool[1], c: pool[0], d: pool[0] };
    let e = PoolLinks { a: pool[0], b: pool[0], c: pool[1], d: pool[0] };
    let f = PoolLinks { a: pool[0], b: pool[0], c: pool[0], d: pool[1] };
    let n = PoolLinks { a: null, b: pool[0], c: null, d: null };
    print("pool ", a == b, " ", a == c, " ", a == d, " ", a == e, " ", a == f, " ", a == n, " ", n == n);
    let va = VarPool { next: pool[0] };
    let vb = VarPool { next: pool[0] };
    let vc = VarPool { next: pool[1] };
    let vn = VarPool { next: null };
    print("varint pool ", va == vb, " ", va == vc, " ", va == vn, " ", vn == vn);
    let la = Limited { refs: [pool[0], null] };
    let lb = Limited { refs: [pool[0], null] };
    let lc = Limited { refs: [pool[1], null] };
    let ld = Limited { refs: [pool[0]] };
    print("limited ", la == lb, " ", la == lc, " ", la == ld);
    let ea: Fixed = Fixed.Link { next: pool[0] };
    let eb: Fixed = Fixed.Link { next: pool[0] };
    let ec: Fixed = Fixed.Link { next: pool[1] };
    let en: Fixed = Fixed.Empty;
    print("fixed adt ", ea == eb, " ", ea == ec, " ", ea == en);
}
