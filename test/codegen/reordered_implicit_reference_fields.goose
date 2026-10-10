// runtime-debug: reordered implicit references retain their separate owners.
import std;
struct Owner {first:i64[>..]&,second:i64[>..]&}
enum Hold {Arrays {first:i64[>..]&,second:i64[>..]&}}
fn main() {
    var a:i64[>..]=[];var b:i64[>..]=[];
    var owner=Owner {second:b,first:a};
    owner.first.push(7);owner.second.push(9);
    assert(a.len==1&&b.len==1&&a[0]==7&&b[0]==9);
    var c:i64[>..]=[];var d:i64[>..]=[];
    var hold:Hold=Hold.Arrays {second:d,first:c};
    match hold {Arrays h=>{h.first.push(11);h.second.push(13);}};
    assert(c.len==1&&d.len==1&&c[0]==11&&d[0]==13);
    print("reordered implicit reference fields passed");
}
