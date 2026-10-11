// error: copying a value of type Ring, which contains self-relative references, is not supported: its links, read out of this copy, would be measured from the copy; read them where the value is stored
// A construct's value is a copy of its branch's even where that names an
// element (§4.1): a link read out of it would measure from the copy (§3.9).
struct Ring { next: Ring&<u32>, v: i64 }
fn ring(rs: Ring[>..]&) {
    let a .= rs.push(Ring { next: self, v: 1 });
    rs.push(Ring { next: a, v: 2 });
}
fn main() {
    var rs: Ring[>..] = [];
    ring(rs);
    let c = rs.len > 1;
    print((if c { rs[1] } else { rs[0] }).next.v);
}
