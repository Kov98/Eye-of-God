# Suite do leitor de ZIP e do importador de exportacao de rede social.
# Monta os fixtures, compila os testes e o CLI, roda e imprime um placar.
$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$repo = Split-Path -Parent $root
Set-Location $repo
$gpp = 'C:\msys64\ucrt64\bin\g++.exe'
if (-not (Test-Path $gpp)) { $c = Get-Command g++ -ErrorAction SilentlyContinue; if ($c) { $gpp = $c.Source } }
if (-not (Test-Path $gpp)) { throw 'g++ not found' }
$inc = @('-I', $repo, '-I', (Join-Path $repo 'third_party'))

# --- fixtures ---
$src = Join-Path $root 'zip_src'
if (Test-Path $src) { Remove-Item $src -Recurse -Force }
New-Item -ItemType Directory -Force (Join-Path $src 'messages\inbox\thread1') | Out-Null
Set-Content -Path (Join-Path $src 'messages\inbox\thread1\message_1.json') -Value '{"participants":[{"name":"Ana"}],"messages":[{"sender_name":"Ana","timestamp_ms":1696000000000,"content":"oi"}]}' -NoNewline
Set-Content -Path (Join-Path $src 'small.txt') -Value 'abc' -NoNewline
$nl = [Environment]::NewLine
$big = ('linha de teste repetida para forcar compressao real' + $nl) * 400
Set-Content -Path (Join-Path $src 'big.txt') -Value $big -NoNewline
Set-Content -Path (Join-Path $src 'acentuacao.txt') -Value 'conteudo com acentuacao' -NoNewline -Encoding UTF8
$zipFixture = Join-Path $root 'zip_fixture.zip'
Remove-Item $zipFixture -ErrorAction SilentlyContinue
Compress-Archive -Path (Join-Path $src '*') -DestinationPath $zipFixture -Force
$fbZip = Join-Path $root 'dyi_fb.zip'
Remove-Item $fbZip -ErrorAction SilentlyContinue
Compress-Archive -Path (Join-Path $root 'fixtures\fb\*') -DestinationPath $fbZip -Force
$igDir = Join-Path $root 'dyi_ig_pasta'
if (Test-Path $igDir) { Remove-Item $igDir -Recurse -Force }
New-Item -ItemType Directory -Force $igDir | Out-Null
Copy-Item (Join-Path $root 'fixtures\ig\*') $igDir -Recurse -Force

# Os testes usam caminhos relativos a .oracle/, entao a suite monta a mesma
# estrutura ali antes de rodar.
$link = Join-Path $repo '.oracle'
if (-not (Test-Path $link)) { New-Item -ItemType Directory -Force $link | Out-Null }
Copy-Item $zipFixture (Join-Path $link 'zip_fixture.zip') -Force
foreach ($name in @('zip_src', 'dyi_fb', 'dyi_ig', 'dyi_ig_pasta', 'dyi_case', 'cli_case')) {
    $target = Join-Path $link $name
    if (Test-Path $target) { Remove-Item $target -Recurse -Force }
}
Copy-Item $src (Join-Path $link 'zip_src') -Recurse -Force
Copy-Item $fbZip (Join-Path $link 'dyi_fb.zip') -Force
Copy-Item (Join-Path $root 'fixtures\fb') (Join-Path $link 'dyi_fb') -Recurse -Force
Copy-Item (Join-Path $root 'fixtures\ig') (Join-Path $link 'dyi_ig') -Recurse -Force
Copy-Item $igDir (Join-Path $link 'dyi_ig_pasta') -Recurse -Force
# Takeout: uma caixa do Gmail em .mbox dentro da arvore do Google.
$takeout = Join-Path $link 'takeout_fixture\Takeout\Mail'
if (Test-Path (Join-Path $link 'takeout_fixture')) { Remove-Item (Join-Path $link 'takeout_fixture') -Recurse -Force }
New-Item -ItemType Directory -Force $takeout | Out-Null
Copy-Item (Join-Path $repo '.oracle\mail_fixture.mbox') (Join-Path $takeout 'All mail.mbox') -Force
# Takeout completo: fotos com sidecar, Drive e contatos.
if (Test-Path (Join-Path $link 'takeout_full')) { Remove-Item (Join-Path $link 'takeout_full') -Recurse -Force }
Copy-Item (Join-Path $root 'fixtures\takeout_full') (Join-Path $link 'takeout_full') -Recurse -Force

