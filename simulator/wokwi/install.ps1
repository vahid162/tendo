#requires -Version 5.1
<#
.SYNOPSIS
Prepares the Tando simulator on Windows without administrator access.
.DESCRIPTION
Run from any directory. Only VS Code must already be installed. The default
checkout is USERPROFILE\Documents\Tando-Wokwi\tendo. Downloads are pinned and
verified before execution. Wokwi credentials are temporary and never saved.
SkipSimulation prepares a GUI-only workspace and returns exit code 2.
#>
[CmdletBinding()]
param(
    [string]$InstallPath,
    [string]$ToolsRoot = (Join-Path $env:LOCALAPPDATA 'Tando-Wokwi'),
    [ValidatePattern('^[A-Za-z0-9][A-Za-z0-9._/-]*$')]
    [string]$Branch = 'develop',
    [switch]$NoUpdate,
    [switch]$SkipSimulation,
    [switch]$NoOpen
)

$script:TandoBranch = $Branch
$script:TandoSecret = $null

function Protect-TandoText([string]$Text) {
    if ($script:TandoSecret) { $Text = $Text.Replace($script:TandoSecret, '[REDACTED]') }
    return $Text
}

function Invoke-TandoCommand {
    param([string]$FilePath, [string[]]$Arguments, [switch]$Capture)
    if (!(Test-Path -LiteralPath $FilePath)) { throw "Command is missing: $FilePath" }
    $previousPreference = $ErrorActionPreference
    try {
        # Windows PowerShell turns native stderr into ErrorRecords; exit status is authoritative.
        $ErrorActionPreference = 'Continue'
        $output = @(& $FilePath @Arguments 2>&1)
        $commandExit = $LASTEXITCODE
    } finally { $ErrorActionPreference = $previousPreference }
    $safeOutput = Protect-TandoText (($output | ForEach-Object { $_.ToString() }) -join "`n")
    if ($commandExit -ne 0) {
        throw "Command failed (exit $commandExit): $FilePath`n$safeOutput"
    }
    if ($Capture) { return $safeOutput.Trim() }
    if ($safeOutput) { Write-Host $safeOutput }
}

function Assert-TandoHost {
    param(
        [version]$Version = [Environment]::OSVersion.Version,
        [string]$Architecture = $env:PROCESSOR_ARCHITECTURE,
        [version]$PowerShellVersion = $PSVersionTable.PSVersion
    )
    if ($env:OS -ne 'Windows_NT' -or $Version.Major -lt 10) { throw 'Windows 10 or 11 is required.' }
    if ($Architecture -ne 'AMD64' -or ![Environment]::Is64BitProcess) {
        throw 'Use x64 PowerShell on x64 Windows. ARM64 and 32-bit toolchains are not verified.'
    }
    if ($PowerShellVersion -lt [version]'5.1') { throw 'PowerShell 5.1 or newer is required.' }
}

function Get-TandoCommandPath([string]$Name) {
    $command = Get-Command $Name -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($command) { return $command.Source }
    return $null
}

function Find-TandoCode {
    $candidates = @(
        (Get-TandoCommandPath 'code.cmd'),
        (Join-Path $env:LOCALAPPDATA 'Programs\Microsoft VS Code\bin\code.cmd'),
        (Join-Path $env:ProgramFiles 'Microsoft VS Code\bin\code.cmd')
    )
    foreach ($candidate in $candidates) {
        if ($candidate -and (Test-Path -LiteralPath $candidate)) { return $candidate }
    }
    throw 'Visual Studio Code was not found. Install VS Code from https://code.visualstudio.com/ and rerun. This installer does not install VS Code.'
}

function Get-TandoToolVersion([string]$Executable, [string[]]$Arguments = @('--version')) {
    if (!$Executable -or !(Test-Path -LiteralPath $Executable)) { return $null }
    try {
        $value = Invoke-TandoCommand $Executable $Arguments -Capture
        if ($value -match '(\d+\.\d+\.\d+)') { return [version]$Matches[1] }
    } catch { return $null }
    return $null
}

