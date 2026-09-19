// error: cannot call bump, which grows P: the run appended to P is still under construction
// Elements holding relative references, `in pool` ones included, are built
// where they stay, so one of them growing the array would land inside the
// run (§1.3(4)).
struct PN { key: i64, link: PN&<u32 in P> }
var P: PN[>..] = [];
fn bump() -> i64 { P.push(PN { key: 0, link: self }); return 1; }
fn main() {
    P.append([PN { key: 1, link: self }, PN { key: bump(), link: self }]);
    print(P.len);
}
