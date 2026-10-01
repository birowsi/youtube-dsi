param([int]$TestStallMs = 0)
$taskRoot = Split-Path -Parent $PSScriptRoot
$taskPython = 'C:\Users\hanbi\AppData\Local\Programs\Python\Python311\python.exe'
$taskPidFile = Join-Path $taskRoot 'server\quality-server.pid'
$taskListeners = Get-NetTCPConnection -LocalPort 8767 -State Listen -ErrorAction SilentlyContinue
if ($taskListeners) {
    if (-not (Test-Path -LiteralPath $taskPidFile)) { throw 'Unowned HQ listener' }
    $taskPid = [int](Get-Content -LiteralPath $taskPidFile)
    if (@($taskListeners.OwningProcess | Select-Object -Unique).Count -ne 1 -or $taskListeners[0].OwningProcess -ne $taskPid) { throw 'HQ listener PID differs' }
    if ((Get-Process -Id $taskPid).Path -ne $taskPython) { throw 'HQ process is not our Python runtime' }
    Stop-Process -Id $taskPid
}
$taskArgs = @((Join-Path $taskRoot 'server\server_quality.py'),'--bind','192.168.0.4','--port','8767','--test-stall-ms',$TestStallMs)
$taskService = Start-Process -FilePath $taskPython -ArgumentList $taskArgs -WorkingDirectory $taskRoot -WindowStyle Hidden -RedirectStandardOutput (Join-Path $taskRoot 'server\quality-stdout.log') -RedirectStandardError (Join-Path $taskRoot 'server\quality-stderr.log') -PassThru
Set-Content -LiteralPath $taskPidFile -Value $taskService.Id
Write-Output ('Started HQ relay PID ' + $taskService.Id + ', stall ms ' + $TestStallMs)
