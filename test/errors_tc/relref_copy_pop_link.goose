// error: copying a value of type Ring, which contains self-relative references, is not supported: its links, read out of this copy, would be measured from the copy; read them where the value is stored
// pop gives the last element as a value, a copy of it: its link, read out
// of the copy, would point wherever the offset leads from there (§3.9).
struct Ring { next: Ring&<u32>, v: i64 }
fn ring(rs: Ring[>..]&) {
    let a .= rs.push(Ring { next: self, v: 1 });
    rs.push(Ring { next: a, v: 2 });
}
fn main() {
    var rs: Ring[>..] = [];
    ring(rs);
    print(rs.pop().next.v);
}
