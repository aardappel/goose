"""Tests for Header-Backed Externs v2, Fixed-Length Slice Windows, Const Params, and target_has."""

import os
import subprocess
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
GOOSE = ROOT / "build" / ("goose.exe" if os.name == "nt" else "goose")


def run_cmd(args, cwd=None):
    env = os.environ.copy()
    if os.name == "nt" and "GOOSE_CC" not in env:
        env["GOOSE_CC"] = "gcc"
    return subprocess.run(args, check=False, cwd=cwd, env=env, text=True, capture_output=True)


def check_ok(src: str, extra_files=None, run_jit=True, run_build=True) -> str:
    with tempfile.TemporaryDirectory() as tmp:
        d = Path(tmp)
        if extra_files:
            for name, content in extra_files.items():
                p = d / name
                p.parent.mkdir(parents=True, exist_ok=True)
                p.write_text(content, encoding="utf-8")
        main_f = d / "main.goose"
        main_f.write_text(src, encoding="utf-8")

        r = run_cmd([str(GOOSE), "check", str(main_f)], cwd=d)
        assert r.returncode == 0, f"check failed:\n{r.stdout}\n{r.stderr}"

        # Ensure formatter roundtrips cleanly
        r_fmt = run_cmd([str(GOOSE), "format", str(main_f)], cwd=d)
        assert r_fmt.returncode == 0, f"format failed:\n{r_fmt.stderr}"
        r_chk2 = run_cmd([str(GOOSE), "check", str(main_f)], cwd=d)
        assert r_chk2.returncode == 0, f"check after format failed:\n{r_chk2.stderr}"

        out = ""
        if run_jit:
            rj = run_cmd([str(GOOSE), "run", str(main_f)], cwd=d)
            assert rj.returncode == 0, f"jit run failed:\n{rj.stdout}\n{rj.stderr}"
            out = rj.stdout

        if run_build:
            exe = d / ("prog.exe" if os.name == "nt" else "prog")
            rb = run_cmd([str(GOOSE), "build", str(main_f), "-o", str(exe)], cwd=d)
            assert rb.returncode == 0, f"build failed:\n{rb.stdout}\n{rb.stderr}"
            rexe = run_cmd([str(exe)], cwd=d)
            assert rexe.returncode == 0, (
                f"built exe failed:\n{rexe.stdout}\n{rexe.stderr}"
            )
            if run_jit:
                assert rexe.stdout == out, (
                    f"JIT vs AOT mismatch:\nJIT: {out!r}\nAOT: {rexe.stdout!r}"
                )
            out = rexe.stdout
        return out


def dump_c_ok(src: str) -> str:
    with tempfile.TemporaryDirectory() as tmp:
        d = Path(tmp)
        main_f = d / "main.goose"
        out_c = d / "out.c"
        main_f.write_text(src, encoding="utf-8")
        r = run_cmd([str(GOOSE), "-o", str(out_c), str(main_f)], cwd=d)
        assert r.returncode == 0, f"c codegen failed:\n{r.stdout}\n{r.stderr}"
        return out_c.read_text(encoding="utf-8")


def check_err(src: str, expected_substr: str):
    with tempfile.TemporaryDirectory() as tmp:
        d = Path(tmp)
        main_f = d / "main.goose"
        main_f.write_text(src, encoding="utf-8")
        r = run_cmd([str(GOOSE), "check", str(main_f)], cwd=d)
        combined = r.stdout + r.stderr
        assert r.returncode != 0, (
            f"expected failure containing {expected_substr!r}, but succeeded"
        )
        assert expected_substr in combined, (
            f"expected {expected_substr!r} in:\n{combined}"
        )


