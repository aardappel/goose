fn choose(r: i64&, flag: bool) -> i64& {
    if flag {
        let c: const i64& = r;
        return c;
    }
    return r;
}
fn main() {
    var n = 1;
    let r .= choose(n, true);
    r = 2;
}
