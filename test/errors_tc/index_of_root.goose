// index_of converts a reference into an index of the array it names, so the
// reference has to be one of that array's own elements (§3.3).
struct INode { key: i32 }
var ipool: INode[>..] = [];
var ispare: INode[>..] = [];
fn main() {
    ipool.push(INode { key: 1 });
    ispare.push(INode { key: 2 });
    print(ipool.index_of(&ispare[0]));
}
