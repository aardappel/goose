import std;
struct Pair { x: i64, y: i64 }
struct Bag { tag: i64, items: i64[>..<] }
fn pair(a: i64, b: i64) { print(a, " ", b); }
extern fn fmod(a: f64, b: f64) -> f64;
fn main() {
    var x = 1;
    fn setx() -> i64 { x = 10; 2 }
    print(x + setx());
    x = 1;
    let px .= x;
    print(px + setx());
    x = 1;
    pair(x, setx());
    x = 1;
    var f = 1.0;
    fn setf() -> f64 { f = 10.0; 2.0 }
    print(fmod(f, setf()) == 1.0);
    var y = 2;
    var r .= x;
    fn change() -> i64& { r .= y; return y; }
    print(r .== change());
    r .= x;
    fn changewrite() -> i64 { r .= y; 9 }
    r = changewrite();
    print(x, " ", y);
    x = 1;
    x += setx();
    print(x);
    var a: i64[3] = [10, 20, 30];
    var i = 0;
    fn next() -> i64 { i = 1; 9 }
    a[i] = next();
    print(a);
    i = 0;
    fn upper() -> i64 { i = 1; 2 }
    let s = a[i..upper()];
    print(s.len);
    var b: i64[3] = [40, 50, 60];
    var ar .= a;
    fn index() -> i64 { ar .= b; 0 }
    print(ar[index()]);
    ar .= a;
    fn end() -> i64 { ar .= b; 2 }
    let old = ar[0..end()];
    print(old[0]);
    var limited_a: i64[..] = [1, 2];
    var limited_b: i64[..] = [3, 4];
    var limited_ref .= limited_a;
    fn change_limited() -> i64[..] { limited_ref .= limited_b; return [7, 8]; }
    limited_ref = change_limited();
    print(limited_a, " ", limited_b);
    var grow_a: i64[>..<] = [1, 2];
    var grow_b: i64[>..<] = [3, 4];
    var grow_ref .= grow_a;
    fn change_grow() -> i64[>..<] { grow_ref .= grow_b; return [7, 8, 9]; }
    grow_ref = change_grow();
    grow_a.push(5);
    grow_b.push(6);
    print(grow_a, " ", grow_b);
    var bag_a = Bag { tag: 1, items: [1, 2] };
    var bag_b = Bag { tag: 2, items: [3, 4] };
    var bag_ref .= bag_a;
    fn change_bag() -> Bag { bag_ref .= bag_b; return Bag { tag: 9, items: [7, 8, 9] }; }
    bag_ref = change_bag();
    bag_a.items.push(5);
    bag_b.items.push(6);
    print(bag_a.tag, bag_a.items, " ", bag_b.tag, bag_b.items);
}
