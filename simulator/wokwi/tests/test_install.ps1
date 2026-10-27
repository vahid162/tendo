# No Pester is required. Installations are stubbed; Git and Python probes use isolated fixtures.
$ErrorActionPreference = 'Stop'
$installer = Join-Path (Split-Path $PSScriptRoot -Parent) 'install.ps1'
if (!(Test-Path -LiteralPath $installer)) { throw 'Installer does not exist yet' }
. $installer

function Assert-True($Value, [string]$Message) { if (!$Value) { throw $Message } }
function Assert-Equal($Actual, $Expected) { if ($Actual -ne $Expected) { throw "Expected [$Expected], got [$Actual]" } }
function Assert-Throws([scriptblock]$Action, [string]$Pattern) {
    try { & $Action } catch { if ($_.Exception.Message -match $Pattern) { return }; throw }
    throw "Expected failure matching $Pattern"
}
$passed = 0
function Test-Case([string]$Name, [scriptblock]$Action) {
    & $Action
    $script:passed++
    Write-Host "[PASS] $Name"
}

Test-Case 'Reject unsupported hosts before installing anything' {
    Assert-Throws { Assert-TandoHost -Version ([version]'6.3') -Architecture 'AMD64' -PowerShellVersion ([version]'5.1') } 'Windows 10'
    Assert-Throws { Assert-TandoHost -Version ([version]'10.0') -Architecture 'ARM64' -PowerShellVersion ([version]'7.4') } 'x64'
    Assert-TandoHost -Version ([version]'10.0') -Architecture 'AMD64' -PowerShellVersion ([version]'5.1')
}
Test-Case 'Missing VS Code stops without installing VS Code' {
    function Get-Command { return $null }
    function Test-Path { return $false }
    Assert-Throws { Find-TandoCode } 'Visual Studio Code'
}
Test-Case 'Dependency reuse never downloads an existing compatible tool' {
    $script:downloads = 0
    function Get-TandoToolVersion { return [version]'2.56.0' }
    function Get-TandoCommandPath { return 'already-installed-git.exe' }
    function Get-TandoDownload { $script:downloads++ }
    Assert-Equal (Ensure-TandoGit -Root 'unused') 'already-installed-git.exe'
    Assert-Equal $script:downloads 0
}
Test-Case 'Missing tools choose user-owned locations and verified downloads' {
    $script:downloads = @()
    function Get-TandoToolVersion { return $null }
    function Get-TandoCommandPath { return $null }
    function Get-TandoDownload { param($Uri,$Path,$Sha256); $script:downloads += @{Uri=$Uri;Path=$Path;Hash=$Sha256} }
    function New-Item {}
    function Expand-Archive {}
    function Test-Path { return $true }
    $git = Ensure-TandoGit -Root 'C:\user-tools'
    Assert-True ($git.StartsWith('C:\user-tools')) 'Git must stay in the supplied user directory'
    Assert-Equal $script:downloads.Count 1
    Assert-True ($script:downloads[0].Hash -match '^[0-9a-f]{64}$') 'Download needs a pinned SHA256'
}
Test-Case 'Downloaded content with the wrong hash is never accepted' {
    $dir = Join-Path $env:TEMP ('tando-hash-test-' + [guid]::NewGuid().ToString('N'))
    New-Item -ItemType Directory -Path $dir | Out-Null
    $path = Join-Path $dir 'download.bin'
    [IO.File]::WriteAllText($path, 'wrong bytes')
    function Invoke-WebRequest { param($Uri,$OutFile); [IO.File]::WriteAllText($OutFile, 'wrong bytes') }
    Assert-Throws { Get-TandoDownload -Uri 'https://example.invalid/tool' -Path $path -Sha256 ('0' * 64) } 'SHA-256'
    Remove-Item -LiteralPath $path
    Remove-Item -LiteralPath $dir
}
Test-Case 'Dirty repository is preserved and no fetch or merge is performed' {
    $script:gitCalls = @()
    function Invoke-TandoCommand {
        param($FilePath,$Arguments,[switch]$Capture)
        $script:gitCalls += ($Arguments -join ' ')
        if ($Arguments -contains '--show-toplevel') { return 'C:/project' }
        if ($Arguments -contains 'get-url') { return 'https://github.com/vahid162/tendo.git' }
        if ($Arguments -contains '--show-current') { return 'feature/wokwi-dual-gc9a01' }
        if ($Arguments -contains '--porcelain') { return ' M sketch.ino' }
        if ($Arguments -contains 'HEAD') { return ('a' * 40) }
    }
    function Test-Path { return $true }
    $state = Initialize-TandoRepository -Git 'git' -Path 'C:\project'
    Assert-True $state.Dirty 'Dirty source must be reported'
    Assert-True (!(($script:gitCalls -join ' ') -match 'fetch|merge|reset|clean|stash')) 'Dirty source must never be changed by Git'
}
Test-Case 'Wrong origin is rejected before repository updates' {
    function Test-Path { return $true }
    function Invoke-TandoCommand {
        param($FilePath,$Arguments,[switch]$Capture)
        if ($Arguments -contains '--show-toplevel') { return 'C:/project' }
        if ($Arguments -contains 'get-url') { return 'https://github.com/someone/other.git' }
    }
    Assert-Throws { Initialize-TandoRepository -Git 'git' -Path 'C:\project' } 'origin'
}
Test-Case 'Secure token prompt returns a temporary value and never changes environment' {
    $old = $env:WOKWI_CLI_TOKEN
    try {
        Remove-Item Env:WOKWI_CLI_TOKEN -ErrorAction SilentlyContinue
        $script:masked = $false
        function Read-Host { param($Prompt,[switch]$AsSecureString); $script:masked=$AsSecureString; ConvertTo-SecureString 'temporary-secret' -AsPlainText -Force }
        Assert-Equal (Read-TandoToken) 'temporary-secret'
        Assert-True $script:masked 'Token prompt must be hidden'
        Assert-True (!(Test-Path Env:WOKWI_CLI_TOKEN)) 'Prompt must not persist the token'
    } finally { $env:WOKWI_CLI_TOKEN = $old }
}
Test-Case 'Invalid token diagnostics redact its value' {
    $script:TandoSecret = 'secret-that-must-not-leak'
    $message = Protect-TandoText '401 Unauthorized: secret-that-must-not-leak'
    Assert-True (!$message.Contains($script:TandoSecret)) 'Secret leaked in diagnostic'
    Assert-True ($message.Contains('[REDACTED]')) 'Redaction marker absent'
    $script:TandoSecret = $null
}
Test-Case 'Firmware and WASM signatures reject stale or empty placeholder files' {
    $path = Join-Path $env:TEMP ('tando-binary-' + [guid]::NewGuid().ToString('N'))
    try {
        [IO.File]::WriteAllBytes($path, [byte[]](1,2,3,4))
        Assert-Throws { Assert-TandoBinary -Path $path -Magic ([byte[]](0,97,115,109)) } 'invalid'
        [IO.File]::WriteAllBytes($path, [byte[]](0,97,115,109,1,0,0,0))
        Assert-TandoBinary -Path $path -Magic ([byte[]](0,97,115,109))
    } finally { Remove-Item -LiteralPath $path -ErrorAction SilentlyContinue }
}
Test-Case 'Runtime failure restores caller token and refuses image success' {
    $old = $env:WOKWI_CLI_TOKEN
    $dir = Join-Path $env:TEMP ('tando-simulation-' + [guid]::NewGuid().ToString('N'))
    New-Item -ItemType Directory -Path $dir | Out-Null
    try {
        $env:WOKWI_CLI_TOKEN = 'caller-value'
        function Invoke-TandoCommand { throw '401 Unauthorized' }
        Assert-Throws { Invoke-TandoSimulation -Wokwi 'wokwi' -Python 'python' -Simulator $dir -Version '0.10.0-rc.8' -Token 'invalid-private-value' } 'token|access|401'
        Assert-Equal $env:WOKWI_CLI_TOKEN 'caller-value'
    } finally {
        $env:WOKWI_CLI_TOKEN=$old
        $screenshots = Join-Path $dir 'screenshots'
        if (Test-Path -LiteralPath $screenshots) { Remove-Item -LiteralPath $screenshots }
        Remove-Item -LiteralPath $dir
    }
}
Test-Case 'Native command errors and secrets are captured without false success' {
    $shell = Join-Path $PSHOME 'powershell.exe'
    if (!(Test-Path -LiteralPath $shell)) { $shell = Join-Path $PSHOME 'pwsh.exe' }
    $script:TandoSecret = 'fixture-private-token'
    try {
        Assert-Throws { Invoke-TandoCommand $shell @('-NoProfile','-Command',"Write-Output 'fixture-private-token'; exit 7") } 'exit 7'
        try { Invoke-TandoCommand $shell @('-NoProfile','-Command',"Write-Output 'fixture-private-token'; exit 7") }
        catch { Assert-True (!$_.Exception.Message.Contains('fixture-private-token')) 'Native failure leaked a token' }
        Assert-Throws { Invoke-TandoCommand 'C:\missing-tando-tool.exe' @() } 'missing'
    } finally { $script:TandoSecret=$null }
}
Test-Case 'A/F/H/J orchestration builds missing outputs, opens VS Code and reports partial state honestly' {
    $project = Join-Path $env:TEMP ('tando-orchestration-' + [guid]::NewGuid().ToString('N'))
    $simulator = Join-Path $project 'simulator\wokwi'
    New-Item -ItemType Directory -Path $simulator -Force | Out-Null
    Set-Content -LiteralPath (Join-Path $project 'VERSION') -Value '0.10.0-rc.8'
    $script:commands=@()
    function Assert-TandoHost {}
    function Find-TandoCode { return 'mock-code' }
    function Ensure-TandoGit { return 'mock-git' }
    function Ensure-TandoPython { return 'mock-python' }
    function Ensure-TandoWokwi { return 'mock-wokwi' }
    function Ensure-TandoBuildEnvironment { return 'mock-build-python' }
    function Get-TandoToolVersion { return [version]'6.2.0' }
    function Initialize-TandoRepository { return @{ Path=$project; Commit=('c'*40); Dirty=$false } }
    function Assert-TandoBinary { param($Path,$Magic); $script:commands+=@{File='binary-check';Args=$Path} }
    function Invoke-TandoCommand {
        param($FilePath,$Arguments,[switch]$Capture)
        $script:commands+=@{File=$FilePath;Args=($Arguments -join ' ')}
        if ($Arguments -contains '--list-extensions') { return 'Wokwi.wokwi-vscode' }
    }
    try {
        $result = Invoke-TandoInstall -InstallPath $project -ToolsRoot (Join-Path $project 'tools') -SkipSimulation
        Assert-Equal $result 2
        $report = Get-Content -LiteralPath (Join-Path $simulator '.tando-install.json') -Raw | ConvertFrom-Json
        Assert-Equal $report.Status 'prepared'
        Assert-Equal $report.Runtime 'unverified'
        Assert-Equal $report.VisualConfirmation 'unverified'
        Assert-True ([bool]($script:commands | Where-Object Args -Match 'platformio run')) 'Firmware build was skipped'
        Assert-True ([bool]($script:commands | Where-Object Args -Match 'chip compile')) 'WASM build was skipped'
        Assert-True ([bool]($script:commands | Where-Object Args -Match '--new-window')) 'VS Code folder was not opened'
        Assert-Equal @($script:commands | Where-Object File -EQ 'binary-check').Count 3
        Assert-True (!(($script:commands | ForEach-Object Args) -match '--install-extension')) 'Existing extension was reinstalled'
        function Read-TandoToken { return 'fixture-token' }
        function Invoke-TandoSimulation { throw 'Cloud unavailable' }
        Assert-Equal (Invoke-TandoInstall -InstallPath $project -ToolsRoot (Join-Path $project 'tools')) 1
        $report = Get-Content -LiteralPath (Join-Path $simulator '.tando-install.json') -Raw | ConvertFrom-Json
        Assert-Equal $report.Status 'verification-failed'
        Assert-Equal $report.Runtime 'unverified'
        Assert-True ($report.VSCode -match 'open requested') 'Runtime failure must still prepare GUI fallback'
        function Invoke-TandoSimulation {}
        Assert-Equal (Invoke-TandoInstall -InstallPath $project -ToolsRoot (Join-Path $project 'tools')) 0
        $report = Get-Content -LiteralPath (Join-Path $simulator '.tando-install.json') -Raw | ConvertFrom-Json
        Assert-Equal $report.Images 'automated checks passed'
        Assert-Equal $report.VisualConfirmation 'unverified'
        Assert-Equal $report.License 'manual activation/status unverified'
    } finally {
        Remove-Item -LiteralPath (Join-Path $simulator '.tando-install.json') -ErrorAction SilentlyContinue
        Remove-Item -LiteralPath (Join-Path $project 'VERSION')
        Remove-Item -LiteralPath $simulator
        Remove-Item -LiteralPath (Join-Path $project 'simulator')
        Remove-Item -LiteralPath $project
    }
}
Test-Case 'A real dirty Git checkout preserves tracked and untracked bytes and HEAD' {
    $git = (Get-Command git.exe -ErrorAction Stop).Source
    $fixture = Join-Path $env:TEMP ('tando-git-' + [guid]::NewGuid().ToString('N'))
    $tempRoot = [IO.Path]::GetFullPath($env:TEMP).TrimEnd('\') + '\'
    New-Item -ItemType Directory -Path $fixture | Out-Null
    try {
        Invoke-TandoCommand $git @('init','--quiet','--initial-branch=feature/wokwi-dual-gc9a01',$fixture)
        Invoke-TandoCommand $git @('-C',$fixture,'config','core.autocrlf','false')
        Invoke-TandoCommand $git @('-C',$fixture,'config','core.hooksPath',(Join-Path $fixture 'disabled-hooks'))
        Invoke-TandoCommand $git @('-C',$fixture,'config','commit.gpgsign','false')
        Invoke-TandoCommand $git @('-C',$fixture,'config','protocol.https.allow','never')
        Invoke-TandoCommand $git @('-C',$fixture,'config','protocol.ssh.allow','never')
        $tracked = Join-Path $fixture 'sketch.ino'
        [IO.File]::WriteAllText($tracked, 'original source')
        Invoke-TandoCommand $git @('-C',$fixture,'add','sketch.ino')
        Invoke-TandoCommand $git @('-C',$fixture,'-c','user.name=Tando Test','-c','user.email=test@example.invalid','commit','--quiet','-m','fixture')
        Invoke-TandoCommand $git @('-C',$fixture,'remote','add','origin','https://github.com/vahid162/tendo.git')
        [IO.File]::WriteAllText($tracked, "local source`nwith edits")
        $untracked = Join-Path $fixture 'my-notes.txt'
        [IO.File]::WriteAllBytes($untracked, [byte[]](0,1,2,13,10,255))
        $trackedHash = (Get-FileHash -LiteralPath $tracked -Algorithm SHA256).Hash
        $untrackedHash = (Get-FileHash -LiteralPath $untracked -Algorithm SHA256).Hash
        $head = Invoke-TandoCommand $git @('-C',$fixture,'rev-parse','HEAD') -Capture
        $state = Initialize-TandoRepository -Git $git -Path $fixture
        Assert-True $state.Dirty 'Actual Git dirt was not reported'
        Assert-Equal $state.Commit $head
        Assert-Equal (Invoke-TandoCommand $git @('-C',$fixture,'rev-parse','HEAD') -Capture) $head
        Assert-Equal (Get-FileHash -LiteralPath $tracked -Algorithm SHA256).Hash $trackedHash
        Assert-Equal (Get-FileHash -LiteralPath $untracked -Algorithm SHA256).Hash $untrackedHash
    } finally {
        $resolved = [IO.Path]::GetFullPath($fixture)
        if (!$resolved.StartsWith($tempRoot, [StringComparison]::OrdinalIgnoreCase) -or (Split-Path $resolved -Leaf) -notmatch '^tando-git-[0-9a-f]{32}$') { throw 'Refusing unsafe fixture cleanup' }
        Remove-Item -LiteralPath $resolved -Recurse -Force
    }
}
Test-Case 'Python reuse accepts a compatible interpreter without downloading or installing' {
    $expected = 'C:\user-tools\python\python.exe'
    function Test-Path { return $false }
    function Get-TandoCommandPath { return 'C:\global-python.exe' }
    function Get-TandoToolVersion { param($Executable); if ($Executable -eq $expected) { return [version]'3.13.17' }; return [version]'3.14.0' }
    function Invoke-TandoCommand {}
    function Get-TandoDownload { throw 'A compatible Python must not be downloaded again' }
    function Start-Process { throw 'A compatible Python must not be installed again' }
    Assert-Equal (Ensure-TandoPython -Root 'C:\user-tools') $expected
}
Test-Case 'Missing compatible Python uses a signed user install and rejects an invalid signature' {
    $script:pythonInstalled = $false
    $script:pythonStarts = 0
    $script:pythonDownload = $null
    function Test-Path { return $false }
    function Get-TandoCommandPath { return 'C:\global-python.exe' }
    function Get-TandoToolVersion {
        param($Executable)
        if ($Executable -eq 'C:\global-python.exe') { return [version]'3.14.0' }
        if ($script:pythonInstalled) { return [version]'3.13.17' }
        return $null
    }
    function Get-TandoDownload { param($Uri,$Path,$Sha256); $script:pythonDownload=@{Uri=$Uri;Path=$Path;Hash=$Sha256} }
    function Get-AuthenticodeSignature { return @{Status='Valid';SignerCertificate=@{Subject='CN=Python Software Foundation'}} }
    function Start-Process {
        [CmdletBinding()]
        param($FilePath,$ArgumentList,$WindowStyle,[switch]$Wait,[switch]$PassThru)
        $script:pythonStarts++
        Assert-True ($ArgumentList -contains 'InstallAllUsers=0') 'Python must install for the current user'
        Assert-True ($ArgumentList -contains 'PrependPath=0') 'Python must not modify the global PATH'
        Assert-True ($ArgumentList -contains 'TargetDir="C:\user-tools\python"') 'Python must stay within ToolsRoot'
        Assert-Equal $WindowStyle 'Hidden'
        $script:pythonInstalled=$true
        return @{ExitCode=0}
    }
    Assert-Equal (Ensure-TandoPython -Root 'C:\user-tools') 'C:\user-tools\python\python.exe'
    Assert-True ($script:pythonDownload.Uri -match '^https://www\.python\.org/ftp/python/') 'Python download must come from its official source'
    Assert-True ($script:pythonDownload.Hash -match '^[0-9a-f]{64}$') 'Python download must have a pinned SHA256'
    Assert-Equal $script:pythonStarts 1
    $script:pythonInstalled=$false
    function Get-AuthenticodeSignature { return @{Status='NotSigned';SignerCertificate=$null} }
    Assert-Throws { Ensure-TandoPython -Root 'C:\user-tools' } 'signature'
    Assert-Equal $script:pythonStarts 1
}
Test-Case 'Wokwi upgrades an incompatible command once and reuses the managed executable' {
    $script:wokwiReady=$false
    $script:wokwiDownloads=0
    function Get-TandoCommandPath { return 'C:\old-wokwi.exe' }
    function Get-TandoToolVersion {
        param($Executable)
        if ($Executable -eq 'C:\old-wokwi.exe') { return [version]'0.28.0' }
        if ($script:wokwiReady) { return [version]'0.29.1' }
        return $null
    }
    function Get-TandoDownload {
        param($Uri,$Path,$Sha256)
        Assert-True ($Uri -match '^https://github\.com/wokwi/wokwi-cli/releases/download/') 'Wokwi must use an official release'
        Assert-Equal $Path 'C:\user-tools\wokwi\wokwi-cli.exe'
        Assert-True ($Sha256 -match '^[0-9a-f]{64}$') 'Wokwi download needs a pinned SHA256'
        $script:wokwiDownloads++
        $script:wokwiReady=$true
    }
    Assert-Equal (Ensure-TandoWokwi -Root 'C:\user-tools') 'C:\user-tools\wokwi\wokwi-cli.exe'
    Assert-Equal (Ensure-TandoWokwi -Root 'C:\user-tools') 'C:\user-tools\wokwi\wokwi-cli.exe'
    Assert-Equal $script:wokwiDownloads 1
}
Test-Case 'Native Python metadata probes reuse a compatible build environment without pip' {
    $nativeCommand = ${function:Invoke-TandoCommand}
    $python = (Get-Command python.exe -ErrorAction Stop).Source
    $fixture = Join-Path $env:TEMP ('tando-python-probe-' + [guid]::NewGuid().ToString('N'))
    $scripts = Join-Path $fixture '.venv\Scripts'
    New-Item -ItemType Directory -Path $scripts -Force | Out-Null
    $venvPython = Join-Path $scripts 'python.exe'
    [IO.File]::WriteAllBytes($venvPython, [byte[]]@())
    $metadata = Join-Path $fixture 'metadata'
    foreach ($package in @(@('platformio','6.2.0'),@('Pillow','11.3.0'))) {
        $dist = Join-Path $metadata ($package[0] + '-' + $package[1] + '.dist-info')
        New-Item -ItemType Directory -Path $dist -Force | Out-Null
        [IO.File]::WriteAllText((Join-Path $dist 'METADATA'), ('Name: ' + $package[0] + "`nVersion: " + $package[1] + "`n"))
    }
    $previousPythonPath=$env:PYTHONPATH
    $script:metadataProbes=0
    $script:pipInstalls=0
    function Invoke-TandoCommand {
        param($FilePath,$Arguments,[switch]$Capture)
        if ($Arguments -contains '-c') {
            $script:metadataProbes++
            return & $nativeCommand -FilePath $python -Arguments $Arguments -Capture:$Capture
        }
        if ($Arguments -contains 'pip') { $script:pipInstalls++; return }
        throw 'The existing environment must not be created again'
    }
    try {
        $env:PYTHONPATH=$metadata
        Assert-Equal (Ensure-TandoBuildEnvironment -Python $python -Simulator $fixture) $venvPython
        Assert-Equal (Ensure-TandoBuildEnvironment -Python $python -Simulator $fixture) $venvPython
        Assert-Equal $script:metadataProbes 2
        Assert-Equal $script:pipInstalls 0
        if ($PSVersionTable.PSVersion.Major -eq 5) {
            Assert-Throws { & $nativeCommand -FilePath $python -Arguments @('-c','import importlib.metadata as m; assert m.version("platformio")=="6.2.0"') -Capture } 'NameError|SyntaxError|not defined'
        }
        [IO.File]::WriteAllText((Join-Path $metadata 'Pillow-11.3.0.dist-info\METADATA'), "Name: Pillow`nVersion: 11.2.0`n")
        Ensure-TandoBuildEnvironment -Python $python -Simulator $fixture | Out-Null
        Assert-Equal $script:pipInstalls 1
    } finally {
        $env:PYTHONPATH=$previousPythonPath
        foreach ($package in @('platformio-6.2.0.dist-info','Pillow-11.3.0.dist-info')) {
            $dist = Join-Path $metadata $package
            Remove-Item -LiteralPath (Join-Path $dist 'METADATA')
            Remove-Item -LiteralPath $dist
        }
        Remove-Item -LiteralPath $venvPython
        Remove-Item -LiteralPath $scripts
        Remove-Item -LiteralPath (Split-Path $scripts -Parent)
        Remove-Item -LiteralPath $metadata
        Remove-Item -LiteralPath $fixture
    }
}
Test-Case 'A failed build or missing firmware stops before simulation and never reports image success' {
    $project = Join-Path $env:TEMP ('tando-build-failure-' + [guid]::NewGuid().ToString('N'))
    $simulator = Join-Path $project 'simulator\wokwi'
    New-Item -ItemType Directory -Path $simulator -Force | Out-Null
    Set-Content -LiteralPath (Join-Path $project 'VERSION') -Value '0.10.0-rc.8'
    function Assert-TandoHost {}
    function Find-TandoCode { return 'mock-code' }
    function Ensure-TandoGit { return 'mock-git' }
    function Ensure-TandoPython { return 'mock-python' }
    function Ensure-TandoWokwi { return 'mock-wokwi' }
    function Ensure-TandoBuildEnvironment { return 'mock-build-python' }
    function Get-TandoToolVersion { return [version]'6.2.0' }
    function Initialize-TandoRepository { return @{Path=$project;Commit=('c'*40);Dirty=$false} }
    $script:cloudCalls=0
    function Read-TandoToken { $script:cloudCalls++; throw 'Build failure must not request credentials' }
    function Invoke-TandoSimulation { $script:cloudCalls++; throw 'Build failure must not reach the cloud' }
    function Invoke-TandoCommand {
        param($FilePath,$Arguments,[switch]$Capture)
        if ($Arguments -contains '--list-extensions') { return 'Wokwi.wokwi-vscode' }
        if ($script:failBuild -and $Arguments -contains 'platformio') { throw 'fixture compile failure' }
    }
    try {
        foreach ($failBuild in @($true,$false)) {
            $script:failBuild=$failBuild
            Assert-Equal (Invoke-TandoInstall -InstallPath $project -ToolsRoot (Join-Path $project 'tools') -NoOpen) 1
            $report=Get-Content -LiteralPath (Join-Path $simulator '.tando-install.json') -Raw | ConvertFrom-Json
            Assert-Equal $report.FailedStage 'firmware build'
            Assert-Equal $report.Runtime 'unverified'
            Assert-Equal $report.Images 'unverified'
            Assert-True ($report.Status -ne 'image-checks-passed') 'Build errors must not claim image success'
        }
        Assert-Equal $script:cloudCalls 0
    } finally {
        Remove-Item -LiteralPath (Join-Path $simulator '.tando-install.json') -ErrorAction SilentlyContinue
        Remove-Item -LiteralPath (Join-Path $project 'VERSION')
        Remove-Item -LiteralPath $simulator
        Remove-Item -LiteralPath (Join-Path $project 'simulator')
        Remove-Item -LiteralPath $project
    }
}
Write-Host "$passed installer checks passed. Installation, cloud rendering and VS Code activation are mocked, not verified."
