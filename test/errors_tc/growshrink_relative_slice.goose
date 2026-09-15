// error: cannot pop arr while s
// A slice of relative references is a view of the cells that hold them, so it
// refers into the grow-shrink array it was taken from like any other slice
// (§5.2): the pop that gives those cells back is refused while it is live.
struct N { v: i64 = 0 }
type Link = N&<u32 in gpool>?;
var gpool: N[>..] = [];

fn main() {
    gpool.push(N { v: 7 });
    var arr: Link[>..<] = [];
    arr.push(gpool[0]);
    let s = arr[0..1];
    arr.pop();
    let x = s[0];
    if x { print(x.v); }
}
