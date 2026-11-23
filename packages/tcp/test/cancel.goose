import std;
import os;
import tcp from "..";

thread_fn pending_accept(listener: i64) {
    qput(1);
    let c, accepted = tcp::accept(listener, -1);
    assert(!accepted && c == 0);
    qput(2);
}

thread_fn pending_recv(socket: i64) {
    qput(1);
    var buf: u8[>..] = [];
    assert(tcp::recv(socket, buf, -1) == -1 && buf.len == 0);
    qput(2);
}

fn started() {
    let stage, got = qget<i64>(2000);
    assert(got && stage == 1);
    sleep_ms(100);
}

fn finished(id: i64) {
    let stage, got = qget<i64>(2000);
    assert(got && stage == 2);
    assert(thread_wait(id));
}

fn main() {
    let l, listening = tcp::listen("127.0.0.1", 0);
    assert(listening);
    let none, accepted = tcp::accept(l, 25);
    assert(!accepted && none == 0);
    let accept_worker = thread_spawn(pending_accept, l);
    started();
    assert(tcp::close(l));
    assert(!tcp::close(l));
    finished(accept_worker);

    let listener, listener_ok = tcp::listen("127.0.0.1", 0);
    assert(listener_ok);
    let port, port_ok = tcp::local_port(listener);
    assert(port_ok);
    let client, connected = tcp::connect("127.0.0.1", port, 2000);
    assert(connected);
    let peer, peer_ok = tcp::accept(listener, 2000);
    assert(peer_ok);

    var buf: u8[>..] = [];
    assert(tcp::recv(peer, buf, 0) == -2 && buf.len == 0);
    assert(tcp::recv(peer, buf, 25) == -2 && buf.len == 0);
    let recv_worker = thread_spawn(pending_recv, peer);
    started();
    assert(tcp::close(peer));
    finished(recv_worker);
    assert(tcp::close(client));
    assert(tcp::close(listener));
}
