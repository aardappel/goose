// error: .index_of needs a reference rooted at the array itself
// error: it was read out of p and may point into xs or the caller's storage behind p
// One call passes find a holder whose reference may point into os or ns, so
// what is read out of p is not known to be an element of xs (§3.3, §9.5).
struct N { v: i64 }
struct H { r: N& }
fn find(p: H, xs: N[>..]&) -> i64 { return xs.index_of(p.r); }
fn main() {
    var ns: N[>..] = [N { v: 1 }, N { v: 2 }];
    var os: N[>..] = [N { v: 3 }];
    print(find(H { r: ns[1] }, ns));
    let c = ns.len > 5;
    print(find(H { r: if c { os[0] } else { ns[1] } }, ns));
}
