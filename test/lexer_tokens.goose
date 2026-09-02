// Token soup for `goose --tokens`; not a parseable program.
// Identifiers and keywords:
foo _bar baz123 _ import struct enum type fn thread_fn rec let var pad reusable
if else while for in loop block guard match return from break continue as
true false null self i64 f64 bool varint i8 i16 i32 i64 u8 u16 u32 u64 f32 f64
// Numbers:
0 42 1234567890 0x0 0xDEADbeef 0xffffffffffffffff 18446744073709551615
1.5 0.25 3.14159 1e10 2.5e-3 1E+2 0x1.8p3 0x1p-2 0xAp+1
// Ranges lex as i64 dotdot i64:
1..2 0..0x10
// Char literals:
'a' 'Z' '0' ' ' '\n' '\t' '\r' '\\' '\'' '\"' '\0' '\x41' '\xff'
// Strings:
"" "hello" "a\nb\tc" "quote:\" backslash:\\ tick:\'" "nul\0hex\x41\x0A"
// All punctuation, longest-munch checks:
( ) [ ] { } , ; : ? . .. .= + - * / % += -= *= /= %= &= |= ^= ++ -- = == != < >
<= >= << >> ! ~ & | ^ && || -> =>
>..< >.. [:] &<u8> /* block comment /* nested */ still comment */ end