def test_slice_windows():
    src = r"""
fn get_ref(s: const u8[:]) -> const u8[4]& {
    return s[1..+4];
}

fn write_win(w: u8[4]&) {
    w[0] = 90;
    w[3] = 99;
}

fn sum_bce(s: const u8[:]) -> i64 {
    var acc: i64 = 0;
    var i = 0;
    while i + 16 <= s.len {
        let w = s[i..+16];
        acc += w[0] as i64 + w[15] as i64;
        i += 16;
    }
    while i + 4 <= s.len {
        let w = s[i..+4];
        acc += w[0] as i64 + w[3] as i64;
        i += 4;
    }
    return acc;
}

fn sum_limit_bce(s: const u8[:]) -> i64 {
    if s.len < 4 {
        return 0;
    }
    var acc: i64 = 0;
    let limit = s.len - 4;
    var i = 0;
    while i <= limit {
        let w = s[i..+4];
        acc += w[0] as i64;
        i += 4;
    }
    return acc;
}

fn main() {
    var arr: u8[8] = [10, 20, 30, 40, 50, 60, 70, 80];
    let s: u8[:] = arr;
    let w0: u8[4] = s[..+4];
    assert(w0[0] == 10 && w0[3] == 40);

    let r = get_ref(s);
    assert(r[0] == 20 && r[3] == 50);

    s[2..+4] = [1, 2, 3, 4];
    assert(arr[2] == 1 && arr[5] == 4);

    write_win(s[4..+4]);
    assert(arr[4] == 90 && arr[7] == 99);

    var big: u8[20] = [1; 20];
    print(sum_bce(big));
    print(sum_limit_bce(big));
}
"""
    out = check_ok(src)
    assert out.strip().splitlines() == ["4", "5"], f"unexpected output: {out!r}"

    # Verify BCE elides gs_bc_win in both sum_bce and sum_limit_bce
    c_src = dump_c_ok(
        r"""
fn sum_bce(s: const u8[:]) -> i64 {
    var acc: i64 = 0;
    var i = 0;
    while i + 16 <= s.len {
        let w = s[i..+16];
        acc += w[0] as i64;
        i += 16;
    }
    while i + 4 <= s.len {
        let w = s[i..+4];
        acc += w[0] as i64;
        i += 4;
    }
    return acc;
}
fn sum_limit_bce(s: const u8[:]) -> i64 {
    if s.len < 4 { return 0; }
    var acc: i64 = 0;
    let limit = s.len - 4;
    var i = 0;
    while i <= limit {
        let w = s[i..+4];
        acc += w[0] as i64;
        i += 4;
    }
    return acc;
}
fn main() {
    let b: u8[20] = [1; 20];
    print(sum_bce(b) + sum_limit_bce(b));
}
"""
    )
    # Count calls to gs_bc_win (excluding its definition in runtime header)
    call_count = c_src.count("gs_bc_win(") - 1
    assert call_count == 0, f"expected 0 gs_bc_win calls after BCE, got {call_count}"

    # Negative slice window diagnostics (Swarm repros poc5, slice_win_zero, slice_win_let_mutate)
    check_err(
        "fn main() { let s: u8[4] = [1, 2, 3, 4]; let _ = s[0..+0]; }\n",
        "positive compile-time integer constant",
    )
    check_err(
        "fn main() { let s: u8[4] = [1, 2, 3, 4]; let _ = s[0..+-2]; }\n",
        "positive compile-time integer constant",
    )
    check_err(
        "fn main() { let s: u8[4] = [1, 2, 3, 4]; let _ = s[0..+(256 as! u8)]; }\n",
        "positive compile-time integer constant",
    )
    check_err(
        "fn mut_win(w: i64[2]&) { w[0] = 99; }\n"
        "fn main() { let imm: const i64[4] = [1, 2, 3, 4]; mut_win(imm[0..+2]); }\n",
        "const",
    )
    check_err(
        "fn main() { let imm: const i64[4] = [1, 2, 3, 4]; imm[0..+2] = [9, 9]; }\n",
        "cannot assign",
    )


