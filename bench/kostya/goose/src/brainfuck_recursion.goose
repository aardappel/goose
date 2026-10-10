// Brainfuck::Recursion: the program parsed into a tree of ops, every loop
// holding its body, and run by walking that tree, recursing into each loop.

namespace brainfuck_recursion;

import std;
import bench;

enum Op { Dec, Inc, Prev, Next, Print, Loop { body: Op[:] } }

// The head on a tape of cells that grows to the right as the head reaches
// its end. `cell` is the cell at `pos`: a reference into a grow-only array
// stays valid as the array grows.
struct Tape { cells: u8[>..]&, pos: i64, cell: u8& }

// One case per op: a call with an Op dispatches on its tag, and every case
// but the loop's is small enough to inline there.
fn execute(op: Op.Dec, tape: Tape&, result: u32&) { tape.cell -= 1; }
fn execute(op: Op.Inc, tape: Tape&, result: u32&) { tape.cell += 1; }
fn execute(op: Op.Prev, tape: Tape&, result: u32&) {
    if tape.pos > 0 {
        tape.pos--;
        tape.cell .= tape.cells[tape.pos];
    }
}
fn execute(op: Op.Next, tape: Tape&, result: u32&) {
    tape.pos++;
    if tape.pos >= tape.cells.len { tape.cells.push(0); }
    tape.cell .= tape.cells[tape.pos];
}
fn execute(op: Op.Print, tape: Tape&, result: u32&) { result = (result << 2) + tape.cell; }
recursive fn execute(op: Op.Loop, tape: Tape&, result: u32&) {
    while tape.cell != 0 {
        for inner in op.body { execute(inner, tape, result); }
    }
}

fn run_program(text: const u8[:]) -> u32 {
    // The ops of every loop body lie together in `ops`, inner bodies before
    // the ones holding them; `pending` holds the ops of the bodies still
    // being parsed.
    var ops: Op[>..] = [];
    var pending: Op[>..<] = [];
    var at = 0;

    // Parses up to the `]` that closes the loop being parsed, or to the end
    // of the text, and returns that loop.
    recursive fn parse() -> Op {
        let mark = pending.len;
        while at < text.len {
            let c = text[at];
            at++;
            match c {
                '-' => { pending.push(Op.Dec); },
                '+' => { pending.push(Op.Inc); },
                '<' => { pending.push(Op.Prev); },
                '>' => { pending.push(Op.Next); },
                '.' => { pending.push(Op.Print); },
                '[' => { pending.push(parse()); },
                ']' => { break; },
                _ => {},
            }
        }
        let start = ops.len;
        ops.append(pending[mark..]);
        pending.resize(mark);
        return Op.Loop { body: ops[start..] };
    }

    var cells: u8[>..] = [0];
    var tape = Tape { cells: cells, pos: 0, cell: cells[0] };
    var result: u32 = 0;
    // The whole text parses as the body of a loop, which runs once.
    match parse() {
        Loop program => { for op in program.body { execute(op, tape, result); } },
        _ => {},
    }
    return result;
}

fn run(b: Bench&) -> u32 {
    let program = config_s(b.name, "program");
    let warmup_program = config_s(b.name, "warmup_program");
    var result: u32 = 0;
    fn warmup(i: i64) { run_program(warmup_program); }
    measure_with(b, warmup) { i => result += run_program(program); };
    return result;
}
