// A function that grows a global through a reference parameter and then
// converts into an index or bound type over that global must check against
// the grown length, which it does not cache: the parameter may be the global.
// The recursive one stays a call at every optimization level.
var array: i64[>..] = [];
type Id = u32 in array;
type End = u32 upto array;

fn grow_and_convert(p: i64[>..]&, x: i64) -> Id {
    p.push(20);
    x as Id
}

recursive fn grow_and_convert_rec(p: i64[>..]&, x: i64, n: i64) -> Id {
    p.push(30);
    if n > 0 { return grow_and_convert_rec(p, x, n - 1); }
    x as Id
}

fn fill(p: i64[>..]&, n: i64) -> End {
    var e: End = 0 as End;
    for i in n { p.push(i); e = (i + 1) as End; }
    e
}

fn main() {
    array.push(10);
    let id = grow_and_convert(array, 1);
    print(array[id]);
    let id2 = grow_and_convert_rec(array, 3, 1);
    print(array[id2]);
    print(fill(array, 5), " ", array.len);
}
