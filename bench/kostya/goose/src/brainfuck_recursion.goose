// Brainfuck::Recursion: the program parsed into a tree of ops, every loop
// holding its body, and run by walking that tree, recursing into each loop.

namespace brainfuck_recursion;

import std;
import bench;

enum Op { Dec, Inc, Prev, Next, Print, Loop { body: Op[:] } }

// The tape grows to the right as the head reaches its end.
struct Tape { pos: i64, tape: u8[>..] }

fn new_tape() -> Tape { Tape { pos: 0, tape: [0] } }

fn current_cell(t: Tape&) -> u8 { t.tape[t.pos] }

fn inc(t: Tape&) { t.tape[t.pos] += 1; }

fn dec(t: Tape&) { t.tape[t.pos] -= 1; }

fn prev(t: Tape&) {
    if t.pos > 0 { t.pos--; }
}

fn next(t: Tape&) {
    t.pos++;
    // One cell more, as resize(pos + 1, 0) would: a struct's growable field
    // grows by push.
    if t.pos >= t.tape.len { t.tape.push(0); }
}

struct Program { ops: Op[:] }

// The program's top level and every loop body are each a run of ops in
// `op_pool`, which the caller owns, added once the run's closing bracket is
// parsed. A recursive function cannot keep a growable array of its own
// across its recursive calls, so `parse` collects the ops of the bodies
// still being parsed on the shared `pending` stack.
fn new_program(code: const u8[:], op_pool: Op[>..]&, pending: Op[>..<]&) -> Program {
    var pos = 0;
    return Program { ops: parse(code, pos, op_pool, pending) };
}

recursive fn parse(code: const u8[:], pos: i64&, op_pool: Op[>..]&, pending: Op[>..<]&) -> Op[:] {
    let mark = pending.len;
    while pos < code.len {
        let byte = code[pos];
        pos++;
        match byte {
            '-' => { pending.push(Op.Dec); },
            '+' => { pending.push(Op.Inc); },
            '<' => { pending.push(Op.Prev); },
            '>' => { pending.push(Op.Next); },
            '.' => { pending.push(Op.Print); },
            '[' => { pending.push(Op.Loop { body: parse(code, pos, op_pool, pending) }); },
            ']' => { break; },
            _ => { continue; },
        }
    }
    let start = op_pool.len;
    op_pool.append(pending[mark..]);
    pending.resize(mark);
    return op_pool[start..];
}

fn run(p: Program&) -> i64 {
    var tape = new_tape();
    var result: i64 = 0;
    p.execute(p.ops, tape, result);
    return result;
}

// `result` wraps on overflow, as signed arithmetic does in a release build.
recursive fn execute(p: Program&, program: Op[:], tape: Tape&, result: i64&) {
    for op in program {
        match op {
            Dec => { tape.dec(); },
            Inc => { tape.inc(); },
            Prev => { tape.prev(); },
            Next => { tape.next(); },
            Print => { result = (result << 2) + tape.current_cell(); },
            Loop inner => {
                while tape.current_cell() != 0 { p.execute(inner.body, tape, result); }
            },
        }
    }
}

fn run_text(text: const u8[:]) -> i64 {
    var op_pool: Op[>..] = [];
    var pending: Op[>..<] = [];
    let program = new_program(text, op_pool, pending);
    return program.run();
}

fn run(b: Bench&) -> u32 {
    let text = config_s(b.name, "program");
    let warmup_text = config_s(b.name, "warmup_program");
    var result_val: u32 = 0;
    fn warmup(i: i64) { run_text(warmup_text); }
    measure_with(b, warmup) { i => result_val += run_text(text) as! u32; };
    return result_val;
}
