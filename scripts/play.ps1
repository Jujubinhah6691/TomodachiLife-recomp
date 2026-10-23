# Play the built game by hand with no time limit; player 1's input (pad or keyboard) is recorded for replay.
#   powershell -File scripts/play.ps1 [name]      -> build/work/play_<name>.rec and play_<name>.log
# Replay later as a test: SSBU_PLAY=play_<name>.rec with scripts/ps_run.ps1.
param([string]$Name = (Get-Date -Format 'yyyyMMdd_HHmm'))
$repo = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$work = "$repo\build\work"
$env:SSBU_ROMFS = "$repo\build\dump\romfs"
$env:SSBU_ERROR_DIALOG = '0'
if (-not $env:SSBU_RENDERER) { $env:SSBU_RENDERER = 'gl' }
$env:SSBU_OS_CONSOLE_CORES = '1'
$env:SSBU_KEYBOARD_PLAYER = '1'                 # keyboard merged into player 1, so it is recorded with the pad
$env:SSBU_REC = "$work\play_$Name.rec"
$p = Start-Process -FilePath "$work\tomodachi_native.exe" -ArgumentList "`"$repo\build\dump\exefs\main.elf`"" -WorkingDirectory $work `
    -RedirectStandardOutput "$work\play_$Name.log" -RedirectStandardError "$work\play_$Name.err" -PassThru
Write-Output ("started pid {0}: recording to play_{1}.rec, log play_{1}.log" -f $p.Id, $Name)
