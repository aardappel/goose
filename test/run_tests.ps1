# Goose parser test runner: parses every test file, checks dump/reparse/dump
# roundtrips to identical output, and checks that error tests fail.
param([string]$exe = "$PSScriptRoot\..\build\Debug\goose.exe")

$failures = 0
$utf8 = New-Object System.Text.UTF8Encoding($false)

& $exe --tokens "$PSScriptRoot\lexer_tokens.goose" | Out-Null
if ($LASTEXITCODE -ne 0) { Write-Host "FAIL lex lexer_tokens.goose"; $failures++ }
else { Write-Host "ok   lex lexer_tokens.goose" }

foreach ($f in Get-ChildItem "$PSScriptRoot\*.goose") {
    if ($f.Name -eq "lexer_tokens.goose") { continue }
    & $exe $f.FullName | Out-Null
    if ($LASTEXITCODE -ne 0) {
        & $exe $f.FullName
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
}

foreach ($f in Get-ChildItem "$PSScriptRoot\errors\*.goose") {
    & $exe $f.FullName 2>&1 | Out-Null
    if ($LASTEXITCODE -eq 0) { Write-Host "FAIL expected-error $($f.Name)"; $failures++ }
    else { Write-Host "ok   error $($f.Name)" }
}

if ($failures) { Write-Host "$failures FAILURE(S)"; exit 1 }
Write-Host "all tests passed"
