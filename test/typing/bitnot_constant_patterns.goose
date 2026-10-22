// A named constant `~c` adapts to the width it is used at (§6.1) as a
// match pattern and as a const for entry too: ALL is 255 as a u8 pattern,
// 65535 as a u16 one and -1 as an i8 one, and a const for entry ALL is
// `~0` in the expansion, which a u8 takes as 255. One annotated with a type
// is ~c at that type's width.

let ALL = ~0;
let F_ROAD = 4;
let NOT_ROAD = ~F_ROAD;
let TOP: u8 = ~0;

fn kind8(x: u8) -> i64 { match x { ALL => 1, NOT_ROAD => 2, _ => 0 } }
fn kind16(x: u16) -> i64 { match x { ALL => 1, NOT_ROAD => 2, _ => 0 } }
fn kind32(x: u32) -> i64 { match x { ALL => 1, NOT_ROAD => 2, _ => 0 } }
fn kind64(x: u64) -> i64 { match x { ALL => 1, NOT_ROAD => 2, _ => 0 } }
fn kindi8(x: i8) -> i64 { match x { ALL => 1, NOT_ROAD => 2, -NOT_ROAD => 3, _ => 0 } }
fn top(x: u8) -> i64 { match x { TOP => 1, _ => 0 } }
fn listed(x: u8) -> i64 {
    match x { const for <C> in [ALL, NOT_ROAD] { C => 1, }, _ => 0 }
}

fn main() {
    print(kind8(255), " ", kind8(251), " ", kind8(3));                       // 1 2 0
    print(kind16(65535), " ", kind16(65531), " ", kind16(255));              // 1 2 0
    print(kind32(4294967295), " ", kind32(4294967291), " ", kind32(65535));  // 1 2 0
    print(kind64(18446744073709551615), " ", kind64(18446744073709551611), " ",
          kind64(4294967295));                                              // 1 2 0
    print(kindi8(-1), " ", kindi8(-5), " ", kindi8(5), " ", kindi8(0));      // 1 2 3 0
    print(top(255), " ", top(0));                                           // 1 0
    print(listed(255), " ", listed(251), " ", listed(0));                   // 1 1 0
    const for <C> in [ALL, NOT_ROAD, TOP] {
        let y: u8 = C;
        let w: u16 = C;
        print(y, " ", w);                       // 255 65535, 251 65531, 255 255
    }
}
