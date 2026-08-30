# Goose test runner: parses every test file, checks dump/reparse/dump
# roundtrips to identical output, typechecks files not marked `parse-only`
# on their first line, and checks that error tests fail in the right phase.
param([string]$exe = "$PSScriptRoot\..\build\Debug\goose.exe")

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
        & $exe $f.FullName | Out-Null
        if ($LASTEXITCODE -ne 0) {
            & $exe $f.FullName
            Write-Host "FAIL typecheck $($f.Name)"
            $failures++
        } else {
            Write-Host "ok   typecheck $($f.Name)"
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
