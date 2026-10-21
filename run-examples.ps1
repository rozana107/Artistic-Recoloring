param(
    [int]$Repeat = 7,
    [ValidateSet('abstraction','graph','slico')][string]$Method = 'abstraction',
    [string]$InputDirectory = 'examples',
    [string]$OutputDirectory = '',
    [switch]$Diagnostics
)

$ErrorActionPreference = 'Stop'
Push-Location $PSScriptRoot
try {
    $executable = Join-Path $PSScriptRoot 'bin/recolor.exe'
    if (-not (Test-Path -LiteralPath $executable)) { throw 'Build using build.ps1 first.' }
    if ($OutputDirectory -eq '') {
        if ($Method -eq 'abstraction') { $OutputDirectory = 'results/examples' }
        else { $OutputDirectory = "results/$Method/examples" }
    }
    New-Item -ItemType Directory -Force -Path $OutputDirectory | Out-Null
    $rows = @()
    $images = Get-ChildItem -LiteralPath $InputDirectory -File | Where-Object { $_.Extension.ToLower() -in @('.jpg','.jpeg','.png','.bmp','.tif','.tiff') } | Sort-Object Name
    if ($images.Count -eq 0) { throw 'No input images found.' }
    foreach ($image in $images) {
        $name = $image.BaseName + '_' + $image.Extension.TrimStart('.')
        $directory = Join-Path $OutputDirectory $name
        New-Item -ItemType Directory -Force -Path $directory | Out-Null
        $hashBefore = (Get-FileHash -LiteralPath $image.FullName).Hash
        Write-Host "Processing $($image.Name) with $Method ..."
        $arguments = @($image.FullName, "$directory/sunset.png", '--method', $Method, '--repeat', $Repeat,
                       '--save-labels', "$directory/labels.yml")
        if ($Method -eq 'abstraction') {
            $arguments += @('--abstraction', "$directory/abstraction.png")
        }
        if ($Diagnostics) {
            $arguments += @('--flat', "$directory/flat.png")
            if ($Method -eq 'abstraction') { $arguments += @('--save-filtered', "$directory/filtered.png") }
        }
        $output = & $executable @arguments
        if ($LASTEXITCODE -ne 0) { throw "Recoloring failed for $name." }
        $output | Set-Content -LiteralPath "$directory/timing.txt"
        $text = $output -join "`n"
        $sizeMatch = [regex]::Match($text, 'Image: (\d+) x (\d+)')
        $regionMatch = [regex]::Match($text, 'Regions: (\d+), edges: (\d+)')
        $segmentMatch = [regex]::Match($text, 'Segmentation: ([\d.]+) ms')
        $recolorMatch = [regex]::Match($text, 'Recoloring median \(\d+ runs\): ([\d.]+) ms')
        $wallMatch = [regex]::Match($text, 'Wall time.*: ([\d.]+) ms')
        $oceanArguments = @($image.FullName, "$directory/ocean.png", '--labels', "$directory/labels.yml", '--palette', 'palettes/ocean.txt')
        if ($Method -eq 'abstraction') { $oceanArguments += @('--region-image', "$directory/abstraction.png") }
        $oceanOutput = & $executable @oceanArguments
        if ($LASTEXITCODE -ne 0) { throw "Ocean recoloring failed for $name." }
        Add-Content -LiteralPath "$directory/timing.txt" -Value @('', 'Ocean palette (cached labels and region colors):', $oceanOutput)
        if ($hashBefore -ne (Get-FileHash -LiteralPath $image.FullName).Hash) { throw "Input changed: $name" }
        $rows += [pscustomobject]@{
            Image = $image.Name; Method = $Method
            Width = [int]$sizeMatch.Groups[1].Value; Height = [int]$sizeMatch.Groups[2].Value
            Regions = [int]$regionMatch.Groups[1].Value; Edges = [int]$regionMatch.Groups[2].Value
            SegmentationMs = [double]$segmentMatch.Groups[1].Value
            RecoloringMs = [double]$recolorMatch.Groups[1].Value
            WallAllRepeatsMs = [double]$wallMatch.Groups[1].Value
            InputSha256 = $hashBefore; Directory = $directory
        }
        $rows | Export-Csv -NoTypeInformation -LiteralPath "$OutputDirectory/timings.csv"
        Write-Host "Finished $($image.Name): $($regionMatch.Groups[1].Value) regions, $($segmentMatch.Groups[1].Value) ms segmentation."
    }
    $rows | Format-Table Image,Width,Height,Regions,SegmentationMs,RecoloringMs -AutoSize
} finally {
    Pop-Location
}
