// The imported side of ns_alias_file_local.goose: it declares geo::Pt, then
// names it through g::, the importer's alias, which does not hold here.
struct geo::Pt { x: i64 }
fn norm(p: g::Pt) -> i64 { p.x }
