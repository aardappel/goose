// Named constants in integer match patterns (§8.1): let and const globals,
// bare or qualified (terrain::, ::), alone, as range bounds and negated,
// beside literals, and at the top of their type. A bare name is a constant
// or a variant as the scrutinee's type says, so a generic function's match
// takes it either way per specialization, and an enum's variant may share
// a global constant's name. A constant scrutinee selects its arm at -O2,
// and BCE takes a named arm's value and bounds as facts.
import ns.terrain;

let GROUND = 0;
let WATER = GROUND + 1;
const SAND: u8 = 2;
let THREE = 3;
let GATE = 70;
let NEG = -3;
let TOP: u64 = 18446744073709551615;

enum Kind { GROUND, WATER, LAVA }

fn name(t: i64) -> u8[] {
    match t {
        GROUND => "ground",
        WATER => "water",
        SAND => "sand",
        terrain::GATE => "terrain gate",
        ::GATE => "gate",
        -NEG..10 => "three to nine",
        terrain::LO..terrain::HI => "terrain range",
        NEG => "neg",
        -10..NEG => "below neg",
        _ => "other",
    }
}

fn kind_name(k: Kind) -> u8[] { match k { GROUND => "ground", WATER => "water", LAVA => "lava" } }

fn either(x) -> i64 { match x { GROUND => 1, WATER => 2, _ => 0 } }

fn top(x: u64) -> i64 { match x { TOP => 1, 0..GATE => 2, _ => 3 } }

fn small(x: u8) -> i64 { match x { SAND => 1, GROUND..SAND => 2, _ => 0 } }

fn at(k: i64) -> i64 {
    let a: i64[4] = [3, 5, 7, 9];
    match k {
        THREE => a[k],                        // bce:elide
        GROUND..terrain::FOUR => a[k],        // bce:elide
        _ => 0,
    }
}

fn main() {
    let ts: i64[14] = [0, 1, 2, 7, 70, 3, 9, 20, 29, 30, -3, -10, -4, 100];
    for t in ts { print(t, " ", name(t)); }
    print(name(7), " ", name(-4), " ", match WATER { GROUND => "g", WATER => "w", _ => "?" });
                                                    // terrain gate below neg w
    print(kind_name(Kind.GROUND), " ", kind_name(Kind.LAVA));       // ground lava
    print(either(0), either(Kind.WATER), either(1), either(Kind.LAVA));   // 1220
    print(terrain::kind(7), terrain::kind(9), terrain::kind(25), terrain::kind(1));  // 1230
    let us: u64[3] = [18446744073709551615, 5, 70];
    var r = 0;
    for u in us { r = r * 10 + top(u); }
    print(r, " ", top(18446744073709551615), top(5), top(70));      // 123 123
    let bs: u8[4] = [2, 1, 0, 3];
    r = 0;
    for b in bs { r = r * 10 + small(b); }
    print(r);                                                       // 1220
    let ks: i64[5] = [3, 0, 2, 4, -1];
    r = 0;
    for k in ks { r = r * 10 + at(k); }
    print(r);                                                       // 93700
}
