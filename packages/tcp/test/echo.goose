// Threaded loopback echo with reads that can span several TCP chunks.
import std;
import tcp from "..";

fn read_ping(s: i64, buf: u8[>..]&) -> bool {
    while buf.len < 4 {
        if tcp::recv(s, buf) <= 0 { return false; }
    }
    return buf == "ping";
}

thread_fn client(port: i64) {
    let s, ok = tcp::connect("127.0.0.1", port);
    // The connect result goes first, so main never waits on accept for a
    // worker that could not connect.
    if ok { qput(1); } else { qput(0); return; }
    var buf: u8[>..] = [];
    let echoed = tcp::send(s, "pi") && tcp::send(s, "ng") && read_ping(s, buf);
    tcp::close(s);
    if echoed { qput(1); } else { qput(0); }
}

fn main() {
    let l, ok = tcp::listen("127.0.0.1", 0);
    print(ok);                                                  // true
    let port, pok = tcp::local_port(l);
    print(pok, " ", port > 0);                                  // true true
    assert(ok && pok);
    let id = thread_spawn(client, port);
    let connected, got = qget<i64>(10000);
    assert(got && connected == 1);
    let c, aok = tcp::accept(l);
    print(aok);                                                 // true
    assert(aok);
    var buf: u8[>..] = [];
    assert(read_ping(c, buf));
    print(buf.len, " ", buf);                                   // 4 ping
    let sent = tcp::send(c, buf);
    print(sent);                                                 // true
    assert(sent);
    print(tcp::recv(c, buf));                                   // 0
    print(tcp::close(c), " ", tcp::close(l));                   // true true
    let echoed, got2 = qget<i64>(10000);
    print(echoed, " ", got2);                                   // 1 true
    print(thread_wait(id));                                     // true
}
