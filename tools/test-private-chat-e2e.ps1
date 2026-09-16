param(
    [Parameter(Mandatory=$true)][string]$ServerBuild,
    [Parameter(Mandatory=$true)][string]$ClientBuild,
    [Parameter(Mandatory=$true)][string]$RedisServer,
    [Parameter(Mandatory=$true)][string]$QtBin,
    [Parameter(Mandatory=$true)][string]$ConfigPath
)
$ErrorActionPreference = 'Stop'
function Free-Port {
    $listener = [Net.Sockets.TcpListener]::new([Net.IPAddress]::Loopback, 0)
    $listener.Start()
    try { return $listener.LocalEndpoint.Port } finally { $listener.Stop() }
}
function Start-TestProcess($exe, $arguments, $environment) {
    $info = [Diagnostics.ProcessStartInfo]::new()
    $info.FileName = (Resolve-Path -LiteralPath $exe).Path
    $info.WorkingDirectory = Split-Path $info.FileName -Parent
    $info.UseShellExecute = $false; $info.CreateNoWindow = $true
    $info.RedirectStandardInput = $true; $info.RedirectStandardOutput = $true; $info.RedirectStandardError = $true
    foreach ($argument in $arguments) { $info.ArgumentList.Add([string]$argument) }
    foreach ($key in $environment.Keys) { $info.Environment[$key] = [string]$environment[$key] }
    $process = [Diagnostics.Process]::new(); $process.StartInfo = $info
    if (!$process.Start()) { throw 'Test process failed to start' }
    return $process
}
function Wait-Listener($process, $port) {
    $clock = [Diagnostics.Stopwatch]::StartNew()
    while ($clock.Elapsed.TotalSeconds -lt 15) {
        if ($process.HasExited) { throw "Test service exited with code $($process.ExitCode)" }
        $probe = [Net.Sockets.TcpClient]::new()
        try {
            $connecting = $probe.ConnectAsync('127.0.0.1', $port)
            if ($connecting.Wait(150) -and $probe.Connected) { return }
        } catch {} finally { $probe.Dispose() }
        Start-Sleep -Milliseconds 100
    }
    throw 'Test listener did not become ready'
}
$redisPort = Free-Port
$gatePort = Free-Port
while ($gatePort -eq $redisPort) { $gatePort = Free-Port }
$redisPassword = [guid]::NewGuid().ToString('N') + [guid]::NewGuid().ToString('N')
$environment = @{
    SAKURA_CONFIG_PATH = (Resolve-Path -LiteralPath $ConfigPath).Path
    SAKURA_SECURITY_MODE = 'development'
    SAKURA_REDIS_HOST = '127.0.0.1'
    SAKURA_REDIS_PORT = $redisPort
    SAKURA_REDIS_PASSWORD = $redisPassword
    SAKURA_TEST_ISOLATED_REDIS = '1'
    SAKURA_GATE_PORT = $gatePort
}
$redis = $fixture = $gate = $client = $null
$fixtureOutput = $null
$cleanupFailed = $false
try {
    $redis = Start-TestProcess $RedisServer @('--bind','127.0.0.1','--port',"$redisPort",'--save','','--appendonly','no','--requirepass',$redisPassword) @{}
    $redisOut = $redis.StandardOutput.ReadToEndAsync(); $redisErr = $redis.StandardError.ReadToEndAsync()
    Wait-Listener $redis $redisPort
    $fixture = Start-TestProcess (Join-Path $ServerBuild 'Common/privatechat_database_tests.exe') @('--provision-isolated') $environment
    $fixtureErr = $fixture.StandardError.ReadToEndAsync()
    while ($true) {
        $lineTask = $fixture.StandardOutput.ReadLineAsync()
        if (!$lineTask.Wait(30000)) { throw 'Isolated database provisioning timed out' }
        $line = $lineTask.Result
        if ($null -eq $line) { throw 'Isolated database provisioning failed; check local database credentials and privileges' }
        if ($line -match '^READY_SCHEMA=(sakura_private_test_[0-9]+_[0-9]+)$') {
            $environment.SAKURA_MYSQL_SCHEMA = $Matches[1]
            Write-Output "Isolated E2E database: $($Matches[1])"
            break
        }
    }
    $fixtureOutput = $fixture.StandardOutput.ReadToEndAsync()
    $gate = Start-TestProcess (Join-Path $ServerBuild 'GateServer/GateServer.exe') @() $environment
    $gateOut = $gate.StandardOutput.ReadToEndAsync(); $gateErr = $gate.StandardError.ReadToEndAsync()
    Wait-Listener $gate $gatePort
    $clientEnvironment = @{ SAKURA_TEST_ISOLATED_REDIS = '1'; PATH = "$QtBin;$env:PATH" }
    $client = Start-TestProcess (Join-Path $ClientBuild 'privatechat_e2e_tests.exe') @("http://127.0.0.1:$gatePort") $clientEnvironment
    $clientOut = $client.StandardOutput.ReadToEndAsync(); $clientErr = $client.StandardError.ReadToEndAsync()
    if (!$client.WaitForExit(60000)) { throw 'Client E2E test timed out' }
    $clientOut.Result; $clientErr.Result
    if ($client.ExitCode -ne 0) { throw "Client E2E test failed with code $($client.ExitCode)" }
} finally {
    if ($client -and !$client.HasExited) { $client.Kill($true); $client.WaitForExit() }
    if ($gate -and !$gate.HasExited) { $gate.Kill($true); $gate.WaitForExit() }
    if ($fixture) {
        if (!$fixture.HasExited) {
            $fixture.StandardInput.WriteLine('finish'); $fixture.StandardInput.Flush()
            if (!$fixture.WaitForExit(15000)) {
                $cleanupFailed = $true
                Write-Warning "Database cleanup still running for $($environment.SAKURA_MYSQL_SCHEMA); fixture PID $($fixture.Id)"
            }
        }
        if ($fixture.HasExited -and $fixtureOutput) { $fixtureOutput.GetAwaiter().GetResult() }
        if ($fixture.HasExited -and $fixture.ExitCode -ne 0) {
            $cleanupFailed = $true
            Write-Warning "Database fixture reported failure ($($fixture.ExitCode)); verify cleanup of $($environment.SAKURA_MYSQL_SCHEMA)."
            $fixtureErr.GetAwaiter().GetResult()
        }
    }
    if ($redis -and !$redis.HasExited) { $redis.Kill($true); $redis.WaitForExit() }
}
if ($cleanupFailed) { throw 'Isolated test cleanup did not complete successfully' }
