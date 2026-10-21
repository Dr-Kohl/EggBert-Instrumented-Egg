param(
    [string]$Port = 'COM37',
    [string]$OutputDirectory = 'build',
    [string]$Prefix = 'dual-bench'
)
$ErrorActionPreference = 'Stop'
$captureDestination = [IO.Path]::GetFullPath($OutputDirectory)
[IO.Directory]::CreateDirectory($captureDestination) | Out-Null
$capturePort = [IO.Ports.SerialPort]::new($Port,115200,[IO.Ports.Parity]::None,8,[IO.Ports.StopBits]::One)
$capturePort.DtrEnable = $true
try {
    $capturePort.Open()
    foreach ($captureKind in @('raw','legacy')) {
        $capturePort.DiscardInBuffer()
        $capturePort.Write($(if ($captureKind -eq 'raw') { "download raw`n" } else { "download`n" }))
        $captureMagics = if ($captureKind -eq 'raw') { @('EGD2') } else { @('EGG1','EGG2') }
        $captureStream = [IO.MemoryStream]::new()
        try {
            $captureTimer = [Diagnostics.Stopwatch]::StartNew()
            $captureOffset = -1
            $captureLength = 0
            while ($captureTimer.Elapsed.TotalSeconds -lt 20) {
                $captureAvailable = $capturePort.BytesToRead
                if ($captureAvailable) {
                    $captureChunk = [byte[]]::new($captureAvailable)
                    $captureRead = $capturePort.Read($captureChunk,0,$captureChunk.Length)
                    $captureStream.Write($captureChunk,0,$captureRead)
                    $captureBytes = $captureStream.GetBuffer()
                    if ($captureOffset -lt 0) {
                        for ($captureIndex=0; $captureIndex -le $captureStream.Length-8; $captureIndex++) {
                            if ([Text.Encoding]::ASCII.GetString($captureBytes,$captureIndex,4) -in $captureMagics -and
                                $captureBytes[$captureIndex+4] -in @(1,2) -and
                                [BitConverter]::ToUInt16($captureBytes,$captureIndex+6) -in @(32,80)) {
                                $captureOffset = $captureIndex
                                break
                            }
                        }
                    }
                    if ($captureOffset -ge 0 -and $captureStream.Length -ge $captureOffset+32) {
                        $captureCount = [BitConverter]::ToUInt32($captureBytes,$captureOffset+12)
                        $captureStride = if ($captureKind -eq 'raw') {
                            [BitConverter]::ToUInt32($captureBytes,$captureOffset+24)
                        } else { [BitConverter]::ToUInt16($captureBytes,$captureOffset+26) }
                        if ($captureCount -gt 19200 -or $captureStride -notin @(6,12)) { throw 'Invalid capture header' }
                        $captureHeaderLength = [BitConverter]::ToUInt16($captureBytes,$captureOffset+6)
                        $captureLength = $captureHeaderLength + $captureCount*$captureStride
                        if ($captureStream.Length -ge $captureOffset+$captureLength) { break }
                    }
                } else { Start-Sleep -Milliseconds 10 }
            }
            if (!$captureLength -or $captureStream.Length -lt $captureOffset+$captureLength) {
                throw ('Incomplete '+$captureKind+' download: '+[Text.Encoding]::ASCII.GetString($captureStream.ToArray()))
            }
            $captureFrame = [byte[]]::new($captureLength)
            [Array]::Copy($captureStream.GetBuffer(),$captureOffset,$captureFrame,0,$captureLength)
            $captureExtension = if ($captureKind -eq 'raw') { '.egd2' } else { '.egg' }
            $captureFile = Join-Path $captureDestination ($Prefix+$captureExtension)
            [IO.File]::WriteAllBytes($captureFile,$captureFrame)
            Write-Output ($captureFile+' ('+$captureCount+' samples)')
        } finally { $captureStream.Dispose() }
    }
    $capturePort.Write("status`n")
    Start-Sleep -Milliseconds 500
    $captureStatus = $capturePort.ReadExisting()
    $captureStatus | Set-Content -LiteralPath (Join-Path $captureDestination ($Prefix+'-status.txt')) -Encoding utf8
    Write-Output $captureStatus
} finally { $capturePort.Dispose() }
