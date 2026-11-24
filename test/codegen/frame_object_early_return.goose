struct Packet { header: i64, bytes: u8[>..] }
struct Envelope { header: i64, packet: Packet }

fn local_header(fail: bool) -> i64 {
    if fail { return -1 from local_packet; }
    return 42;
}

fn local_packet(fail: bool) -> i64 {
    var p = Packet { header: local_header(fail), bytes: [] };
    p.bytes.push(1);
    return p.header + p.bytes.len;
}

fn nested_header(fail: bool) -> i64 {
    if fail { return -2 from nested_packet; }
    return 42;
}

fn nested_packet(fail: bool) -> i64 {
    var e = Envelope { header: 10, packet: Packet { header: nested_header(fail), bytes: [1] } };
    return e.header + e.packet.header + e.packet.bytes.len;
}

fn arg_header(fail: bool) -> i64 {
    if fail { return -3 from packet_argument; }
    return 42;
}

fn total(p: Packet) -> i64 { p.header + p.bytes.len }

fn packet_argument(fail: bool) -> i64 {
    return total(Packet { header: arg_header(fail), bytes: [1, 2] });
}

fn main() {
    var results: i64[>..] = [];
    results.push(local_packet(true));
    results.push(nested_packet(true));
    results.push(packet_argument(true));
    results.push(local_packet(false));
    results.push(nested_packet(false));
    results.push(packet_argument(false));
    print(results);
}
