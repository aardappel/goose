// error: storing a reference rooted at local, which does not outlive the destination
// A chunk_at of a local stored in a global would outlive it (§9.2).
var saved: const i64[:];
fn keep() {
    let local:  i64[4] = [1, 2, 3, 4];
    let bounds: i64[2] = [1, 3];
    saved = local.chunk_at(bounds, 0);
}
fn main() { keep(); print(saved); }
