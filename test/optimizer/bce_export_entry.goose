// An export's C wrapper passes whatever its C caller does, so the export
// enters with nothing its Goose call sites proved; a plain function with
// the same sites gets their facts (`recursive` keeps it out of line, as the
// export is). The C host test/optimizer/bce_export_entry_host.c calls the
// wrapper past the end, which the kept check stops.
export "c_pick" fn pick(dist: i32[:], i: i64) -> i32 {
    return dist[i];                               // bce:keep (C callers pass unchecked arguments)
}

recursive fn pick_twin(dist: i32[:], i: i64) -> i32 {
    return dist[i];                               // bce:elide (every site passes i < dist.len)
}

fn main() {
    var d: i32[>..] = [];
    d.push(7);
    print(pick(d, 0), " ", pick_twin(d, 0));
}
