// error: constant integer expression expected for array size
// A limited array's length changes as it runs; only its capacity is fixed
// (§3.3).
let room: i64[..4] = [1];

fn main() {
    let a: i64[room.len] = [1];
    print(a);
}
