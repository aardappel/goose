// Namespace priority applies across all type declaration kinds (§11.1).
struct AliasOverStruct { value: i64 }
struct EnumOverStruct { value: i64 }
enum AliasOverEnum { Value { value: i64 } }
enum StructOverEnum { Value { value: i64 } }
type StructOverAlias = i64;
type EnumOverAlias = i64;
struct Fallback { value: i64 }

type local::AliasOverStruct = i64;
enum local::EnumOverStruct { Value { value: i64 } }
type local::AliasOverEnum = i64;
struct local::StructOverEnum { value: i64 }
struct local::StructOverAlias { value: i64 }
enum local::EnumOverAlias { Value { value: i64 } }

fn local::alias_struct(value: AliasOverStruct) -> AliasOverStruct { value }
fn local::enum_struct(value: EnumOverStruct) -> i64 {
    match value { Value v => v.value }
}
fn local::alias_enum(value: AliasOverEnum) -> AliasOverEnum { value }
fn local::struct_enum(value: StructOverEnum) -> i64 { value.value }
fn local::struct_alias(value: StructOverAlias) -> i64 { value.value }
fn local::enum_alias(value: EnumOverAlias) -> i64 {
    match value { Value v => v.value }
}
fn local::explicit_global(value: ::AliasOverStruct) -> i64 { value.value }
fn local::fallback(value: Fallback) -> i64 { value.value }

fn main() {
    print(local::alias_struct(1), " ",
          local::enum_struct(local::EnumOverStruct.Value { 2 }), " ",
          local::alias_enum(3), " ",
          local::struct_enum(local::StructOverEnum { 4 }), " ",
          local::struct_alias(local::StructOverAlias { 5 }), " ",
          local::enum_alias(local::EnumOverAlias.Value { 6 }), " ",
          local::explicit_global(AliasOverStruct { 7 }), " ",
          local::fallback(Fallback { 8 }));
}
