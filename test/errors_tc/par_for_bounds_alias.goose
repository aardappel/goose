// error: offs is partitioned by this par for
// R2 (§4.4): the bounds of chunk_at are read by every iteration, so they may
// not be the array the iterations write.
fn main() {
    var offs = [0, 1, 2, 3, 4];
    par for k in offs.len - 1 {
        let c = offs.chunk_at(offs, k);
        c[0] = 0;
    }
    print(offs);
}