function Get-TandoDownload {
    param([string]$Uri, [string]$Path, [string]$Sha256)
    if ((Test-Path -LiteralPath $Path) -and (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash -eq $Sha256) { return }
    $parent = Split-Path $Path -Parent
    New-Item -ItemType Directory -Path $parent -Force | Out-Null
    $partial = $Path + '.partial'
    try {
        Invoke-WebRequest -UseBasicParsing -Uri $Uri -OutFile $partial -TimeoutSec 180
        if ((Get-FileHash -LiteralPath $partial -Algorithm SHA256).Hash -ne $Sha256) {
            throw "SHA-256 mismatch: $Uri. The download was not executed; rerun after checking your network."
        }
        Move-Item -LiteralPath $partial -Destination $Path -Force
    } catch {
        throw "Download failed: $Uri. Check internet/proxy access and retry. $(Protect-TandoText $_.Exception.Message)"
    } finally {
        if (Test-Path -LiteralPath $partial) { Remove-Item -LiteralPath $partial -Force }
    }
}

function Ensure-TandoGit([string]$Root) {
    $git = Get-TandoCommandPath 'git.exe'
    $version = Get-TandoToolVersion $git
    if ($version -and $version -ge [version]'2.30.0') { return $git }
    $git = Join-Path $Root 'git\cmd\git.exe'
    $version = Get-TandoToolVersion $git
    if ($version -and $version -ge [version]'2.30.0') { return $git }
    $archive = Join-Path $Root 'downloads\MinGit-2.56.0.3-64-bit.zip'
    Get-TandoDownload -Uri 'https://github.com/git-for-windows/git/releases/download/v2.56.0.windows.3/MinGit-2.56.0.3-64-bit.zip' -Path $archive -Sha256 '2e5619966e588fde5f8122c5557cad50c0fbafcaed736689f40ec1dfb1d78ea7'
    Expand-Archive -LiteralPath $archive -DestinationPath (Join-Path $Root 'git') -Force
    if (!(Test-Path -LiteralPath $git)) { throw 'Git installation failed. Check free disk space and rerun.' }
    return $git
}

function Ensure-TandoPython([string]$Root) {
    $candidates = @((Join-Path $Root 'python\python.exe'), (Get-TandoCommandPath 'python.exe'))
    $pythonRegistry = 'HKCU:\Software\Python\PythonCore'
    if (Test-Path $pythonRegistry) {
        foreach ($item in Get-ChildItem $pythonRegistry -ErrorAction SilentlyContinue) {
            $installKey = Join-Path $item.PSPath 'InstallPath'
            $registered = Get-ItemProperty $installKey -ErrorAction SilentlyContinue
            if ($registered -and $registered.ExecutablePath) { $candidates += $registered.ExecutablePath }
        }
    }
    foreach ($python in $candidates) {
        if ($python -and $python -notmatch '\\WindowsApps\\') {
            $version = Get-TandoToolVersion $python
            if ($version -and $version -ge [version]'3.10.0' -and $version -lt [version]'3.14.0') {
                try { Invoke-TandoCommand $python @('-c','import venv, ensurepip') -Capture | Out-Null; return $python } catch {}
            }
        }
    }
    $setup = Join-Path $Root 'downloads\python-3.13.17-amd64.exe'
    Get-TandoDownload -Uri 'https://www.python.org/ftp/python/3.13.17/python-3.13.17-amd64.exe' -Path $setup -Sha256 '5da012101ba2e17113a9a1cd901e9ce1441086f2b7ecb5de06ec94163899a190'
    $signature = Get-AuthenticodeSignature -LiteralPath $setup
    if ($signature.Status -ne 'Valid' -or $signature.SignerCertificate.Subject -notmatch 'Python Software Foundation') {
        throw 'Python installer signature is not valid for Python Software Foundation. Check the system clock and retry.'
    }
    $target = Join-Path $Root 'python'
    $process = Start-Process -FilePath $setup -ArgumentList @('/quiet','InstallAllUsers=0','Include_launcher=0','Include_test=0','PrependPath=0',('TargetDir="' + $target + '"')) -WindowStyle Hidden -Wait -PassThru
    if ($process.ExitCode -eq 3010) { throw 'Python installation requires a Windows restart. Restart and rerun this command.' }
    if ($process.ExitCode -ne 0) { throw "Python installation failed (exit $($process.ExitCode)). Rerun or inspect Windows installer diagnostics." }
    $python = Join-Path $target 'python.exe'
    if (!(Get-TandoToolVersion $python)) { throw 'Python installation did not produce a working interpreter.' }
    return $python
}

function Ensure-TandoWokwi([string]$Root) {
    $wokwi = Get-TandoCommandPath 'wokwi-cli.exe'
    $version = Get-TandoToolVersion $wokwi
    if ($version -and $version -ge [version]'0.29.1') { return $wokwi }
    $wokwi = Join-Path $Root 'wokwi\wokwi-cli.exe'
    $version = Get-TandoToolVersion $wokwi
    if (!$version -or $version -lt [version]'0.29.1') {
        Get-TandoDownload -Uri 'https://github.com/wokwi/wokwi-cli/releases/download/v0.29.1/wokwi-cli-win-x64.exe' -Path $wokwi -Sha256 '45292e3c4f32ba4cfbcc7a457a6df48aad95fdb3269a218cb83423da9dc483b2'
    }
    if (!(Get-TandoToolVersion $wokwi)) { throw 'Wokwi CLI is not executable. Check antivirus/quarantine and rerun.' }
    return $wokwi
}

function Initialize-TandoRepository {
    param([string]$Git, [string]$Path, [switch]$NoUpdate,
          [ValidatePattern('^[A-Za-z0-9][A-Za-z0-9._/-]*$')][string]$Branch = $script:TandoBranch)
    Invoke-TandoCommand $Git @('check-ref-format','--branch',$Branch) -Capture | Out-Null
    $Path = [IO.Path]::GetFullPath($Path)
    if (!(Test-Path -LiteralPath $Path)) {
        New-Item -ItemType Directory -Path (Split-Path $Path -Parent) -Force | Out-Null
        Invoke-TandoCommand $Git @('clone','--branch',$Branch,'--single-branch','https://github.com/vahid162/tendo.git',$Path)
    }
    $top = Invoke-TandoCommand $Git @('-C',$Path,'rev-parse','--show-toplevel') -Capture
    if ($top.TrimEnd('/','\').Replace('/','\') -ne $Path.TrimEnd('\')) {
        throw 'The selected path is not a repository root. Choose a new empty path or the actual tendo checkout.'
    }
    $origin = Invoke-TandoCommand $Git @('-C',$Path,'remote','get-url','origin') -Capture
    if ($origin -notmatch '^(https://github\.com/vahid162/tendo(?:\.git)?/?|git@github\.com:vahid162/tendo(?:\.git)?)$') {
        throw 'The selected Git origin is not vahid162/tendo. Choose the correct repository; no files were changed.'
    }
    $currentBranch = Invoke-TandoCommand $Git @('-C',$Path,'branch','--show-current') -Capture
    if ($currentBranch -ne $Branch) { throw "Expected branch $Branch. Switch it manually after saving your work, or choose another installation path." }
    $dirty = [bool](Invoke-TandoCommand $Git @('-C',$Path,'status','--porcelain') -Capture)
    if ($dirty) {
        Write-Host '[WARN] Local changes detected: using your current source without updating. Commit your work before requesting an update.'
    } elseif (!$NoUpdate) {
        Invoke-TandoCommand $Git @('-C',$Path,'fetch','origin',$Branch)
        try { Invoke-TandoCommand $Git @('-C',$Path,'merge','--ff-only','FETCH_HEAD') }
        catch { throw 'Git update could not fast-forward. Your commits are preserved. Resolve divergence manually or rerun with -NoUpdate to build the current checkout.' }
    }
    $commit = Invoke-TandoCommand $Git @('-C',$Path,'rev-parse','HEAD') -Capture
    return @{ Path=$Path; Commit=$commit; Dirty=$dirty; Branch=$currentBranch }
}

function Ensure-TandoBuildEnvironment([string]$Python, [string]$Simulator) {
    $venv = Join-Path $Simulator '.venv'
    $venvPython = Join-Path $venv 'Scripts\python.exe'
    if (!(Test-Path -LiteralPath $venvPython)) { Invoke-TandoCommand $Python @('-m','venv',$venv) }
    $packagesReady = $false
    try {
        Invoke-TandoCommand $venvPython @('-c',"import importlib.metadata as m; assert m.version('platformio')=='6.2.0'; assert m.version('Pillow')=='11.3.0'") -Capture | Out-Null
        $packagesReady = $true
    } catch {}
    if (!$packagesReady) { Invoke-TandoCommand $venvPython @('-m','pip','install','--disable-pip-version-check','platformio==6.2.0','Pillow==11.3.0') }
    return $venvPython
}

function Assert-TandoBinary([string]$Path, [byte[]]$Magic) {
    if (!(Test-Path -LiteralPath $Path)) { throw "Required build output is missing: $Path. Rerun the failed build stage." }
    $stream = [IO.File]::OpenRead($Path)
    try {
        if ($stream.Length -lt 8) { throw "Build output is invalid: $Path" }
        foreach ($value in $Magic) { if ($stream.ReadByte() -ne $value) { throw "Build output is invalid: $Path" } }
    } finally { $stream.Dispose() }
}

function Read-TandoToken {
    if ($env:WOKWI_CLI_TOKEN) { return $env:WOKWI_CLI_TOKEN }
    Write-Host 'Get a personal token at https://wokwi.com/dashboard/ci. This is separate from the VS Code license.'
    Write-Host 'The token is used only for this test. Leave blank to prepare VS Code with cloud/visual checks UNVERIFIED.'
    $secureToken = Read-Host 'Wokwi API Token' -AsSecureString
    $pointer = [Runtime.InteropServices.Marshal]::SecureStringToBSTR($secureToken)
    try { return [Runtime.InteropServices.Marshal]::PtrToStringBSTR($pointer) }
    finally { [Runtime.InteropServices.Marshal]::ZeroFreeBSTR($pointer); $secureToken.Dispose() }
}

function Invoke-TandoSimulation {
    param([string]$Wokwi, [string]$Python, [string]$Simulator, [string]$Version, [string]$Token)
    $priorToken = $env:WOKWI_CLI_TOKEN
    $script:TandoSecret = $Token
    $serialLog = Join-Path $Simulator 'sim-smoke.log'
    $screenshots = Join-Path $Simulator 'screenshots'
    try {
        New-Item -ItemType Directory -Path $screenshots -Force | Out-Null
        foreach ($name in @('left-idle.png','right-idle.png','left-pet.png','right-pet.png')) {
            $generated = Join-Path $screenshots $name
            if (Test-Path -LiteralPath $generated) { Remove-Item -LiteralPath $generated -Force }
        }
        if (Test-Path -LiteralPath $serialLog) { Remove-Item -LiteralPath $serialLog -Force }
        $env:WOKWI_CLI_TOKEN = $Token
        Push-Location $Simulator
        try { Invoke-TandoCommand $Wokwi @('.','--scenario','tests/smoke.yaml','--timeout','30000','--serial-log-file',$serialLog) }
        finally { Pop-Location }
        Invoke-TandoCommand $Python @((Join-Path $Simulator 'tests\verify_simulation.py'),'--serial-log',$serialLog,'--screenshots',$screenshots,'--version',$Version)
    } catch {
        $message = Protect-TandoText $_.Exception.Message
        if ($message -match '(?i)401|403|unauthori[sz]ed|invalid.{0,20}token|forbidden') {
            throw 'Wokwi token/access rejected. Obtain a valid personal CI token, check account access/quota, then rerun. VS Code activation is separate.'
        }
        throw "Wokwi runtime/visual check failed: $message. Check sim-smoke.log and screenshots; the two eyes are NOT verified."
    } finally {
        $env:WOKWI_CLI_TOKEN = $priorToken
        try {
            if (Test-Path -LiteralPath $serialLog) {
                $safeLog = Protect-TandoText ([IO.File]::ReadAllText($serialLog))
                [IO.File]::WriteAllText($serialLog, $safeLog)
            }
        } finally { $script:TandoSecret = $null }
    }
}

function Invoke-TandoInstall {
    param([string]$InstallPath, [string]$ToolsRoot, [switch]$NoUpdate, [switch]$SkipSimulation, [switch]$NoOpen,
          [ValidatePattern('^[A-Za-z0-9][A-Za-z0-9._/-]*$')][string]$Branch = $script:TandoBranch)
    $ErrorActionPreference = 'Stop'
    $report = [ordered]@{ Status='failed'; Stage='host'; FailedStage=$null; Firmware=$null; Commit=$null; Dirty=$null; Branch=$null; Project=$null; Tools=@{}; Runtime='unverified'; Images='unverified'; VisualConfirmation='unverified'; VSCode='unverified'; License='manual activation/status unverified'; Failure=$null; Recovery=$null }
    $reportPath = $null
    $exitCode = 1
    try {
        Assert-TandoHost
        [Net.ServicePointManager]::SecurityProtocol = [Net.ServicePointManager]::SecurityProtocol -bor [Net.SecurityProtocolType]::Tls12
        $code = Find-TandoCode
        Invoke-TandoCommand $code @('--version')
        Write-Host '[OK] Visual Studio Code detected'
        if (!$InstallPath) {
            $defaultPath = Join-Path $env:USERPROFILE 'Documents\Tando-Wokwi\tendo'
            $answer = Read-Host "Repository path [Enter = $defaultPath]"
            $InstallPath = $defaultPath
            if ($answer) { $InstallPath = $answer.Trim('"') }
        }
        $ToolsRoot = [IO.Path]::GetFullPath($ToolsRoot)
        $report.Stage = 'Git'
        $git = Ensure-TandoGit $ToolsRoot
        $report.Tools.Git = @{Path=$git; Version=(Get-TandoToolVersion $git).ToString()}
        Write-Host '[OK] Git ready'
        $report.Stage = 'repository'
        $repository = Initialize-TandoRepository -Git $git -Path $InstallPath -NoUpdate:$NoUpdate -Branch $Branch
        $simulator = Join-Path $repository.Path 'simulator\wokwi'
        $reportPath = Join-Path $simulator '.tando-install.json'
        $report.Project = $repository.Path; $report.Commit=$repository.Commit; $report.Dirty=$repository.Dirty; $report.Branch=$repository.Branch
        $report.Firmware = (Get-Content -LiteralPath (Join-Path $repository.Path 'VERSION') -Raw).Trim()
        Write-Host "[OK] Repository ready at $($report.Commit)"
        $report.Stage = 'Python'
        $python = Ensure-TandoPython $ToolsRoot
        $report.Tools.Python = @{Path=$python; Version=(Get-TandoToolVersion $python).ToString()}
        Write-Host '[OK] Python ready'
        $report.Stage = 'PlatformIO'
        $buildPython = Ensure-TandoBuildEnvironment -Python $python -Simulator $simulator
        $report.Tools.PlatformIO = @{Path=$buildPython; Version='6.2.0'}
        Write-Host '[OK] PlatformIO ready'
        $report.Stage = 'Wokwi CLI'
        $wokwi = Ensure-TandoWokwi $ToolsRoot
        $report.Tools.Wokwi = @{Path=$wokwi; Version=(Get-TandoToolVersion $wokwi).ToString()}
        Write-Host '[OK] Wokwi CLI ready'
        $report.Stage = 'VS Code extension'
        $extensions = Invoke-TandoCommand $code @('--list-extensions') -Capture
        if (($extensions -split '\r?\n') -notcontains 'Wokwi.wokwi-vscode') {
            Invoke-TandoCommand $code @('--install-extension','Wokwi.wokwi-vscode')
            $extensions = Invoke-TandoCommand $code @('--list-extensions') -Capture
            if (($extensions -split '\r?\n') -notcontains 'Wokwi.wokwi-vscode') { throw 'Wokwi extension installation could not be verified. Check VS Code Marketplace/network access and retry.' }
        }
        Write-Host '[OK] Wokwi extension installed/detected (license status unverified)'
        $report.Stage = 'structural tests'
        Invoke-TandoCommand $buildPython @((Join-Path $simulator 'tests\test_project.py'))
        $report.Stage = 'diagram lint'
        Invoke-TandoCommand $wokwi @('lint',$simulator)
        $report.Stage = 'firmware build'
        Invoke-TandoCommand $buildPython @('-m','platformio','run','--project-dir',$simulator)
        $firmwareDir = Join-Path $simulator '.pio\build\tando-s3-sim'
        Assert-TandoBinary (Join-Path $firmwareDir 'firmware.bin') ([byte[]](0xe9))
        Assert-TandoBinary (Join-Path $firmwareDir 'firmware.elf') ([byte[]](0x7f,0x45,0x4c,0x46))
        Write-Host '[OK] Firmware compiled and binary signatures verified'
        $report.Stage = 'custom chip build'
        Push-Location $simulator
        try { Invoke-TandoCommand $wokwi @('chip','compile','gc9a01.chip.c','-o','gc9a01.chip.wasm') }
        finally { Pop-Location }
        Assert-TandoBinary (Join-Path $simulator 'gc9a01.chip.wasm') ([byte[]](0,0x61,0x73,0x6d))
        Write-Host '[OK] GC9A01 custom chip compiled and WASM signature verified'
        $report.Status='prepared'; $report.Stage='simulation'; $exitCode=2
        if (!$SkipSimulation) {
            $token = Read-TandoToken
            if ($token) {
                try {
                    Invoke-TandoSimulation -Wokwi $wokwi -Python $buildPython -Simulator $simulator -Version $report.Firmware -Token $token
                    $report.Runtime='passed'; $report.Images='automated checks passed'; $report.Status='image-checks-passed'; $exitCode=0
                    Write-Host '[OK] Serial boot/events and automated screenshot checks passed. Inspect both eyes before claiming visual correctness.'
                } catch { $report.Failure=$_.Exception.Message; $report.FailedStage='simulation'; $report.Status='verification-failed'; $exitCode=1; Write-Host ('[FAIL] ' + $report.Failure) }
                finally { $token=$null }
            }
        }
        if ($report.Runtime -eq 'unverified') { Write-Host '[WARN] Cloud runtime and two-eye visuals are UNVERIFIED.' }
        $report.Stage='VS Code open'
        if (!$NoOpen) {
            Invoke-TandoCommand $code @('--new-window',$simulator)
            $report.VSCode='open requested through CLI'; Write-Host '[OK] VS Code project open requested'
        }
        $report.Stage='complete'
        $report.Recovery='In VS Code: F1 > Wokwi: Request a new License (if needed), complete the browser flow, then F1 > Wokwi: Start Simulator. Use p, 1, 2, s and b in the Serial Monitor. Rerun install.ps1 to rebuild/retry.'
    } catch {
        $report.FailedStage=$report.Stage
        $report.Failure=Protect-TandoText $_.Exception.Message
        $report.Recovery='Resolve the reported stage, preserve local edits, and rerun the same command. Git divergence: resolve manually or use -NoUpdate. Dependencies: check internet/proxy, disk space and antivirus.'
        Write-Host ("[FAIL] Stage $($report.Stage): " + $report.Failure)
        $exitCode=1
    } finally {
        if ($reportPath) { $report | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $reportPath -Encoding UTF8 }
        Write-Host ('Firmware: ' + $report.Firmware + ' | Branch: ' + $report.Branch + ' | Commit: ' + $report.Commit)
        Write-Host ('Project: ' + $report.Project)
        Write-Host ('Status: ' + $report.Status + ' | Runtime: ' + $report.Runtime + ' | Images: ' + $report.Images)
        if ($report.FailedStage) { Write-Host ('Failed stage: ' + $report.FailedStage) }
        Write-Host ('Visual confirmation: ' + $report.VisualConfirmation + ' | Wokwi VS Code license: ' + $report.License)
        if ($report.Project) { Write-Host ('Evidence: ' + (Join-Path $report.Project 'simulator\wokwi\screenshots') + '; sim-smoke.log; .tando-install.json') }
        Write-Host $report.Recovery
    }
    return $exitCode
}

if ($MyInvocation.InvocationName -ne '.') {
    exit (Invoke-TandoInstall -InstallPath $InstallPath -ToolsRoot $ToolsRoot -NoUpdate:$NoUpdate -SkipSimulation:$SkipSimulation -NoOpen:$NoOpen -Branch $Branch)
}
