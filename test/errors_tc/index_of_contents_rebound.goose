// error: .index_of needs a reference rooted at the array itself
// error: it was read out of h and may point into a or b
// h.r is rebound to an element of b where b is not empty, so what is read
// out of h may be an element of either array (§3.3, §9.5).
struct N { v: i64 }
struct H { r: N& }
fn main() {
    var a: N[>..] = [N { v: 1 }, N { v: 2 }];
    var b: N[>..] = [N { v: 3 }];
    var h = H { r: a[0] };
    if b.len > 0 { h.r .= b[0]; }
    let i = a.index_of(h.r);
    print(i);
}