# Telegram: uma pasta chats/<id>/result.json por conversa.
if (Test-Path (Join-Path $link 'telegram_fixture')) { Remove-Item (Join-Path $link 'telegram_fixture') -Recurse -Force }
Copy-Item (Join-Path $root 'fixtures\telegram') (Join-Path $link 'telegram_fixture') -Recurse -Force
$vazio = Join-Path $link 'dyi_vazio\vazio'
New-Item -ItemType Directory -Force $vazio | Out-Null
Set-Content -Path (Join-Path $vazio 'leiame.txt') -Value 'sem mensagens' -NoNewline

# --- fixtures ZIP64 (geradas pelo zipfile do Python: oraculo independente) ---
Remove-Item (Join-Path $link 'zip64_count.zip'), (Join-Path $link 'zip64_extra.zip'), (Join-Path $link 'zip64_payload.bin') -ErrorAction SilentlyContinue
python (Join-Path $repo '.oracle\make_zip64.py') | Out-Null
if (-not (Test-Path (Join-Path $link 'zip64_count.zip'))) { throw 'zip64 fixtures missing' }

# --- compila ---
$zipTest = Join-Path $root 'zip_test.exe'
$dyiTest = Join-Path $root 'dyi_test.exe'
$socialExe = Join-Path $root 'olho_social.exe'
Remove-Item $zipTest, $dyiTest, $socialExe -ErrorAction SilentlyContinue
& $gpp -std=c++17 -O2 -Wall -Wextra @inc (Join-Path $root 'zip_test.cpp') (Join-Path $repo 'social\zip_reader.cpp') (Join-Path $repo 'android_backup.cpp') -lbcrypt -o $zipTest
if ($LASTEXITCODE -ne 0) { throw 'zip_test build failed' }
& $gpp -std=c++17 -O2 -Wall -Wextra @inc (Join-Path $root 'dyi_test.cpp') (Join-Path $repo 'social\dyi_import.cpp') (Join-Path $repo 'social\takeout_import.cpp') (Join-Path $repo 'social\telegram_import.cpp') (Join-Path $repo 'social\mbox_reader.cpp') (Join-Path $repo 'social\zip_reader.cpp') (Join-Path $repo 'android_backup.cpp') -lbcrypt -o $dyiTest
if ($LASTEXITCODE -ne 0) { throw 'dyi_test build failed' }
& $gpp -std=c++17 -O2 -Wall -Wextra @inc (Join-Path $repo 'social_main.cpp') (Join-Path $repo 'social\dyi_import.cpp') (Join-Path $repo 'social\takeout_import.cpp') (Join-Path $repo 'social\telegram_import.cpp') (Join-Path $repo 'social\mbox_reader.cpp') (Join-Path $repo 'social\zip_reader.cpp') (Join-Path $repo 'android_backup.cpp') -lbcrypt -o $socialExe
if ($LASTEXITCODE -ne 0) { throw 'olho_social build failed' }

# --- roda ---
$zipOut = & $zipTest 2>&1
$zipCode = $LASTEXITCODE
$zipChecks = ($zipOut | Select-String -Pattern '^  \[' | Measure-Object).Count
$zipFails = ($zipOut | Select-String -Pattern '^  \[FAIL\]' | Measure-Object).Count
$dyiOut = & $dyiTest 2>&1
$dyiCode = $LASTEXITCODE
$dyiChecks = ($dyiOut | Select-String -Pattern '^  \[' | Measure-Object).Count
$dyiFails = ($dyiOut | Select-String -Pattern '^  \[FAIL\]' | Measure-Object).Count
$zipOut | Where-Object { "$_" -match 'FAIL' } | ForEach-Object { Write-Host "$_" }
$dyiOut | Where-Object { "$_" -match 'FAIL' } | ForEach-Object { Write-Host "$_" }

$cliOut = & $socialExe --export (Join-Path $link 'dyi_fb.zip') --case (Join-Path $link 'cli_case') --account 'kai@exemplo.com' --legal-basis 'OFICIO-1' --job 'job-cli' 2>&1
$cliCode = $LASTEXITCODE
$manifestPath = Join-Path $link 'cli_case\social\facebook\kai_exemplo.com\manifest.json'
$manifestOk = Test-Path $manifestPath
if (-not $manifestOk) { Write-Host "cli: manifesto nao gerado" }

$total = $zipChecks + $dyiChecks + 1
$fails = $zipFails + $dyiFails + $(if ($cliCode -eq 0 -and $manifestOk) { 0 } else { 1 })
$passed = $total - $fails
Write-Host ("checks: {0} de {1} passaram, {2} falharam (zip {3}/{4}, dyi {5}/{6}, cli exit {7})" -f $passed, $total, $fails, ($zipChecks - $zipFails), $zipChecks, ($dyiChecks - $dyiFails), $dyiChecks, $cliCode)
if ($zipCode -ne 0 -or $dyiCode -ne 0 -or $cliCode -ne 0 -or -not $manifestOk) { exit 1 }
exit 0
