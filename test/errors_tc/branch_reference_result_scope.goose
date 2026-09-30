// error: storing a reference rooted at b, which does not outlive the destination
// A reference parameter binds each branch of a construct by reference, of a
// fixed-size value too (§4.1): the reference it returns is rooted at them.
fn same(v: i64&) -> i64& { v }
fn main() {
    var a = 1;
    var r: i64& = a;
    let c = a > 0;
    {
        var b = 2;
        var d = 3;
        r .= same(if c { b } else { d });
    }
    print(r);
}
