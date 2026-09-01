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

# Bounds-check elimination: verify the per-line elide/keep annotations in
# bce.goose (the file's runtime behavior is covered by the cgen runs below).
& $exe --check --bce-test "$PSScriptRoot\bce.goose" | Out-Null
if ($LASTEXITCODE -ne 0) {
    & $exe --check --bce-test "$PSScriptRoot\bce.goose"
    Write-Host "FAIL bce-test bce.goose"
    $failures++
} else {
    Write-Host "ok   bce-test bce.goose"
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

    # The same coverage test through clang, release and debug. MSVC's C front
    # end accepts things clang rejects, so a second compiler is what checks the
    # generated C is actually valid C: a call to a function defined only in
    # debug builds compiled silently under MSVC and broke every clang release
    # build. clang here is the VS-bundled one unless PATH has its own; it needs
    # the vcvars environment imported above to link.
    $clang = (Get-Command clang.exe -ErrorAction SilentlyContinue).Source
    if (-not $clang -and $vsroot) {
        $vsclang = "$vsroot\VC\Tools\Llvm\x64\bin\clang.exe"
        if (Test-Path $vsclang) { $clang = $vsclang }
    }
    if (-not $clang) {
        Write-Host "skip cgen-clang (no clang found)"
    } else {
        & $exe -O2 -o "$gendir\cgclang.c" "$PSScriptRoot\codegen_exec.goose" | Out-Null
        foreach ($label in "release", "debug") {
            $cargs = @("-O1", "-Wno-everything", "-Werror=implicit-function-declaration",
                       "$gendir\cgclang.c", "-o", "$gendir\cgclang-$label.exe")
            if ($label -eq "debug") { $cargs = @("-DGS_DEBUG=1") + $cargs }
            & $clang @cargs > "$gendir\cgclang-$label.log" 2>&1
            if ($LASTEXITCODE -ne 0) {
                Get-Content "$gendir\cgclang-$label.log" | Select-Object -First 8
                Write-Host "FAIL cgen-clang-$label codegen_exec.goose"
                $failures++
                continue
            }
            $out = & "$gendir\cgclang-$label.exe" 2>$null
            if ($LASTEXITCODE -ne 0 -or ($out -join "`n") -ne $want) {
                Write-Host "FAIL cgen-clang-$label-output codegen_exec.goose (exit $LASTEXITCODE)"
                $failures++
            } else {
                Write-Host "ok   cgen-clang-$label codegen_exec.goose"
            }
        }
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
# Explicit, so the script's status is its own and not the last native command's
# (the error tests all exit nonzero by design).
exit 0
