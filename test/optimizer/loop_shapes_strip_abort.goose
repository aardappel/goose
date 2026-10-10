// runtime-debug: a strip-mined loop (optimize_loops.h) whose body aborts partway through a block of iterations prints and aborts exactly where the loop as written does.

fn lanes(data: u8[:], out: i64[:]) -> i64 {
    var acc: i64[4] = [0; 4];
    for b, i in data {
        acc[i % 4] += b;
        print(i);
        out[i] = acc[i % 4];
    }
    return acc[0] + acc[1] + acc[2] + acc[3];
}

fn main() {
    var d: u8[>..] = [];
    for k in 20 { d.push((k * 3) as! u8); }
    var o: i64[11] = [0; 11];
    print(lanes(d[..11], o));
    print(lanes(d, o));
}
