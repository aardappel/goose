"""Controlled experiments on the saved slot_pool_users.py builder benchmarks.

Keeps the original binaries for reusable/slot_pool. Builds checked Goose
variants and narrowly edited C counterfactuals alongside them, without changing
the compiler, library or benchmarks. The unchecked variant is diagnostic only.
Generated-C substitutions deliberately assert the expected snapshot shape.
"""
import argparse
import json
from pathlib import Path
import random
import re
import shutil
import statistics
import subprocess

import slot_pool_users as users

ROOT = users.ROOT
WORKLOADS = {"lru_indices": ("lru_indices", []), "merkletrees": ("1", ["17"])}


def emit(exe, folder, stem, source):
    folder.mkdir(parents=True, exist_ok=True)
    path = folder / (stem + ".goose")
    path.write_text(source, encoding="utf-8", newline="\n")
    shutil.copyfile(ROOT / "bench/goose/rng.goose", folder / "rng.goose")
    cfile = path.with_suffix(".c")
    p = subprocess.run([str(exe), "-O2", "--standalone", "-o", str(cfile), str(path)],
                       capture_output=True, text=True, timeout=180)
    path.with_suffix(".log").write_text(p.stdout + p.stderr, encoding="utf-8")
    if p.returncode:
        raise RuntimeError(p.stdout + p.stderr)
    return cfile.read_text(encoding="utf-8")