def test_header_externs_and_const_params():
    # 1. <string.h> with slices, refs, and optional refs (null)
    src_str = r"""
extern "<string.h>" {
    fn memcmp(a: const u8[:], b: const u8[:], n: u64) -> i32;
    fn memset(dst: u8[:], c: i32, n: u64) -> u64;
    fn memset_ref(dst: u8[4]&, c: i32, n: u64) -> u64 = "memset";
    fn memchr_opt(s: const u8?, c: i32, n: u64) -> u64 = "memchr";
}

fn main() {
    var buf: u8[8] = [0; 8];
    let _ = memset(buf, 65, 4);
    let _ = memset_ref(buf[4..+4], 66, 4);
    assert(memcmp(buf[0..4], "AAAA", 4) == 0);
    assert(memcmp(buf[4..8], "BBBB", 4) == 0);
    assert(memchr_opt(null, 65, 0) == 0);
    print("string_ok");
}
"""
    assert check_ok(src_str).strip() == "string_ok"

    # 2. <emmintrin.h> with `type m128i = u8[16] = "__m128i";`
    # Tests array literals, indexing, slice windows s[i..+16], mixed structs, root structs, globals
    src_simd = r"""
extern "<emmintrin.h>" target "sse2" {
    type m128i = u8[16] = "__m128i";
    fn mm_set1_epi8(b: i8) -> m128i = "_mm_set1_epi8";
    fn mm_cmpeq_epi8(a: m128i, b: m128i) -> m128i = "_mm_cmpeq_epi8";
    fn mm_movemask_epi8(a: m128i) -> i32 = "_mm_movemask_epi8";
    fn mm_xor_si128(a: m128i, b: m128i) -> m128i = "_mm_xor_si128";
}

struct Mixed {
    tag: u8,
    len: u64,
    vec: m128i,
}

root struct RootState {
    buf: u8[>..],
    mask: m128i,
}

let g_pad: u8 = 7;
let g_vec: m128i = [1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16];

fn touch_root(r: RootState&) {
    r.buf.push(r.mask[0]);
}

fn main() {
    var data: u8[32] = [0; 32];
    data[5] = 42;
    data[20] = 42;
    let s: const u8[:] = data;
    let needle = mm_set1_epi8(42);
    let m0 = mm_movemask_epi8(mm_cmpeq_epi8(s[0..+16], needle));
    let m1 = mm_movemask_epi8(mm_cmpeq_epi8(s[16..+16], needle));
    assert(m0 == (1 << 5));
    assert(m1 == (1 << 4));

    let z: m128i = [0; 16];
    let x = mm_xor_si128(g_vec, z);
    assert(x[0] == 1 && x[15] == 16 && g_pad == 7);

    let mx = Mixed { tag: 3, len: 99, vec: x };
    assert(mx.vec[7] == 8);

    var rs = RootState { buf: [], mask: [0x5A; 16] };
    touch_root(rs);
    assert(rs.buf[0] == 0x5A && rs.mask[15] == 0x5A);
    print("simd_ok");
}
"""
    assert check_ok(src_simd).strip() == "simd_ok"

    # 3. `const` parameters on extern fn, single-return/multi-return inline fn, &k, const for, extern const
    hdr_const = r"""
#ifndef TEST_CONST_HDR_H
#define TEST_CONST_HDR_H
#include <stdint.h>
#define MY_MAGIC_CONST 40
#define MY_SHIFT_ADD(x, imm) ((int64_t)((x) << (imm)) + (imm))
#endif
"""
    src_const = r"""
extern "test_const_hdr.h" {
    const MY_MAGIC: i64 = "MY_MAGIC_CONST";
    fn my_shift_add(x: i64, const imm: i32) -> i64 = "MY_SHIFT_ADD";
}

inline fn wrap_shift(x: i64, const k: i32) -> i64 {
    let r = &k;
    return my_shift_add(x, r);
}

inline fn pair_shift(x: i64, const k: i32) -> i64, i32 {
    return my_shift_add(x, k), k + 1;
}

fn main() {
    let m = MY_MAGIC + 2;
    assert(m == 42);
    let a = wrap_shift(3, 2);
    assert(a == 14);
    let b, c = pair_shift(5, 3);
    assert(b == 43 && c == 4);

    var sum: i64 = 0;
    const for <i_val> in [1, 2, 3] {
        sum += wrap_shift(10, i_val as i32);
    }
    assert(sum == (21 + 42 + 83));
    print("const_ok");
}
"""
    assert (
        check_ok(src_const, extra_files={"test_const_hdr.h": hdr_const}).strip()
        == "const_ok"
    )

    # 4. Subdirectory module with local header + JIT cache invalidation on header edit
    sub_hdr_v1 = "#define SUB_VAL() 111\n"
    sub_hdr_v2 = "#define SUB_VAL() 222\n"
    sub_goose = r"""
namespace sub;
extern "sub_hdr.h" {
    fn sub_val() -> i64 = "SUB_VAL";
}
fn get_sub() -> i64 {
    return sub_val();
}
"""
    main_sub = r"""
import sub from "./sub";
fn main() {
    print(sub::get_sub());
}
"""
    with tempfile.TemporaryDirectory() as tmp:
        d = Path(tmp)
        (d / "sub").mkdir()
        (d / "sub" / "sub_hdr.h").write_text(sub_hdr_v1, encoding="utf-8")
        (d / "sub" / "sub.goose").write_text(sub_goose, encoding="utf-8")
        (d / "main.goose").write_text(main_sub, encoding="utf-8")

        r1 = run_cmd([str(GOOSE), "run", str(d / "main.goose")], cwd=d)
        assert r1.returncode == 0 and r1.stdout.strip() == "111", (
            f"v1 failed: {r1.stdout} {r1.stderr}"
        )

        # Modify header and re-run in same directory to test thunk cache invalidation
        (d / "sub" / "sub_hdr.h").write_text(sub_hdr_v2, encoding="utf-8")
        r2 = run_cmd([str(GOOSE), "run", str(d / "main.goose")], cwd=d)
        assert r2.returncode == 0 and r2.stdout.strip() == "222", (
            f"v2 cache invalidation failed: {r2.stdout} {r2.stderr}"
        )

    # 5. Negative tests: const mutability through extern slice/ref, non-const arg to const param
    check_err(
        r"""
extern "<string.h>" {
    fn memset(dst: u8[:], c: i32, n: u64) -> u64;
}
fn main() {
    let imm: const u8[4] = [1, 2, 3, 4];
    let _ = memset(imm, 0, 4);
}
""",
        "const",
    )
    check_err(
        r"""
extern "<emmintrin.h>" {
    fn mm_slli_si128(a: u8[16], const imm: i32) -> u8[16] = "_mm_slli_si128";
}
fn run(k: i32) {
    let z: u8[16] = [0; 16];
    let _ = mm_slli_si128(z, k);
}
fn main() { run(2); }
""",
        "compile-time constant",
    )
    check_err(
        r"""
extern "<emmintrin.h>" {
    fn mm_slli_si128(a: u8[16], const imm: i32) -> u8[16] = "_mm_slli_si128";
}
fn mutate(x: i32&) { x = 9; }
fn main() {
    let k: i32 = 2;
    let z: u8[16] = [0; 16];
    let _ = mm_slli_si128(z, k);
    mutate(&k);
}
""",
        "writable reference",
    )


