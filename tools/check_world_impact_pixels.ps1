param([Parameter(Mandatory=$true)][string]$Folder)
$ErrorActionPreference='Stop'
Add-Type -AssemblyName System.Drawing
$taskFrames=Import-Csv (Join-Path $Folder 'world_impact_review.csv')
$taskDraws=Import-Csv (Join-Path $Folder 'world_impact_draw.csv')
foreach($taskIndex in @(1,2)) {
    $taskFrame=$taskFrames | Where-Object shot -eq ([string]$taskIndex)
    $taskNearest=$taskDraws | Sort-Object { [Math]::Abs([double]$_.distance-[double]$taskFrame.distance) } | Select-Object -First 1
    $taskImage=[System.Drawing.Bitmap]::FromFile((Join-Path $Folder "impact-$taskIndex.png"))
    try {
        $taskX=[int][double]$taskNearest.screen_x
        $taskY=[int][double]$taskNearest.screen_y
        $taskBlue=0
        for($taskPy=[Math]::Max(0,$taskY-28);$taskPy -lt [Math]::Min($taskImage.Height,$taskY+29);$taskPy++) {
            for($taskPx=[Math]::Max(0,$taskX-28);$taskPx -lt [Math]::Min($taskImage.Width,$taskX+29);$taskPx++) {
                $taskColor=$taskImage.GetPixel($taskPx,$taskPy)
                if($taskColor.B -gt 80 -and $taskColor.B -gt ($taskColor.R*1.25) -and
                    $taskColor.B -gt ($taskColor.G*1.08) -and ($taskColor.B-$taskColor.R) -gt 12) {$taskBlue++}
            }
        }
        if($taskBlue -lt 4) {throw "Frame ${taskIndex}: blue surface afterglow is not visible ($taskBlue pixels)."}
        Write-Output "Frame ${taskIndex}: $taskBlue visible blue pixels near the resolved contact."
    } finally {$taskImage.Dispose()}
}
