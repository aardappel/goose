// runtime-debug: `~c` of a constant c >= 0 takes the integer type its
// destination or other operand has where c fits it, computed at that type
// (§6.1): ~4 is 251 as a u8 and -5 as an i8 or where nothing gives it a
// type. Named constants clear flags in narrow fields, a named constant `~c`
// adapts the same way, and `-~c` is a constant again.

let F_ROAD = 4;
let F_ALL = 7;
let NOT_ROAD = ~F_ROAD;
struct Cell { flags: u8 }

fn narrow(x: u8) -> u8 { x }

fn main() {
    let a: u8 = ~4;
    print(a);                                       // 251
    var c = Cell { flags: 7 };
    c.flags = c.flags & ~F_ROAD;
    print(c.flags);                                 // 3
    c.flags = 7;
    c.flags &= ~F_ROAD;
    print(c.flags);                                 // 3
    c.flags = 7;
    c.flags &= NOT_ROAD;
    print(c.flags);                                 // 3
    let x: u8 = 251;
    print(x == ~4, " ", ~F_ROAD == x);              // true true
    print(narrow(~F_ROAD), " ", narrow(NOT_ROAD));  // 251 251
    let b: i8 = ~4;
    let w: u16 = ~F_ALL;
    let m: u64 = ~0;
    let k: u32 = 1;
    print(b, " ", w, " ", m, " ", k | ~F_ROAD);     // -5 65528 18446744073709551615 4294967291
    let n = ~4;
    print(n, " ", NOT_ROAD, " ", -~4, " ", -NOT_ROAD);   // -5 -5 5 5
    let h: u8 = c.flags | ~(F_ALL & F_ROAD);
    print(h);                                       // 251
    print(~-1);                                     // 0
}
