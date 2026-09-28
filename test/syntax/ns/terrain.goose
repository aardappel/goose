// Namespace terrain: integer constants match_constants.goose names in its
// patterns qualified, and this file's own match names unqualified.
namespace terrain;

let GATE = 7;
let WALL: u8 = 9;
let LO = 20;
let HI = LO + 10;
let FOUR = 4;

fn kind(t: i64) -> i64 { match t { GATE => 1, WALL => 2, LO..HI => 3, _ => 0 } }
