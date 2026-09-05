// A sudoku solver by backtracking. The board is 81 bytes and the "used"
// sets are bitmasks in u16s: three fixed arrays of nine, one bit per digit.
// The search always fills the cell with the fewest candidates first, which
// is what turns a hard puzzle from millions of guesses into hundreds.
// The whole state is one 135-byte struct passed by reference down the
// recursion, and nothing is allocated anywhere.
//
// Build and run (from this directory):
//   goose -o sudoku.c sudoku.goose && cl sudoku.c && sudoku

import std;
import os;

struct Board { cells: u8[81], rows: u16[9], cols: u16[9], boxes: u16[9] }

var guesses = 0;

fn bit(d: u8) -> u16 { (1 << d) as! u16 }
fn box_of(i: i64) -> i64 { (i / 27) * 3 + (i % 9) / 3 }

fn place(b: Board&, i: i64, d: u8) {
    b.cells[i] = d;
    b.rows[i / 9] |= bit(d);
    b.cols[i % 9] |= bit(d);
    b.boxes[box_of(i)] |= bit(d);
}

fn unplace(b: Board&, i: i64, d: u8) {
    b.cells[i] = 0;
    b.rows[i / 9] &= ~bit(d);
    b.cols[i % 9] &= ~bit(d);
    b.boxes[box_of(i)] &= ~bit(d);
}

// The digits still possible at cell i, as a mask.
fn candidates(b: Board&, i: i64) -> u16 {
    let used = b.rows[i / 9] | b.cols[i % 9] | b.boxes[box_of(i)];
    ~used & 0x3FE                        // bits 1..9
}

fn parse(text: u8[:]) -> Board {
    var b = Board { cells: [0; 81], rows: [0; 9], cols: [0; 9], boxes: [0; 9] };
    var i = 0;
    for c in text {
        if c >= '1' && c <= '9' { place(b, i, c - '0'); i++; }
        else if c == '.' || c == '0' { i++; }
    }
    assert(i == 81);
    return b;
}

recursive fn solve(b: Board&) -> bool {
    // Pick the empty cell with the fewest candidates.
    var best = -1;
    var bestn = 10;
    for i in 81 {
        guard b.cells[i] == 0 else { continue; }
        let n = popcount(candidates(b, i) as u64);
        if n < bestn { best = i; bestn = n; }
    }
    if best < 0 { return true; }         // no empty cell left: solved
    if bestn == 0 { return false; }      // a cell with no options: dead end
    let cands = candidates(b, best);
    for d in 1..10 {
        guard cands & bit(d as! u8) != 0 else { continue; }
        guesses++;
        place(b, best, d as! u8);
        if solve(b) { return true; }
        unplace(b, best, d as! u8);
    }
    return false;
}

fn show(b: Board&) {
    var line: u8[>..] = [];
    for r in 9 {
        line.clear();
        for c in 9 {
            let d = b.cells[r * 9 + c];
            line.push(if d == 0 { '.' } else { '0' + d });
            if c == 2 || c == 5 { line.append(" | "); } else if c < 8 { line.push(' '); }
        }
        print(line);
        if r == 2 || r == 5 { print("------+-------+------"); }
    }
}

fn run(name: u8[:], puzzle: u8[:]) {
    var b = parse(puzzle);
    print(name, ":");
    show(b);
    guesses = 0;
    let t0 = clock();
    let ok = solve(b);
    let ms = (clock() - t0) * 1000.0;
    print(if ok { "solved" } else { "no solution" }, " after ", guesses, " guesses");
    show(b);
    var note: u8[>..] = [];
    format(note, name, ": ");
    format_flt(note, ms, 2);
    format(note, " ms\n");
    write_stderr(note);
    print("");
}

fn main() {
    run("easy", "53..7....6..195....98....6.8...6...34..8.3..17...2...6.6....28....419..5....8..79");
    // Arto Inkala's 2012 "world's hardest sudoku".
    run("hard", "8..........36......7..9.2...5...7.......457.....1...3...1....68..85...1..9....4..");
    // Only 17 clues: the minimum for a unique solution.
    run("sparse", "...8.1..........435............7.8........1...2..3....6......75..34........2..6..");
}
