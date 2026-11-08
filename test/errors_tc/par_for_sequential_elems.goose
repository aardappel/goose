// error: a par for walks an array or slice of fixed-size elements
// §4.1: the loop cuts its sequence into runs, which an array of variable-size
// elements has no index for (§3.3).
fn main() {
    let names: u8[][] = ["ab", "cde"];
    var n = 0;
    par for s in names { n += s.len; }
    print(n);
}
