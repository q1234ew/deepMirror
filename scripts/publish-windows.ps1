$ErrorActionPreference = 'Stop'
$zip = Join-Path $PWD 'build-windows/DeepMirror-Windows-x64.zip'
Compress-Archive -Path dist/* -DestinationPath $zip -Force
# Inspect the actual ZIP rather than relying only on staging-directory copies.
$archive = [System.IO.Compression.ZipFile]::OpenRead($zip)
try {
    foreach ($required in @('README.md', 'docs/windows-camera-test.md', 'Windows-Camera-Test.html')) {
        $entry = $archive.Entries | Where-Object { $_.FullName.Replace('\', '/') -eq $required }
        if (-not $entry -or $entry.Length -eq 0) { throw "Missing packaged document: $required" }
    }
} finally { $archive.Dispose() }
$checksum = "$zip.sha256"
"$((Get-FileHash $zip -Algorithm SHA256).Hash.ToLower())  DeepMirror-Windows-x64.zip" | Set-Content $checksum -Encoding ascii
$notes = Join-Path $PWD 'build-windows/release-notes.md'
@(
    'Windows x64 portable build. Extract the ZIP and launch deepmirror.exe.',
    '',
    "Commit: $env:GITHUB_SHA",
    'Qt 6.8.2 / MSVC x64 / Insight7 PID 3652:0104.',
    'Windows compilation, offline tests and packaged data self-test passed.',
    'Offline camera test instructions: open Windows-Camera-Test.html beside deepmirror.exe. Markdown source is included in docs/windows-camera-test.md.',
    'Real camera capture still requires hardware validation. Enable depth output manually.',
    '',
    'Published as a prerelease; repository visibility is unchanged.'
) | Set-Content $notes -Encoding utf8
$tag = "windows-build-$env:GITHUB_RUN_NUMBER"
& gh release view $tag --repo $env:GH_REPO 2>$null
if ($LASTEXITCODE -eq 0) {
    & gh release upload $tag $zip $checksum --repo $env:GH_REPO --clobber
} else {
    & gh release create $tag $zip $checksum --repo $env:GH_REPO --target $env:GITHUB_SHA --title "Windows x64 build $env:GITHUB_RUN_NUMBER" --notes-file $notes --prerelease
}
if ($LASTEXITCODE -ne 0) { throw 'Release upload failed' }
"Download: https://github.com/$env:GH_REPO/releases/tag/$tag" | Out-File -FilePath $env:GITHUB_STEP_SUMMARY -Append -Encoding utf8
