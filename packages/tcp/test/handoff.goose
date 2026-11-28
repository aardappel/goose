import std;
import tcp from "..";

extern fn test_tcp_handoff(socket: u64) -> bool;

fn main() {
    let listener, listening = tcp::listen("127.0.0.1", 0);
    assert(listening);
    let port, port_ok = tcp::local_port(listener);
    assert(port_ok);
    let client, connected = tcp::connect("127.0.0.1", port, 2000);
    assert(connected);
    let peer, accepted = tcp::accept(listener, 2000);
    assert(accepted);
    let native_client, took_client = tcp::take_native(client);
    let native_peer, took_peer = tcp::take_native(peer);
    assert(took_client && took_peer);
    assert(!tcp::close(client) && !tcp::close(peer));
    assert(test_tcp_handoff(native_client));
    assert(test_tcp_handoff(native_peer));
    assert(tcp::close(listener));
}
