# 用 PowerPoint 把 PPT 每一页导出为 PNG，便于检查版式：powershell -File docs/ppt/render.ps1
param([string]$Pptx = "$PSScriptRoot\..\GZG语言介绍.pptx", [string]$OutDir = "$PSScriptRoot\..\..\build\slides")
$Pptx = (Resolve-Path $Pptx).Path
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
$OutDir = (Resolve-Path $OutDir).Path
$app = New-Object -ComObject PowerPoint.Application
$pres = $app.Presentations.Open($Pptx, $true, $false, $false)
$i = 1
foreach ($slide in $pres.Slides) {
    $slide.Export((Join-Path $OutDir ("slide{0:D2}.png" -f $i)), "PNG", 1600, 900)
    $i++
}
$pres.Close()
$app.Quit()
Write-Output "exported $($i - 1) slides to $OutDir"
