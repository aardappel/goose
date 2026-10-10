// runtime-debug: fixed values above the C backend's native-value threshold
// keep their value semantics on data stacks. With the old lowering the
// save/reset/load sequence alone reserved 1,280,144 bytes in MSVC's main
// frame after Goose inlining, overflowing Windows' default 1 MiB stack.
struct Game { blocks: u8[256000], seed: i64 }
var game = Game { .. };

fn reset(seed: i64) { game = Game { seed: seed, .. }; }
fn save() -> u8[>..] { return to_bytes([game]); }
fn load(bytes: const u8[:]) {
    let saved, valid = from_bytes<Game[>..]>(bytes);
    assert(valid && saved.len == 1);
    game = saved[0];
}

// Each recursive frame owns its argument. Captures see that private copy,
// not the caller's value, and a returned value survives the frame's return.
// Inside a recursive cycle a large fixed value lives on the native stack,
// not a data stack, so that no stack is held across the call into the cycle
// (§7.8, §10.3): the recursion's values stay above the threshold but well
// inside a 1 MiB native stack across its four activations.
struct Piece { blocks: u8[20000], seed: i64 }
recursive fn alter(g: Piece, n: i64) -> Piece {
    fn stamp() { g.blocks[0] += 1; g.seed += 1; }
    stamp();
    if n == 0 { return g; }
    let inner = alter(g, n - 1);
    assert(g.seed + n == inner.seed);
    return inner;
}
fn pair(g: Game) -> Game, i64 { return g, g.seed; }
fn forward(g: Game) -> Game, i64 { return pair(g); }
fn second(g: Game, ignored: i64) -> Game { return g; }
fn change() -> i64 { game.seed = 99; return 0; }

fn calls() {
    reset(7);
    let piece = Piece { seed: 7, .. };
    let result = alter(piece, 3);
    assert(result.seed == 11 && result.blocks[0] == 4);
    assert(piece.seed == 7 && piece.blocks[0] == 0);
    let copied, seed = forward(game);
    assert(copied == game && seed == 7);
    let before = second(game, change());
    assert(before.seed == 7 && game.seed == 99);
    let selected = if seed == 7 { copied } else { game };
    assert(selected.seed == 7);
}

// A large return can unwind through other large locals. Repeat exits often
// enough that failing to restore a 256 KB data-stack watermark exhausts its
// default 256 MiB reservation.
fn bail(n: i64) {
    let scratch = Game { seed: n, .. };
    if n > 0 { return scratch from early; }
}
fn early(n: i64) -> Game {
    let live = Game { seed: -1, .. };
    bail(n);
    return live;
}
fn scopes() {
    for i in 1100 {
        let g = Game { seed: i, .. };
        if i % 2 == 0 { continue; }
        assert(g.seed == i);
    }
    for i in 1100 {
        let result = early(i);
        assert(result.seed == (if i == 0 { -1 } else { i }));
        while true {
            let g = Game { seed: i, .. };
            assert(g.seed == i);
            break;
        }
    }
}

enum Item { Empty, World { value: Game } }
fn weight(x: Item.Empty, other: Game) -> i64 { return other.seed; }
fn weight(x: Item.World, other: Game) -> i64 { return x.value.seed + other.seed; }
fn enums() {
    let item: Item = Item.World { value: Game { seed: 4, .. } };
    let n = match item { Empty => 0, World w => w.value.seed };
    assert(n == 4);
    assert(weight(item, Game { seed: 2, .. }) == 6);
    assert(weight(Item.Empty, Game { seed: 3, .. }) == 3);
    let packed: Item.. = item;
    let unpacked: Item = packed;
    assert(unpacked == item);
    assert(weight(packed, Game { seed: 5, .. }) == 9);
    let byref = match &packed { Empty => 0, World w => w.value.seed };
    assert(byref == 4);
    assert(str(item) == "World { G4 }");
}

fn format(out: u8[>..]&, g: Game) { format(out, "G", g.seed); }
fn arrays() {
    let rows = [Game { seed: 1, .. }, Game { seed: 2, .. }];
    assert(str(rows) == "[G1, G2]");
    var all: Game[>..<] = [];
    all.append(rows);
    let last = all.pop();
    assert(last.seed == 2 && all.len == 1);
    let bounded: Game[..4] = rows;
    assert(bounded.len == 2 && bounded[1].seed == 2);
    var total = 0;
    for g in rows { total += g.seed; }
    assert(total == 3);
}

struct Initialized {
    blocks: u8[8192] = [3; 8192],
    seed: i64 = 5,
}
struct Link { blocks: u8[8192], next: Link&<u32>? }
fn defaults_and_links() {
    let initialized = default<Initialized[2]>();
    assert(initialized[1].blocks[8191] == 3 && initialized[0].seed == 5);
    reusable[] var pool: Initialized[>..] = [];
    let run = pool.alloc_slice(3);
    assert(run[2].blocks[8191] == 3);
    // This fixed root must be built at its final address, including every
    // self-relative link. The change affects storage, not relative offsets.
    var links = [Link { next: null, .. }; 2];
    links[0].next .= &links[1];
    links[1].blocks[8191] = 23;
    let target = links[0].next;
    guard target else { assert(false); return; }
    assert(target.blocks[8191] == 23);
    let absent: Game&? = null;
    assert(str(absent) == "null");
}

fn edges(data: const u8[:]) -> i64 { return data[0] + data[data.len - 1]; }
recursive fn slice_literal(n: i64) -> i64 {
    // The checked argument is only a slice, but its literal has a large
    // fixed backing array. This function still needs its own stack indices.
    let v = edges([9; 8192]);
    if n == 0 { return v; }
    return v + slice_literal(n - 1);
}

fn main() {
    for i in 4 {
        reset(i);
        game.blocks[0] = 19;
        game.blocks[255999] = 23;
        let bytes = save();
        reset(99);
        load(bytes);
        assert(game.seed == i);
        assert(game.blocks[0] == 19 && game.blocks[255999] == 23);
    }
    calls();
    scopes();
    enums();
    arrays();
    defaults_and_links();
    assert(slice_literal(3) == 72);
    print("large fixed values ok");
}
