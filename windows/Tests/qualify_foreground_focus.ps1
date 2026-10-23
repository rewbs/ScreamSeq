# Read-only evidence after Computer Use focuses an observed field in an isolated
# QA process. This script never starts ScreamSeq or supplies foreground input.
param(
    [Parameter(Mandatory=$true)][string]$BuildReceipt,
    [Parameter(Mandatory=$true)][uint32]$QaProcessId,
    [Parameter(Mandatory=$true)][uint64]$MainWindow,
    [uint64]$FieldWindow=0,
    [uint32]$FieldControlId=0,
    [Parameter(Mandatory=$true)][string]$Output
)
$ErrorActionPreference = 'Stop'
if ((!$FieldWindow -and !$FieldControlId) -or ($FieldWindow -and $FieldControlId)) { throw 'Provide one observed field HWND or direct control ID, not both.' }
if (Test-Path -LiteralPath $Output) { throw 'Focus receipt already exists; preserve it.' }
$log = "$Output.log"
if (Test-Path -LiteralPath $log) { throw 'Focus log already exists; preserve it.' }
$build = Get-Content -LiteralPath $BuildReceipt -Raw | ConvertFrom-Json
if ($build.buildExit -ne 0 -or !$build.sourceCommit -or !$build.executableSHA256) { throw 'No successful candidate build receipt.' }
$executable = [IO.Path]::GetFullPath((Join-Path $build.output 'ScreamSeq.exe'))
$probe = Join-Path $build.output 'workspace-restore-tests.exe'
if ((Get-FileHash -LiteralPath $executable -Algorithm SHA256).Hash.ToLowerInvariant() -ne $build.executableSHA256) { throw 'Candidate hash changed.' }
$process = Get-CimInstance Win32_Process -Filter "ProcessId=$QaProcessId"
$privateMode = $process -and ($process.CommandLine -match '(?:^|\s)--inspection(?:\s|$)' -or
    ($process.CommandLine -match '(?:^|\s)--audio-test-silent(?:\s|$)' -and
     $process.CommandLine -match '(?:^|\s)--audio-test-allow-stop(?:\s|$)' -and
     $process.CommandLine -match '(?:^|\s)--recovery-test-directory(?:\s|$)'))
if (!$process -or ![string]::Equals($process.ExecutablePath,$executable,[StringComparison]::OrdinalIgnoreCase) -or
    !$privateMode -or $process.CommandLine -notmatch '(?:^|\s)--automation(?:\s|$)') {
    throw 'Target is not the exact isolated inspection or silent-audio automation candidate.'
}
$probeHash = (Get-FileHash -LiteralPath $probe -Algorithm SHA256).Hash.ToLowerInvariant()
$receipt = [ordered]@{status='started';sourceCommit=$build.sourceCommit;executableSHA256=$build.executableSHA256;
    observerScriptSHA256=(Get-FileHash -LiteralPath $PSCommandPath -Algorithm SHA256).Hash.ToLowerInvariant();
    probeSHA256=$probeHash;pid=$QaProcessId;processCreatedAt=$process.CreationDate;mainWindow=$MainWindow;fieldWindow=$FieldWindow;fieldControlId=$FieldControlId;
    startedAtUTC=[DateTimeOffset]::UtcNow.ToString('o');readOnly=$true}
$receipt | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $Output -Encoding utf8
try {
    if ($FieldControlId) {
        & $probe --inspect-foreground-control-focus $QaProcessId $MainWindow $FieldControlId *> $log
    } else {
        & $probe --inspect-foreground-focus $QaProcessId $MainWindow $FieldWindow *> $log
    }
    if ($LASTEXITCODE -ne 0) { throw 'Exact global/native UIA focus assertion failed; inspect the retained log.' }
    $observation = Get-Content -LiteralPath $log -Raw | ConvertFrom-Json
    if ($observation.status -ne 'passed' -or !$observation.globalFocusExact -or !$observation.nativeFocusExact -or
        !$observation.valuePattern -or !$observation.textPattern -or !$observation.selection -or !$observation.readOnly -or
        $observation.pid -ne $QaProcessId -or $observation.root -ne $MainWindow -or
        ($FieldWindow -and $observation.field -ne $FieldWindow) -or
        ($FieldControlId -and ($observation.controlId -ne $FieldControlId -or !$observation.field))) { throw 'Incomplete focus observation.' }
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
