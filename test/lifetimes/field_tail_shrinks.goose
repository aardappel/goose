// A struct's growable tail field shrinks, and resizes, where nothing still
// refers into the storage the struct lies in: named through a local, a
// reference parameter, a global, or a struct with a variable-size prefix
// (§5.1).
import std;

struct Tape { pos: i64, tape: u8[>..] }

fn next(t: Tape&) {
    t.pos++;
    if t.pos >= t.tape.len { t.tape.resize(t.pos + 1, 0); }
}

fn rewind(t: Tape&) {
    t.tape.clear();
    t.tape.push(0);
    t.pos = 0;
}

struct Program { commands: u8[], jumps: i64[>..] }

fn new_program(text: const u8[:]) -> Program {
    var program = Program { commands: text.filter() { c => c != ' ' }, jumps: [] };
    program.jumps.resize(program.commands.len, 0);
    return program;
}

var log = Tape { pos: 0, tape: [] };

fn trim_log(n: i64) {
    while log.tape.len > n { log.tape.pop(); }
}

fn main() {
    var t = Tape { pos: 0, tape: [0] };
    next(t);
    next(t);
    t.tape[2] = 7;
    print(t.tape.len, " ", t.tape[2]);
    t.tape.pop();
    print(t.tape.len);
    rewind(t);
    print(t.tape.len, " ", t.pos);

    var p = new_program("+ - [ ]");
    print(p.commands.len, " ", p.jumps.len);
    p.jumps.pop();
    p.jumps.resize(1);
    print(p.jumps.len);

    for i in 5 { log.tape.push(i as u8); }
    trim_log(2);
    print(log.tape.len);
}
