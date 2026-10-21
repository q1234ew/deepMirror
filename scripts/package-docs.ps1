#requires -Version 7.0
param(
    [string]$SourceRoot = (Split-Path -Parent $PSScriptRoot),
    [string]$PackageDir = (Join-Path $PWD 'dist')
)
$ErrorActionPreference = 'Stop'
$guide = Join-Path $SourceRoot 'docs/windows-camera-test.md'
$docDir = Join-Path $PackageDir 'docs'
New-Item -ItemType Directory -Force $docDir | Out-Null
Copy-Item -LiteralPath $guide -Destination (Join-Path $docDir 'windows-camera-test.md')
$rendered = (ConvertFrom-Markdown -Path $guide).Html
if ($rendered -notmatch '<h1' -or $rendered -notmatch '<table' -or ([regex]::Matches($rendered, '<h2\b')).Count -lt 7) {
    throw 'Offline test guide conversion is incomplete'
}
$sourceHash = (Get-FileHash $guide -Algorithm SHA256).Hash.ToLower()
$html = @"
<!doctype html>
<html lang="zh-CN">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<meta name="source-sha256" content="$sourceHash">
<title>Windows 相机采集测试步骤 · DeepMirror</title>
<style>
body { margin:0; background:#f3f5f8; color:#213047; font:16px/1.8 "Segoe UI","Microsoft YaHei",sans-serif; }
main { max-width:980px; margin:24px auto; padding:28px 40px; background:white; border:1px solid #dce3ec; border-radius:10px; }
h1 { font-size:28px; line-height:1.4; } h2 { margin-top:34px; font-size:21px; border-bottom:1px solid #dce3ec; padding-bottom:8px; }
a { color:#1e5bb8; overflow-wrap:anywhere; } code { background:#edf1f6; padding:2px 5px; border-radius:3px; overflow-wrap:anywhere; }
table { width:100%; border-collapse:collapse; margin:16px 0; } th,td { border:1px solid #dce3ec; padding:10px 12px; text-align:left; vertical-align:top; } th { background:#edf2f9; }
.notice { color:#52647b; background:#eef4ff; border-left:3px solid #3876d4; padding:12px 16px; }
li { margin:5px 0; } @media(max-width:700px) { main { margin:0; padding:16px; border:0; } table { font-size:14px; } }
@media print { body { background:white; } main { margin:0; padding:0; border:0; } h2 { break-after:avoid; } tr { break-inside:avoid; } }
</style>
</head>
<body><main>
<p class="notice">本文件可离线查看，无需安装 Markdown 阅读器。程序已在同目录中，直接运行 deepmirror.exe；相机配置网页需要连接相机，GitHub 下载链接需要联网。</p>
$rendered
</main></body>
</html>
"@
$destination = Join-Path $PackageDir 'Windows-Camera-Test.html'
$html | Set-Content -LiteralPath $destination -Encoding utf8
if ((Get-FileHash (Join-Path $docDir 'windows-camera-test.md') -Algorithm SHA256).Hash.ToLower() -ne $sourceHash) {
    throw 'Packaged Markdown differs from project documentation'
}
Write-Host "Offline guide ready: $destination"
