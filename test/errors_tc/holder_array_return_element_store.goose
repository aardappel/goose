// error: returning a reference rooted in b, which dies with this function
// An array of holders pushed with null links holds what is stored into its
// elements afterwards, through a reference push returned too (§9.2).
struct B { v: f64 }
struct N { v: f64, link: B? }
fn make(n: i64) -> N[>..] {
    var b = B { v: 2.0 };
    var xs: N[>..] = [];
    for i in n { xs.push(N { v: 1.0, link: null }); }
    let r: N& = xs.push(N { v: 1.0, link: null });
    r.link .= b;
    return xs;
}
fn main() { print(make(2).len); }
