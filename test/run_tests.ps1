# Goose test runner: parses every test file, checks dump/reparse/dump
# roundtrips to identical output, typechecks files not marked `parse-only`
# on their first line, checks that error tests fail in the right phase, and
# (when MSVC is available) compiles and runs the generated C at -O0 and -O2,
# comparing the two runs and any blessed output in expected/<name>.out.
# expected/<name>.aborts marks tests whose run is expected to end in a
# runtime abort (nonzero exit) after printing their expected stdout.
param([string]$exe = "$PSScriptRoot\..\build\Debug\goose.exe", [switch]$nocgen)

$failures = 0
$utf8 = New-Object System.Text.UTF8Encoding($false)

& $exe --tokens "$PSScriptRoot\lexer_tokens.goose" | Out-Null
if ($LASTEXITCODE -ne 0) { Write-Host "FAIL lex lexer_tokens.goose"; $failures++ }
else { Write-Host "ok   lex lexer_tokens.goose" }

foreach ($f in Get-ChildItem "$PSScriptRoot\*.goose") {
    if ($f.Name -eq "lexer_tokens.goose") { continue }
    & $exe --parse $f.FullName | Out-Null
    if ($LASTEXITCODE -ne 0) {
        & $exe --parse $f.FullName
        Write-Host "FAIL parse $($f.Name)"
        $failures++
        continue
    }
    $d1 = (& $exe --dump $f.FullName) -join "`n"
    $tmp = "$PSScriptRoot\..\build\roundtrip.goose"
    [IO.File]::WriteAllText($tmp, $d1, $utf8)
    $d2 = (& $exe --dump $tmp) -join "`n"
    if ($LASTEXITCODE -ne 0) {
        & $exe --dump $tmp
        Write-Host "FAIL reparse-of-dump $($f.Name)"
        $failures++
    } elseif ($d1 -ne $d2) {
        Write-Host "FAIL roundtrip $($f.Name)"
        $failures++
    } else {
        Write-Host "ok   parse+roundtrip $($f.Name)"
    }
    $first = Get-Content $f.FullName -TotalCount 1
    if ($first -notmatch "parse-only") {
        & $exe --check $f.FullName | Out-Null
        if ($LASTEXITCODE -ne 0) {
            & $exe --check $f.FullName
            Write-Host "FAIL typecheck $($f.Name)"
            $failures++
        } else {
            Write-Host "ok   typecheck $($f.Name)"
        }
    }
}

# The optimizer runs at -O1 in every typecheck above; also exercise the other
# levels (and the --specs dump path) on the optimizer coverage file.
foreach ($lvl in "-O0", "-O1", "-O2") {
    & $exe $lvl --check --specs "$PSScriptRoot\optimize.goose" | Out-Null
    if ($LASTEXITCODE -ne 0) {
        & $exe $lvl --check "$PSScriptRoot\optimize.goose"
        Write-Host "FAIL optimize $lvl"
        $failures++
    } else {
        Write-Host "ok   optimize $lvl"
    }
}

