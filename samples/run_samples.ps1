# Compiles every sample, builds the generated C with MSVC, runs it and compares
# its stdout, byte for byte after newline normalization, with
# expected/<name>.out. Samples run with this directory as the working
# directory; a sample reads data/<name>.stdin as its stdin when that file
# exists, and a sample's C header (<name>.h, see call_c) is passed with
# --include. Timings and machine-dependent facts go to stderr, which is not
# compared.
#
# Used by test/run_tests.ps1; runnable on its own:
#   powershell -File samples\run_samples.ps1 [-exe path\to\goose.exe] [-bless]
# -bless rewrites the expected outputs from the current runs.
param([string]$exe = "$PSScriptRoot\..\build\Debug\goose.exe", [switch]$bless, [switch]$nocgen)

$failures = 0
$utf8 = New-Object System.Text.UTF8Encoding($false)
[Console]::OutputEncoding = $utf8

$cc = $null
if (-not $nocgen) {
    $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
    if (Test-Path $vswhere) {
        $vsroot = & $vswhere -latest -property installationPath 2>$null
        if ($vsroot) {
            foreach ($l in (cmd /c "`"$vsroot\VC\Auxiliary\Build\vcvars64.bat`" >nul 2>&1 && set")) {
                if ($l -match '^([^=]+)=(.*)$') {
                    [Environment]::SetEnvironmentVariable($matches[1], $matches[2])
                }
            }
            $cc = "cl"
        }
    }
}

$gendir = "$PSScriptRoot\..\build\gen\samples"
New-Item -ItemType Directory -Force $gendir | Out-Null
New-Item -ItemType Directory -Force "$PSScriptRoot\expected" | Out-Null

# A file's text with CRLF folded to LF, so the comparison does not depend on
# how the C runtime or git translated line ends.
function Normalized([string]$path) {
    if (-not (Test-Path $path)) { return $null }
    return [IO.File]::ReadAllText($path, $utf8).Replace("`r`n", "`n")
}

foreach ($f in Get-ChildItem "$PSScriptRoot\*.goose") {
    # The number prefix orders the files for reading; outputs, data and
    # headers go by the bare name.
    $name = [IO.Path]::GetFileNameWithoutExtension($f.Name) -replace '^\d+_', ''
    $cfile = "$gendir\$name.c"
    $efile = "$gendir\$name.exe"
    $gargs = @("-O2")
    $header = "$PSScriptRoot\$name.h"
    if (Test-Path $header) { $gargs += @("--include", $header) }
    if ($cc) { $gargs += @("-o", $cfile) } else { $gargs += "--check" }
    $gargs += $f.FullName
    & $exe @gargs | Out-Null
    if ($LASTEXITCODE -ne 0) {
        & $exe @gargs
        Write-Host "FAIL sample-compile $($f.Name)"
        $failures++
        continue
    }
    if (-not $cc) { Write-Host "ok   sample-check $($f.Name)"; continue }
    & $cc /nologo /W3 /O2 $cfile "/Fe:$efile" "/Fo:$gendir\$name.obj" > "$gendir\$name.cl.log" 2>&1
    if ($LASTEXITCODE -ne 0) {
        Get-Content "$gendir\$name.cl.log" | Select-Object -First 8
        Write-Host "FAIL sample-cc $($f.Name)"
        $failures++
        continue
    }
    $infile = "$PSScriptRoot\data\$name.stdin"
    $outfile = "$gendir\$name.out"
    $errfile = "$gendir\$name.err"
    Push-Location $PSScriptRoot
    if (Test-Path $infile) {
        cmd /c "`"$efile`" < `"$infile`" > `"$outfile`" 2> `"$errfile`""
    } else {
        cmd /c "`"$efile`" > `"$outfile`" 2> `"$errfile`""
    }
    $code = $LASTEXITCODE
    Pop-Location
    if ($code -ne 0) {
        Get-Content $errfile | Select-Object -First 3
        Write-Host "FAIL sample-run $($f.Name) (exit $code)"
        $failures++
        continue
    }
    $got = Normalized $outfile
    $expfile = "$PSScriptRoot\expected\$name.out"
    if ($bless) {
        [IO.File]::WriteAllText($expfile, $got, $utf8)
        Write-Host "ok   sample-blessed $($f.Name)"
        continue
    }
    $want = Normalized $expfile
    if ($want -ne $null -and $got -ne $want) {
        Write-Host "FAIL sample-expected $($f.Name)"
        Write-Host "--- got:`n$got`n--- want:`n$want"
        $failures++
        continue
    }
    Write-Host "ok   sample $($f.Name)"
}

if ($failures) { Write-Host "$failures SAMPLE FAILURE(S)"; exit 1 }
Write-Host "all samples passed"
exit 0