def variants(name, stem, source, c, exe, out):
    result = {}
    if name == "lru_indices":
        old = "let ni = pool.items.index_of(pool.alloc_with() { Node { key: key as i32, val: (key % 1000) as i32 } });"
        assert source.count(old) == 1
        ref = source.replace(old,
            "let nn: Node& = pool.alloc_with() { Node { key: key as i32, val: (key % 1000) as i32 } };\n"
            "        let ni = pool.items.index_of(nn);")
        old = "link_front(pool.items[hi], pool.items[ni]);"
        assert ref.count(old) == 1
        ref = ref.replace(old, "link_front(pool.items[hi], nn);")
        result["keep_reference"] = emit(exe, out / "keep_reference", stem, ref)
        index, count = re.subn(r"pool.items.index_of\(pool.alloc_with\(\) (\{ Node \{[^\n]*\} \})\)",
                               r"pool.alloc_index_with() \1", source)
        assert count == 3
        index += """
fn alloc_index_with<T, F>(pool: slot_pool<T>&) -> i64 {
    if pool.free_slots.len != 0 {
        let i = pool.free_slots.pop();
        pool.items[i] .= F();
        return i;
    }
    let i = pool.items.len;
    pool.items.push(F());
    return i;
}
"""
        result["index_builder"] = emit(exe, out / "index_builder", stem, index)
    else:
        # This program always passes the one global pool to these helpers.
        # Direct field accesses expose that identity to native alias analysis.
        direct = c
        for field, num in [("items", 0), ("free_slots", 1)]:
            for member, replacement in [("hdr", f"GS_GL->gs_component_pool_g_{num}."),
                                         ("stk", f"GS_GL->gs_gstk_gs_component_pool_g_{num}.")]:
                old = f"(*pool_g_2).{field}_g.{member}->"
                assert old in direct
                direct = direct.replace(old, replacement)
        result["direct_components"] = direct
        assert source.count("pool.alloc_with()") == 2
        alloc = source.replace("pool.alloc_with()", "alloc_known_pool()") + """
fn alloc_known_pool<F>() -> Node& {
    if pool.free_slots.len != 0 {
        let i = pool.free_slots.pop();
        pool.items[i] .= F();
        return pool.items[i];
    }
    return pool.items.push(F());
}
"""
        result["known_alloc"] = emit(exe, out / "known_alloc", stem, alloc)
        assert alloc.count("pool.free(") == 1
        both = alloc.replace("pool.free(", "free_known_pool(") + """
fn free_known_pool(i: i64) {
    if i < 0 || i >= pool.items.len { abort("slot_pool.free: index out of bounds"); }
    pool.free_slots.push(i);
}
"""
        result["known_alloc_free"] = emit(exe, out / "known_alloc_free", stem, both)
    # Only the recycled-index lookup in alloc_with loses its check. In the
    # library the public freelist does not establish that this index is valid.
    unchecked, count = re.subn(
        r"GS_IDX\(\(int64_t\)\((t\d+)\), \((t\d+)\), gs_file[13], 32\)", r"(\1)", c)
    assert count == (3 if name == "lru_indices" else 2)
    result["unchecked_reuse"] = unchecked
    return result


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--input", type=Path, required=True, help="saved slot_pool_users.py output")
    ap.add_argument("--out", type=Path, required=True)
    ap.add_argument("--exe", required=True)
    ap.add_argument("--cpus", help="inherited by every benchmark process; unset leaves affinity unchanged")
    ap.add_argument("--reps", type=int, default=15)
    ap.add_argument("--warmups", type=int, default=3)
    ap.add_argument("--only", nargs="+", choices=WORKLOADS, default=list(WORKLOADS))
    args = ap.parse_args()
    if args.reps < 1 or args.warmups < 1:
        ap.error("positive reps and warmups required")
    exe = users.tc.find_goose(args.exe)
    compiler = users.tc.find_ccs().get("clang")
    if compiler is None:
        raise SystemExit("Clang is required")
    users.pin_cpus(args.cpus)
    source_dir, out = args.input.resolve(), args.out.resolve()
    if source_dir == out or source_dir in out.parents or out in source_dir.parents:
        ap.error("input and output directories must be separate")
    out.mkdir(parents=True, exist_ok=True)
    original = json.loads((source_dir / "results.json").read_text(encoding="utf-8"))
    for key, value in [("compiler_sha256", users.digest(exe.read_bytes())),
                       ("library_sha256", users.digest((ROOT / "stdlib/slot_pool.goose").read_bytes())),
                       ("compiler", compiler.desc)]:
        if original[key] != value:
            ap.error(f"{key} differs from the saved build; regenerate the input comparison")
    data = dict(original_build={k: v for k, v in original.items() if k != "runs"},
                cpus=args.cpus, reps=args.reps, warmups=args.warmups,
                seed=731, runs=[])
    for name in args.only:
        stem, argv = WORKLOADS[name]
        folder = out / name
        source = (source_dir / name / "slot_pool" / (stem + ".goose")).read_text(encoding="utf-8")
        c_sources = {b: (source_dir / name / b / (stem + ".c")).read_text(encoding="utf-8")
                     for b in ["reusable", "slot_pool"]}
        c_sources.update(variants(name, stem, source, c_sources["slot_pool"], exe, folder))
        binaries, hashes = {}, {}
        for variant, c in c_sources.items():
            dst = folder / variant
            dst.mkdir(parents=True, exist_ok=True)
            path = dst / (stem + ".c")
            path.write_text(c, encoding="utf-8", newline="\n")
            binary = dst / ("run" + users.tc.EXE_SUFFIX)
            if variant in ["reusable", "slot_pool"]:
                shutil.copyfile(source_dir / name / variant / binary.name, binary)
            else:
                ok, log = compiler.compile(path, binary, opt=2)
                (dst / "cc.log").write_text(log, encoding="utf-8")
                if not ok:
                    raise RuntimeError(log)
            binaries[variant] = binary
            hashes[variant] = dict(c_sha256=users.digest(c.encode()), exe_sha256=users.digest(binary.read_bytes()))
        timings = {k: [] for k in binaries}
        expected = None
        rng = random.Random(data["seed"])
        for rep in range(args.reps + args.warmups):
            order = list(binaries)
            rng.shuffle(order)
            for variant in order:
                elapsed, output = users.execute(binaries[variant], argv)
                if expected is None:
                    expected = output
                if output != expected:
                    raise RuntimeError(f"Output mismatch: {name}, {variant}")
                if rep >= args.warmups:
                    timings[variant].append(elapsed)
        medians = {k: statistics.median(v) for k, v in timings.items()}
        ratios = {k: v / medians["reusable"] for k, v in medians.items()}
        data["runs"].append(dict(workload=name, args=argv, hashes=hashes, output=expected,
                                 seconds=timings, median_seconds=medians, ratios=ratios))
        (out / "results.json").write_text(json.dumps(data, indent=2) + "\n", encoding="utf-8", newline="\n")
        print(name, {k: (round(v * 1000, 2), round(ratios[k], 3)) for k, v in medians.items()}, flush=True)


if __name__ == "__main__":
    main()