def test_target_has_and_propagation():
    # 1. Cross-architecture extern blocks (<wmmintrin.h> and <arm_neon.h>) in the same file
    #    with dead-branch pruning in `if target_has(...)` and automatic GS_TARGET propagation
    #    on helper functions, plus inliner guard so entry function is not polluted.
    src_dispatch = r"""
extern "<emmintrin.h>" target "sse2" {
    type m128i = u8[16] = "__m128i";
    fn mm_xor_si128(a: m128i, b: m128i) -> m128i = "_mm_xor_si128";
}

extern "<wmmintrin.h>" target "sse2,aes,pclmul" {
    fn mm_aesenc_si128(a: m128i, round_key: m128i) -> m128i = "_mm_aesenc_si128";
    fn mm_clmulepi64_si128(a: m128i, b: m128i, const imm: i32) -> m128i = "_mm_clmulepi64_si128";
}

extern "<arm_neon.h>" target "neon" {
    type uint8x16 = u8[16] = "uint8x16_t";
    fn veorq_u8(a: uint8x16, b: uint8x16) -> uint8x16;
    fn vceqq_u8(a: uint8x16, b: uint8x16) -> uint8x16;
}

// Helper has NO explicit target annotation: compiler propagates target "sse2,aes,pclmul"
// from mm_aesenc_si128 and mm_clmulepi64_si128 automatically.
fn aes_pclmul_helper(blk: m128i, key: m128i) -> m128i {
    let enc = mm_aesenc_si128(blk, key);
    let mul = mm_clmulepi64_si128(enc, key, 0x00);
    return mm_xor_si128(enc, mul);
}

// Mid-level helper calling aes_pclmul_helper without a target_has guard also inherits target_attr.
fn aes_pclmul_outer_helper(blk: m128i, key: m128i) -> m128i {
    return aes_pclmul_helper(blk, key);
}

fn neon_helper(blk: uint8x16, key: uint8x16) -> uint8x16 {
    return veorq_u8(blk, key);
}

fn dispatch_block(blk: u8[16], key: u8[16]) -> u8[16] {
    if target_has("x86_64,sse2,aes,pclmul") {
        return aes_pclmul_outer_helper(blk, key);
    } else if target_has("aarch64,neon") {
        // Inactive on x86_64: symbols inside dead branch or neon calls are pruned cleanly
        return neon_helper(blk, key);
    }
    var out: u8[16] = [0; 16];
    for i in 0..16 {
        out[i] = blk[i] ^ key[i];
    }
    return out;
}

fn main() {
    let b: u8[16] = [1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16];
    let k: u8[16] = [16, 15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1];
    let res = dispatch_block(b, k);
    // Verify deterministic output across JIT and AOT
    print(res[0], res[15]);
}
"""
    out = check_ok(src_dispatch).strip()
    assert len(out) > 0

    c_src = dump_c_ok(src_dispatch)
    # 1. Inactive <arm_neon.h> header is not emitted on x86_64
    assert "<arm_neon.h>" not in c_src, "expected <arm_neon.h> to be omitted on x86_64"
    # 2. Both aes_pclmul_helper and aes_pclmul_outer_helper receive propagated GS_TARGET
    assert 'GS_TARGET("sse2,aes,pclmul")' in c_src, (
        f"expected propagated GS_TARGET in generated C:\n{c_src}"
    )
    # 3. Runtime CPUID check gs_cpu_has is emitted in dispatch_block
    assert "gs_cpu_has(" in c_src, "expected runtime gs_cpu_has(...) check in dispatch_block"

    # 2. Dead-branch pruning allows untyped/foreign-arch code inside inactive target_has branch
    src_dead_branch = r"""
fn main() {
    if target_has("aarch64,neon") {
        nonexistent_aarch64_intrinsic(1, 2, 3);
    } else if target_has("wasm32") {
        nonexistent_wasm_intrinsic();
    } else if target_has("x86_64,sse2") {
        print("x86_64_baseline");
    }
}
"""
    assert check_ok(src_dead_branch).strip() == "x86_64_baseline"

    # 3. Negative tests: unknown tokens in target_has(...) and extern target "..."
    check_err(
        'fn main() { if target_has("bogus_isa") { print(1); } }\n',
        "unknown target feature",
    )
    check_err(
        'fn main() { if target_has("") { print(1); } }\n',
        "target_has",
    )
    check_err(
        'extern "<emmintrin.h>" target "bogus_target" {\n'
        '    fn mm_pause() = "_mm_pause";\n'
        "}\n"
        "fn main() {}\n",
        "unknown target feature",
    )


if __name__ == "__main__":
    test_slice_windows()
    test_header_externs_and_const_params()
    test_target_has_and_propagation()
    print("extern_headers.py: all tests passed!")
