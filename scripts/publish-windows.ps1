$ErrorActionPreference = 'Stop'
$zip = Join-Path $PWD 'build-windows/DeepMirror-Windows-x64.zip'
Compress-Archive -Path dist/* -DestinationPath $zip -Force
$checksum = "$zip.sha256"
"$((Get-FileHash $zip -Algorithm SHA256).Hash.ToLower())  DeepMirror-Windows-x64.zip" | Set-Content $checksum -Encoding ascii
$notes = Join-Path $PWD 'build-windows/release-notes.md'
@(
    'Windows x64 portable build. Extract the ZIP and launch deepmirror.exe.',
    '',
    "Commit: $env:GITHUB_SHA",
    'Qt 6.8.2 / MSVC x64 / Insight7 PID 3652:0104.',
    'Windows compilation, offline tests and packaged data self-test passed.',
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
