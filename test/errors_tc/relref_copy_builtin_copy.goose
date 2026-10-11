// error: copying a value of type Ring, which contains self-relative references, is not supported; construct it in place
// copy(x) makes the copy whose links would be measured from it, even where
// only a field of it is read (§4.1, §3.9).
struct Ring { next: Ring&<u32>, v: i64 }
fn ring(rs: Ring[>..]&) {
    let a .= rs.push(Ring { next: self, v: 1 });
    rs.push(Ring { next: a, v: 2 });
}
fn main() {
    var rs: Ring[>..] = [];
    ring(rs);
    print(copy(rs[1]).next.v);
}