# --- codegen: generate C, compile with MSVC, run, compare ------------------
$cc = $null
if (-not $nocgen) {
    $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
    if (Test-Path $vswhere) {
        $vsroot = & $vswhere -latest -property installationPath 2>$null
        if ($vsroot) {
            # Import the vcvars64 environment once.
            foreach ($l in (cmd /c "`"$vsroot\VC\Auxiliary\Build\vcvars64.bat`" >nul 2>&1 && set")) {
                if ($l -match '^([^=]+)=(.*)$') {
                    [Environment]::SetEnvironmentVariable($matches[1], $matches[2])
                }
            }
            $cc = "cl"
        }
    }
}
if (-not $cc) {
    Write-Host "skip codegen run tests (no MSVC found or -nocgen)"
} else {
    $gendir = "$PSScriptRoot\..\build\gen"
    New-Item -ItemType Directory -Force $gendir | Out-Null
    $allouts = @{}
    foreach ($f in Get-ChildItem "$PSScriptRoot\*.goose") {
        if ($f.Name -eq "lexer_tokens.goose") { continue }
        $first = Get-Content $f.FullName -TotalCount 1
        if ($first -match "parse-only") { continue }
        $name = [IO.Path]::GetFileNameWithoutExtension($f.Name)
        $aborts = Test-Path "$PSScriptRoot\expected\$name.aborts"
        $runs = @{}
        $bad = $false
        foreach ($ol in "0", "2") {
            $cfile = "$gendir\$name-O$ol.c"
            $efile = "$gendir\$name-O$ol.exe"
            & $exe "-O$ol" -o $cfile $f.FullName | Out-Null
            if ($LASTEXITCODE -ne 0) {
                & $exe "-O$ol" -o $cfile $f.FullName
                Write-Host "FAIL cgen -O$ol $($f.Name)"
                $failures++; $bad = $true; continue
            }
            & $cc /nologo /W3 $cfile "/Fe:$efile" "/Fo:$gendir\$name-O$ol.obj" > "$gendir\$name-O$ol.cl.log" 2>&1
            if ($LASTEXITCODE -ne 0) {
                Get-Content "$gendir\$name-O$ol.cl.log" | Select-Object -First 8
                Write-Host "FAIL cc -O$ol $($f.Name)"
                $failures++; $bad = $true; continue
            }
            $out = & $efile 2>"$gendir\$name-O$ol.err"
            $code = $LASTEXITCODE
            if (($aborts -and $code -eq 0) -or (-not $aborts -and $code -ne 0)) {
                Get-Content "$gendir\$name-O$ol.err" | Select-Object -First 3
                Write-Host "FAIL run -O$ol $($f.Name) (exit $code)"
                $failures++; $bad = $true; continue
            }
            $runs[$ol] = ($out -join "`n")
        }
        if ($bad) { continue }
        if ($runs["0"] -ne $runs["2"]) {
            Write-Host "FAIL cgen-output-differs-by-O $($f.Name)"
            $failures++
            continue
        }
        $expfile = "$PSScriptRoot\expected\$name.out"
        if (Test-Path $expfile) {
            $want = ((Get-Content $expfile) -join "`n")
            if ($runs["0"] -ne $want) {
                Write-Host "FAIL cgen-expected $($f.Name)"
                Write-Host "--- got:`n$($runs['0'])`n--- want:`n$want"
                $failures++
                continue
            }
        }
        $allouts[$name] = $runs["0"]
        Write-Host "ok   cgen+run $($f.Name)"
    }
    # One debug-checked build (-DGS_DEBUG=1: overflow and `as` range aborts,
    # §9.3) of the codegen coverage test; its output must not change.
    & $exe -O0 -o "$gendir\cgdbg.c" "$PSScriptRoot\codegen_exec.goose" | Out-Null
    & $cc /nologo /W3 /DGS_DEBUG=1 "$gendir\cgdbg.c" "/Fe:$gendir\cgdbg.exe" "/Fo:$gendir\cgdbg.obj" > "$gendir\cgdbg.cl.log" 2>&1
    $out = & "$gendir\cgdbg.exe" 2>$null
    $want = ((Get-Content "$PSScriptRoot\expected\codegen_exec.out") -join "`n")
    if ($LASTEXITCODE -ne 0 -or ($out -join "`n") -ne $want) {
        Write-Host "FAIL cgen-debug codegen_exec.goose (exit $LASTEXITCODE)"
        $failures++
    } else {
        Write-Host "ok   cgen-debug codegen_exec.goose"
    }
    # --- every runnable test combined into one program -----------------------
    # Tests keep globally unique top-level names for this; each main becomes
    # main_<name>, called in order (optimize last: it ends in a runtime abort).
    $order = @("typecheck", "roots", "spec_examples", "codegen_exec",
               "codegen_threads", "parser_all", "optimize")
    $missing = $order | Where-Object { -not $allouts.ContainsKey($_) }
    if ($missing) {
        Write-Host "FAIL combined (missing per-test runs: $missing)"
        $failures++
    } else {
        $lines = @("// Generated by run_tests.ps1: every runnable test in one program.")
        $calls = @()
        foreach ($name in $order) {
            $lines += "// ==== $name.goose ===="
            foreach ($ln in (Get-Content "$PSScriptRoot\$name.goose")) {
                $lines += ($ln -replace '^fn main\(\) \{', "fn main_$name() {")
            }
            $calls += "    main_$name();"
        }
        $lines += @("fn main() {") + $calls + @("}")
        [IO.File]::WriteAllLines("$gendir\all_tests.goose", $lines, $utf8)
        Copy-Item "$PSScriptRoot\util.goose" $gendir -Force
        New-Item -ItemType Directory -Force "$gendir\sub" | Out-Null
        Copy-Item "$PSScriptRoot\sub\*.goose" "$gendir\sub" -Force
        $want = ($order | ForEach-Object { $allouts[$_] }) -join "`n"
        $bad = $false
        foreach ($ol in "0", "2") {
            & $exe "-O$ol" -o "$gendir\all_tests-O$ol.c" "$gendir\all_tests.goose" | Out-Null
            if ($LASTEXITCODE -ne 0) {
                & $exe "-O$ol" -o "$gendir\all_tests-O$ol.c" "$gendir\all_tests.goose"
                Write-Host "FAIL combined cgen -O$ol"; $failures++; $bad = $true; continue
            }
            & $cc /nologo /W3 "$gendir\all_tests-O$ol.c" "/Fe:$gendir\all_tests-O$ol.exe" "/Fo:$gendir\all_tests-O$ol.obj" > "$gendir\all_tests-O$ol.cl.log" 2>&1
            if ($LASTEXITCODE -ne 0) {
                Get-Content "$gendir\all_tests-O$ol.cl.log" | Select-Object -First 8
                Write-Host "FAIL combined cc -O$ol"; $failures++; $bad = $true; continue
            }
            $out = & "$gendir\all_tests-O$ol.exe" 2>$null
            if ($LASTEXITCODE -eq 0) {   # optimize's final division aborts.
                Write-Host "FAIL combined run -O$ol (expected the trailing abort)"
                $failures++; $bad = $true; continue
            }
            if (($out -join "`n") -ne $want) {
                Write-Host "FAIL combined output -O$ol"
                $failures++; $bad = $true
            }
        }
        if (-not $bad) { Write-Host "ok   combined all_tests (O0+O2)" }
    }
}

foreach ($f in Get-ChildItem "$PSScriptRoot\errors\*.goose") {
    & $exe --parse $f.FullName 2>&1 | Out-Null
    if ($LASTEXITCODE -eq 0) { Write-Host "FAIL expected-error $($f.Name)"; $failures++ }
    else { Write-Host "ok   error $($f.Name)" }
}

# Typecheck error tests: must parse, must fail the typechecker.
foreach ($f in Get-ChildItem "$PSScriptRoot\errors_tc\*.goose") {
    & $exe --parse $f.FullName 2>&1 | Out-Null
    if ($LASTEXITCODE -ne 0) {
        & $exe --parse $f.FullName
        Write-Host "FAIL tc-error-parses $($f.Name)"
        $failures++
        continue
    }
    & $exe $f.FullName 2>&1 | Out-Null
    if ($LASTEXITCODE -eq 0) { Write-Host "FAIL expected-tc-error $($f.Name)"; $failures++ }
    else { Write-Host "ok   tc-error $($f.Name)" }
}

if ($failures) { Write-Host "$failures FAILURE(S)"; exit 1 }
Write-Host "all tests passed"
