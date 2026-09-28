// A range's or count's binder can take an integer type of its own (§6.5):
// `for i: i32 in a..b` checks each bound against i32, a constant adapting and
// a typed bound only widening, and counts at i32. The end may be one past the
// type's largest value, and the loop visits every value of the type without
// its counter overflowing. An array's index binder takes a type that holds
// every index the array can have. u8 and u16 arithmetic wrapping shows the
// binders' types.

fn take(x: i32) -> i32 { x }

fn main() {
    var n: i64 = 5;
    var m: i32 = 4;
    var s: u8 = 3;
    var total: i32 = 0;
    for i: i32 in 0..10 { total += take(i); }
    print(total);                                   // 45
    for i: i32 in 2..m { print(take(i)); }          // 2 3
    for i: i64 in m { print(i); }                   // 0 1 2 3

    // Every value of a narrow type, the end one past its largest.
    var count = 0;
    for i: u8 in 0..256 {
        count += 1;
        if i == 255 { print("last ", i); }          // last 255
    }
    print(count);                                   // 256
    var sum = 0;
    for i: u8 in 256 { sum += i; }
    print(sum);                                     // 32640
    for i: i8 in -128..128 { if i == 127 { print("i8 end ", i); } }   // i8 end 127
    for i: u16 in s { print(i * 40000); }           // 0 40000 14464
    for i: u8 in 250..256 { if i > 253 { print(i + 10); } }   // 8 9

    // Index binders of fixed-size and static-capacity arrays.
    var grid: i64[4] = [10, 20, 30, 40];
    for x, i: u8 in grid { print(i * 100, " ", x); }   // 0 10, 100 20, 200 30, 44 40
    var words: u8[..8] = [1, 2, 3];
    for w, k: u8 in words { print(k + 250, " ", w); }  // 250 1, 251 2, 252 3
    for &x, i: u64 in grid { x += 1; }
    print(grid);                                    // [11, 21, 31, 41]
    var dyn: i64[>..] = [7, 8];
    for x, i: u64 in dyn { print(i, " ", x); }      // 0 7, 1 8
}
