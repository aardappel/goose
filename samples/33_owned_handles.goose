// Owning handles: a table of connections whose buffers cannot leak.
//
// A slot_pool or slice_pool hands out slots and runs that the program has
// to give back exactly once. Declared `own`, such a handle carries that as
// an obligation the checker follows: every path must end in a release, a
// return, or a store into a field that owns it in turn. Reading through it
// borrows it as the plain reference or slice it is. `own` is checked and
// then erased: there is no destructor, and no code runs at a scope exit.
//
// Here a connection owns the run holding its unread input, and a session
// table owns the connections. Each "without this" note names the compile
// error that leaving the line out would be.
//
// Build and run (from this directory):
//   goose -o owned_handles.c 33_owned_handles.goose && cl owned_handles.c && owned_handles

import slice_pool;
import slot_pool;

struct Conn {
    id:    i64,
    input: own u8[:],      // received and not yet a whole line: a run of `bytes`
    used:  i64 = 0,
    lines: i64 = 0,
}

// An open connection's place in the table. The entry owns the connection:
// an `own Conn?` is a slot handle, or null once it has been taken out.
struct Session { id: i64, conn: own Conn? }

var bytes = slice_pool<u8> {};
var conns = slot_pool<Conn> {};
var sessions: Session[>..<] = [];

// take gives a slot as an `own Conn&`, which the entry's field takes over,
// as the connection's field takes over its run.
fn open(id: i64) {
    sessions.push(Session { id: id, conn: conns.take(Conn { id: id, input: bytes.take_slice(4) }) });
}

// Gives a connection back. release hands the element to its block, which
// must take every own field out of it. Without the block's line:
//   error: trusted fn release ends the claim of h on a Conn whose own field
//   input may still hold a value
fn close(c: own Conn&) {
    conns.release(c) { bytes.release(take(it.input)); };
}

// Ends session i. pop moves the last entry out of the table, replace swaps
// it into the place of the one leaving, and take empties that one's field.
// Without the last line:
//   error: c (own Conn?) is still held at the end of the function
fn end(i: i64) {
    var e = sessions.pop();
    if i < sessions.len { e = replace(sessions[i], e); }
    let c = take(e.conn);
    if c { close(c); }
}

// Appends to the unread input, growing its run when it is full. take
// empties the field and the run resize returns fills it again; assigning
// over a run the field still holds would be
//   error: assigning to c.input would drop the own u8[:] it holds
fn feed(c: Conn&, data: u8[:]) {
    if c.used + data.len > c.input.len {
        c.input = bytes.resize(take(c.input), (c.used + data.len) * 2);
    }
    for b, i in data { c.input[c.used + i] = b; }
    c.used += data.len;
}

// Answers every whole line of the input with the line reversed. The defer
// claims the scratch run: it is released on every way out, the early return
// included. Without the defer:
//   error: tmp (own u8[:]) is still held at this return
fn answer(c: Conn&, out: u8[>..]&) -> bool {
    let tmp = bytes.take_slice(c.used);
    defer bytes.release(tmp);
    var start = 0;
    for i in c.used {
        if c.input[i] != '\n' { continue; }
        let n = i - start;
        if n == 0 { return false; }                  // an empty line is a protocol error
        for k in n { tmp[k] = c.input[i - 1 - k]; }
        format(out, c.id, "> ", tmp[..n], "\n");
        c.lines += 1;
        start = i + 1;
    }
    // What follows the last newline waits for the next packet.
    for k in c.used - start { c.input[k] = c.input[start + k]; }
    c.used -= start;
    return true;
}

fn find(id: i64) -> i64 {
    for i in sessions.len { if sessions[i].id == id { return i; } }
    abort("no such session");
}

fn receive(id: i64, data: u8[:], out: u8[>..]&) {
    let i = find(id);
    let c = sessions[i].conn;          // a borrow: the entry keeps the handle
    assert(c);
    feed(c, data);
    if !answer(c, out) {
        format(out, id, "! protocol error, closing\n");
        end(i);
    }
}

fn main() {
    var out: u8[>..] = [];
    open(1);
    open(2);
    open(3);
    receive(1, "hello\nwor", out);
    receive(2, "a much longer line than the first run holds\n", out);
    receive(1, "ld\n", out);
    receive(3, "\n", out);             // closes 3
    receive(2, "abc\ndef\n", out);
    end(find(1));
    open(4);                           // takes a slot and a run given back above
    receive(4, "reused\n", out);
    end(find(2));
    end(find(4));
    print(out[..^1]);
    print("sessions ", sessions.len, ", connections live ", conns.items.len - conns.free_slots.len,
          " of ", conns.items.len, " slots");
}
