# Run the built game for N seconds from PowerShell (no shell wrapper between us and the process: the real exit code).
#   powershell -File scripts/ps_run.ps1 <log name> [seconds]     (extra env such as TOMO_SAMPLE=1 set by the caller)
param([string]$Log = 'run.log', [int]$Seconds = 40)
$repo = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
Set-Location "$repo\build\work"
if (-not $env:SSBU_ROMFS) { $env:SSBU_ROMFS = "$repo\build\dump\romfs" }
if (-not $env:SSBU_ERROR_DIALOG) { $env:SSBU_ERROR_DIALOG = '0' }
if (-not $env:SSBU_RENDERER) { $env:SSBU_RENDERER = 'gl' }
if (-not $env:SSBU_OS_CONSOLE_CORES) { $env:SSBU_OS_CONSOLE_CORES = '1' }
$psi = New-Object System.Diagnostics.ProcessStartInfo
$psi.FileName = "$repo\build\work\tomodachi_native.exe"
$psi.Arguments = "`"$repo\build\dump\exefs\main.elf`" $Seconds"
$psi.UseShellExecute = $false
$psi.RedirectStandardOutput = $true
$psi.RedirectStandardError = $true
$p = [System.Diagnostics.Process]::Start($psi)
$out = $p.StandardOutput.ReadToEndAsync()
$err = $p.StandardError.ReadToEndAsync()
if (-not $p.WaitForExit(($Seconds + 90) * 1000)) { Write-Output "still running after the limit: killed"; $p.Kill() }
$p.WaitForExit()
[IO.File]::WriteAllText("$repo\build\work\$Log", $out.Result + $err.Result + "`nexit code $($p.ExitCode) (0x$('{0:X8}' -f $p.ExitCode))`n")
Write-Output ("exit code {0} (0x{1:X8}), {2} log lines" -f $p.ExitCode, $p.ExitCode, ($out.Result -split "`n").Count)
