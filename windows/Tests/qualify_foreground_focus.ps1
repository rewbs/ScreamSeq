# Read-only evidence after Computer Use focuses an observed field in an isolated
# QA process. This script never starts ScreamSeq or supplies foreground input.
param(
    [Parameter(Mandatory=$true)][string]$BuildReceipt,
    [Parameter(Mandatory=$true)][uint32]$QaProcessId,
    [Parameter(Mandatory=$true)][uint64]$MainWindow,
    [Parameter(Mandatory=$true)][uint64]$FieldWindow,
    [Parameter(Mandatory=$true)][string]$Output
)
$ErrorActionPreference = 'Stop'
if (Test-Path -LiteralPath $Output) { throw 'Focus receipt already exists; preserve it.' }
$log = "$Output.log"
if (Test-Path -LiteralPath $log) { throw 'Focus log already exists; preserve it.' }
$build = Get-Content -LiteralPath $BuildReceipt -Raw | ConvertFrom-Json
if ($build.buildExit -ne 0 -or !$build.sourceCommit -or !$build.executableSHA256) { throw 'No successful candidate build receipt.' }
$executable = [IO.Path]::GetFullPath((Join-Path $build.output 'ScreamSeq.exe'))
$probe = Join-Path $build.output 'workspace-restore-tests.exe'
if ((Get-FileHash -LiteralPath $executable -Algorithm SHA256).Hash.ToLowerInvariant() -ne $build.executableSHA256) { throw 'Candidate hash changed.' }
$process = Get-CimInstance Win32_Process -Filter "ProcessId=$QaProcessId"
if (!$process -or ![string]::Equals($process.ExecutablePath,$executable,[StringComparison]::OrdinalIgnoreCase) -or
    $process.CommandLine -notmatch '(?:^|\s)--inspection(?:\s|$)' -or $process.CommandLine -notmatch '(?:^|\s)--automation(?:\s|$)') {
    throw 'Target is not the exact isolated inspection/automation candidate.'
}
$probeHash = (Get-FileHash -LiteralPath $probe -Algorithm SHA256).Hash.ToLowerInvariant()
$receipt = [ordered]@{status='started';sourceCommit=$build.sourceCommit;executableSHA256=$build.executableSHA256;
    probeSHA256=$probeHash;pid=$QaProcessId;processCreatedAt=$process.CreationDate;mainWindow=$MainWindow;fieldWindow=$FieldWindow;
    startedAtUTC=[DateTimeOffset]::UtcNow.ToString('o');readOnly=$true}
$receipt | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $Output -Encoding utf8
try {
    & $probe --inspect-foreground-focus $QaProcessId $MainWindow $FieldWindow *> $log
    if ($LASTEXITCODE -ne 0) { throw 'Exact global/native UIA focus assertion failed; inspect the retained log.' }
    $observation = Get-Content -LiteralPath $log -Raw | ConvertFrom-Json
    if ($observation.status -ne 'passed' -or !$observation.globalFocusExact -or !$observation.nativeFocusExact -or
        !$observation.valuePattern -or !$observation.textPattern -or !$observation.selection -or !$observation.readOnly -or
        $observation.pid -ne $QaProcessId -or $observation.root -ne $MainWindow -or $observation.field -ne $FieldWindow) { throw 'Incomplete focus observation.' }
    $after = Get-CimInstance Win32_Process -Filter "ProcessId=$QaProcessId"
    if (!$after -or $after.CreationDate -ne $process.CreationDate -or
        (Get-FileHash -LiteralPath $executable -Algorithm SHA256).Hash.ToLowerInvariant() -ne $build.executableSHA256 -or
        (Get-FileHash -LiteralPath $probe -Algorithm SHA256).Hash.ToLowerInvariant() -ne $probeHash) { throw 'QA process or binary identity changed.' }
    $receipt.observation=$observation;$receipt.status='passed'
} catch {
    $receipt.status='failed';$receipt.error=$_.Exception.Message
    throw
} finally {
    $receipt.completedAtUTC=[DateTimeOffset]::UtcNow.ToString('o')
    $receipt | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $Output -Encoding utf8
}
