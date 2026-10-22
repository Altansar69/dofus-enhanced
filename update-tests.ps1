$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'Update-GitHub.ps1') -LibraryOnly
Add-Type -AssemblyName System.IO.Compression
Add-Type -AssemblyName System.IO.Compression.FileSystem
$build = Join-Path $PSScriptRoot 'build'
$null = [IO.Directory]::CreateDirectory($build)
Assert-PlainPath $build $true
$fixture = Join-Path $build ('update-fixtures-' + [Guid]::NewGuid().ToString('N'))
$null = [IO.Directory]::CreateDirectory($fixture)
$utf8 = New-Object Text.UTF8Encoding($false)
function Check([bool]$Value, [string]$Message) { if (!$Value) { throw $Message } }
function Reject([scriptblock]$Action, [string]$Message) {
    $rejected = $false
    try { & $Action | Out-Null } catch { $rejected = $true }
    Check $rejected $Message
}
function Write-Text([string]$Path, [string]$Text) { [IO.File]::WriteAllText($Path, $Text, $utf8) }
function Make-Package([string]$Name, [int]$Revision = 75, [string]$Extra = '') {
    $folder = Join-Path $fixture $Name
    $null = [IO.Directory]::CreateDirectory($folder)
    $manifest = "format=1`nclientVersion=9.9.9.9`nrevision=$Revision`ngameHash=$gameHash`nmetadataHash=$metadataHash`n"
    $integrity = ''
    foreach ($file in $script:PayloadFiles) {
        $path = Join-Path $folder $file
        Write-Text $path "synthetic test data $file"
        $hash = (Get-FileHash -LiteralPath $path).Hash.ToLowerInvariant()
        $integrity += "$file=$hash`n"
        if ($file -in @('DofusMiniatures.dll', 'ModuleLoader.exe', 'ModuleAutoLoader.exe')) { $manifest += "$file=$hash`n" }
    }
    Write-Text (Join-Path $folder 'release.ini') $manifest
    Write-Text (Join-Path $folder 'update-files.ini') $integrity
    Write-Text (Join-Path $folder 'LISEZ-MOI.txt') 'fixture'
    Write-Text (Join-Path $folder 'WEBVIEW2-LICENSE.txt') 'fixture'
    $archive = Join-Path $fixture "$Name.zip"
    $zip = [IO.Compression.ZipFile]::Open($archive, [IO.Compression.ZipArchiveMode]::Create)
    try {
        foreach ($file in Get-ChildItem -LiteralPath $folder -File) {
            $entry = "$script:Family$Revision/$($file.Name)"
            if ($Extra -and $file.Name -eq 'LISEZ-MOI.txt') { $entry = $Extra }
            $null = [IO.Compression.ZipFileExtensions]::CreateEntryFromFile($zip, $file.FullName, $entry)
        }
    } finally { $zip.Dispose() }
    return $archive
}
function Asset([int]$Revision, [string]$Game, [string]$Metadata, [string]$Digest) {
    $name = "DofusMiniatures-r$Revision-$Game-$Metadata.zip"
    return [pscustomobject]@{
        draft = $false; prerelease = $false; tag_name = "r$Revision"
        assets = @([pscustomobject]@{
            name = $name; state = 'uploaded'; size = 100
            browser_download_url = "https://github.com/$script:Repository/releases/download/r$Revision/$name"
            digest = "sha256:$Digest"
        })
    }
}
try {
    $gameHash = 'a' * 64
    $metadataHash = 'b' * 64
    $digest = 'c' * 64
    $first = Asset 75 $gameHash $metadataHash $digest
    $otherGame = Asset 79 ('d' * 64) $metadataHash $digest
    $otherMetadata = Asset 80 $gameHash ('d' * 64) $digest
    $draft = Asset 81 $gameHash $metadataHash $digest; $draft.draft = $true
    $preview = Asset 82 $gameHash $metadataHash $digest; $preview.prerelease = $true
    $latest = Asset 78 $gameHash $metadataHash $digest
    Check ((Select-Update @($otherGame, $otherMetadata, $draft, $preview, $latest, $first) $gameHash $metadataHash).revision -eq 78) 'Wrong compatible release selected'
    $external = Asset 90 $gameHash $metadataHash $digest
    $external.assets[0].browser_download_url = 'https://untrusted.example/archive.zip'
    Check ($null -eq (Select-Update @($external) $gameHash $metadataHash)) 'External URL accepted'
    $first.assets[0].digest = $null
    Check ($null -eq (Select-Update @($first) $gameHash $metadataHash)) 'Missing GitHub digest accepted'
    $first.assets[0].digest = "sha256:$digest"; $first.assets[0].size = 33MB
    Check ($null -eq (Select-Update @($first) $gameHash $metadataHash)) 'Oversized archive accepted'
    $root = Join-Path $fixture 'installed'
    $stage = Join-Path $fixture 'stage'
    $null = [IO.Directory]::CreateDirectory($root)
    $null = [IO.Directory]::CreateDirectory($stage)
    Write-Text (Join-Path $root 'DofusMiniatures-3.7.11.11-settings.ini') "githubUpdates=0`nmode=1`n"
    $settingsHash = (Get-FileHash -LiteralPath (Join-Path $root 'DofusMiniatures-3.7.11.11-settings.ini')).Hash
    $archive = Make-Package 'valid'
    $hash = (Get-FileHash -LiteralPath $archive).Hash.ToLowerInvariant()
    Reject { Install-Archive $archive $stage $root 75 $gameHash $metadataHash ('0' * 64) } 'Bad GitHub digest accepted'
    Reject { Install-Archive $archive $stage $root 75 $gameHash ('d' * 64) $hash } 'Wrong client metadata accepted'
    Check (!(Test-Path -LiteralPath (Join-Path $root "$script:Family`75"))) 'Failed extraction published a partial release'
    # A fresh stage is required after a deliberately rejected archive.
    $stage = Join-Path $fixture 'stage2'; $null = [IO.Directory]::CreateDirectory($stage)
    Check ((Install-Archive $archive $stage $root 75 $gameHash $metadataHash $hash) -eq 'INSTALLED') 'Verified release was not installed'
    Check ((Install-Archive $archive $stage $root 75 $gameHash $metadataHash $hash) -eq 'ALREADY_INSTALLED') 'Second check must be idempotent'
    Check ((Get-FileHash -LiteralPath (Join-Path $root 'DofusMiniatures-3.7.11.11-settings.ini')).Hash -eq $settingsHash) 'Preferences changed'
    $installed = Join-Path $root "$script:Family`75"
    Write-Text (Join-Path $installed 'TreasureOverlay.exe') 'tampered'
    Reject { Test-Package $installed 75 $gameHash $metadataHash } 'Tampered companion accepted'
    Reject { Install-Archive $archive $stage $root 75 $gameHash $metadataHash $hash } 'Existing corrupt release silently overwritten'
    $badZip = Make-Package 'traversal' 76 '../outside.exe'
    $badHash = (Get-FileHash -LiteralPath $badZip).Hash.ToLowerInvariant()
    Reject { Install-Archive $badZip $stage $root 76 $gameHash $metadataHash $badHash } 'Archive traversal accepted'
    $duplicate = Make-Package 'duplicate' 77 "$script:Family`77/ModuleLoader.exe"
    $badHash = (Get-FileHash -LiteralPath $duplicate).Hash.ToLowerInvariant()
    Reject { Install-Archive $duplicate $stage $root 77 $gameHash $metadataHash $badHash } 'Duplicate path accepted'
    Check (!(Test-Path -LiteralPath (Join-Path $fixture 'outside.exe'))) 'Archive wrote outside staging'

    # Exercise the real orchestration with synthetic installed client files and
    # a deterministic HTTP boundary. No network, registry or game process.
    $client = Join-Path $fixture 'client'
    $metaDirectory = Join-Path $client 'Dofus_Data/il2cpp_data/Metadata'
    $null = [IO.Directory]::CreateDirectory($metaDirectory)
    Write-Text (Join-Path $client 'Dofus.exe') 'fake'
    Write-Text (Join-Path $client 'GameAssembly.dll') 'future game'
    Write-Text (Join-Path $metaDirectory 'global-metadata.dat') 'future metadata'
    $gameHash = (Get-FileHash -LiteralPath (Join-Path $client 'GameAssembly.dll')).Hash.ToLowerInvariant()
    $metadataHash = (Get-FileHash -LiteralPath (Join-Path $metaDirectory 'global-metadata.dat')).Hash.ToLowerInvariant()
    $script:testArchive = Make-Package 'future' 78
    $script:testRelease = Asset 78 $gameHash $metadataHash (Get-FileHash -LiteralPath $script:testArchive).Hash.ToLowerInvariant()
    $script:testRelease.assets[0].size = (Get-Item -LiteralPath $script:testArchive).Length
    $script:requests = 0
    function Get-TrustedDownload([string]$Url, [string]$Destination, [long]$Limit, [Threading.CancellationToken]$Cancellation) {
        $script:requests++
        if ($Url.StartsWith('https://api.github.com/')) {
            Write-Text $Destination (ConvertTo-Json -InputObject @($script:testRelease) -Depth 6)
        } else { [IO.File]::Copy($script:testArchive, $Destination) }
    }
    $result = Invoke-GitHubUpdate $root (Join-Path $client 'Dofus.exe')
    Check ($result -eq 'UPDATE_INSTALLED r78' -and $script:requests -eq 2) 'Full staged update failed'
    $result = Invoke-GitHubUpdate $root (Join-Path $client 'Dofus.exe')
    Check ($result -eq 'UPDATE_ALREADY_INSTALLED r78' -and $script:requests -eq 3) 'Unnecessary repeated archive download'
    Check (@(Get-ChildItem -LiteralPath $root -Force -Directory | Where-Object Name -Like '.dofus-update-*').Count -eq 0) 'Staging cleanup failed'
    # A degraded GitHub answers the listing with an empty page: the newest
    # release endpoint still finds the compatible package.
    $script:testArchive = Make-Package 'newest' 79
    $script:testRelease = Asset 79 $gameHash $metadataHash (Get-FileHash -LiteralPath $script:testArchive).Hash.ToLowerInvariant()
    $script:testRelease.assets[0].size = (Get-Item -LiteralPath $script:testArchive).Length
    $script:requested = @()
    function Get-TrustedDownload([string]$Url, [string]$Destination, [long]$Limit, [Threading.CancellationToken]$Cancellation) {
        $script:requested += $Url
        if ($Url.EndsWith('/releases?per_page=100')) { Write-Text $Destination '[]' }
        elseif ($Url.EndsWith('/releases/latest')) { Write-Text $Destination (ConvertTo-Json -InputObject $script:testRelease -Depth 6) }
        else { [IO.File]::Copy($script:testArchive, $Destination) }
    }
    $result = @(Invoke-GitHubUpdate $root (Join-Path $client 'Dofus.exe'))
    Check (($result -join '|') -eq 'UPDATE_LISTING_EMPTY|UPDATE_INSTALLED r79' -and $script:requested.Count -eq 3 -and
        $script:requested[1] -eq "https://api.github.com/repos/$script:Repository/releases/latest") 'Empty listing must fall back to the newest release'
    $script:testRelease = Asset 80 ('d' * 64) $metadataHash $digest
    $script:requested = @()
    $result = @(Invoke-GitHubUpdate $root (Join-Path $client 'Dofus.exe'))
    Check (($result -join '|') -eq 'UPDATE_LISTING_EMPTY|UPDATE_NO_COMPATIBLE_RELEASE' -and $script:requested.Count -eq 2) 'An incompatible newest release must not be downloaded'
    # Releases without a compatible one are a definitive answer: no second request.
    $script:requested = @()
    function Get-TrustedDownload([string]$Url, [string]$Destination, [long]$Limit, [Threading.CancellationToken]$Cancellation) {
        $script:requested += $Url
        Write-Text $Destination (ConvertTo-Json -InputObject @($script:testRelease) -Depth 6)
    }
    $result = @(Invoke-GitHubUpdate $root (Join-Path $client 'Dofus.exe'))
    Check (($result -join '|') -eq 'UPDATE_NO_COMPATIBLE_RELEASE' -and $script:requested.Count -eq 1) 'A listing without a compatible release must not query the newest release'
    Check (@(Get-ChildItem -LiteralPath $root -Force -Directory | Where-Object Name -Like '.dofus-update-*').Count -eq 0) 'Staging cleanup failed after the fallback'
    function Get-TrustedDownload { throw 'simulated offline' }
    Reject { Invoke-GitHubUpdate $root (Join-Path $client 'Dofus.exe') } 'Network failure must be reported'
    Test-Package (Join-Path $root "$script:Family`78") 78 $gameHash $metadataHash
    Write-Output 'PASS: stable compatible selection, trusted assets, atomic install, no overwrite, no traversal, integrity, idempotence, future client, empty-listing fallback to the newest release, offline retention; fixtures only'
} finally {
    if ([IO.Path]::GetDirectoryName([IO.Path]::GetFullPath($fixture)) -ne [IO.Path]::GetFullPath($build) -or
        [IO.Path]::GetFileName($fixture) -notmatch '^update-fixtures-[a-f0-9]{32}$') { throw 'Invalid fixture cleanup target' }
    Assert-PlainPath $fixture $true
    Remove-Item -LiteralPath $fixture -Recurse -Force
}
