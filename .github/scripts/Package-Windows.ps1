[CmdletBinding()]
param(
    [ValidateSet('x64')]
    [string] $Target = 'x64',
    [ValidateSet('Debug', 'RelWithDebInfo', 'Release', 'MinSizeRel')]
    [string] $Configuration = 'RelWithDebInfo'
)

$ErrorActionPreference = 'Stop'

if ( $DebugPreference -eq 'Continue' ) {
    $VerbosePreference = 'Continue'
    $InformationPreference = 'Continue'
}

if ( $env:CI -eq $null ) {
    throw "Package-Windows.ps1 requires CI environment"
}

if ( ! ( [System.Environment]::Is64BitOperatingSystem ) ) {
    throw "Packaging script requires a 64-bit system to build and run."
}

if ( $PSVersionTable.PSVersion -lt '7.2.0' ) {
    Write-Warning 'The packaging script requires PowerShell Core 7. Install or upgrade your PowerShell version: https://aka.ms/pscore6'
    exit 2
}

# Produces an archive using the OBS Studio 33+ plugin layout:
#
#   <name>-<version>-windows-x64.zip
#   └── <name>/
#       ├── <name>.dll
#       └── data/
#           ├── manifest.json
#           └── locale/...
#
# Users extract it into C:\ProgramData\obs-studio\plugins\ (a folder OBS creates on first start).
# Debug symbols go into a separate <name>-<version>-windows-x64-pdb.zip.
function Package {
    trap {
        Write-Error $_
        exit 2
    }

    $ScriptHome = $PSScriptRoot
    $ProjectRoot = Resolve-Path -Path "$PSScriptRoot/../.."
    $BuildSpecFile = "${ProjectRoot}/buildspec.json"

    $UtilityFunctions = Get-ChildItem -Path $PSScriptRoot/utils.pwsh/*.ps1 -Recurse

    foreach( $Utility in $UtilityFunctions ) {
        Write-Debug "Loading $($Utility.FullName)"
        . $Utility.FullName
    }

    $BuildSpec = Get-Content -Path ${BuildSpecFile} -Raw | ConvertFrom-Json
    $ProductName = $BuildSpec.name
    $ProductVersion = $BuildSpec.version

    $OutputName = "${ProductName}-${ProductVersion}-windows-${Target}"
    $InstallRoot = Resolve-Path -Path "${ProjectRoot}/release/${Configuration}"
    $StageRoot = "${ProjectRoot}/release/stage-${Target}"
    $PluginRoot = "${StageRoot}/${ProductName}"

    $RemoveArgs = @{
        ErrorAction = 'SilentlyContinue'
        Path = @(
            "${ProjectRoot}/release/${ProductName}-*-windows-*.zip",
            $StageRoot
        )
    }
    Remove-Item @RemoveArgs -Recurse -Force

    $Dll = "${InstallRoot}/${ProductName}/${ProductName}.dll"
    if ( ! ( Test-Path -LiteralPath $Dll ) ) {
        throw "Plugin binary not found at ${Dll}. Was the project installed with 'cmake --install'?"
    }

    $Manifest = "${InstallRoot}/${ProductName}/data/manifest.json"
    if ( ! ( Test-Path -LiteralPath $Manifest ) ) {
        throw "Plugin manifest not found at ${Manifest}."
    }

    Log-Group "Staging ${ProductName} (OBS 33+ layout)..."
    New-Item -ItemType Directory -Force -Path $PluginRoot | Out-Null
    Copy-Item -LiteralPath $Dll -Destination $PluginRoot -Force
    Copy-Item -LiteralPath "${InstallRoot}/${ProductName}/data" -Destination $PluginRoot -Recurse -Force

    Get-ChildItem -LiteralPath $StageRoot -Recurse -File | ForEach-Object {
        Write-Information ($_.FullName.Substring($StageRoot.Length + 1))
    }

    Log-Group "Archiving ${ProductName}..."
    $CompressArgs = @{
        Path = $PluginRoot
        CompressionLevel = 'Optimal'
        DestinationPath = "${ProjectRoot}/release/${OutputName}.zip"
        Verbose = ($Env:CI -ne $null)
    }
    Compress-Archive -Force @CompressArgs

    $Pdbs = @(Get-ChildItem -Path "${InstallRoot}/debug-symbols" -Filter '*.pdb' -File -ErrorAction SilentlyContinue)
    if ( $Pdbs.Count -gt 0 ) {
        Log-Group "Archiving ${ProductName} debug symbols..."
        $PdbArgs = @{
            Path = $Pdbs.FullName
            CompressionLevel = 'Optimal'
            DestinationPath = "${ProjectRoot}/release/${OutputName}-pdb.zip"
        }
        Compress-Archive -Force @PdbArgs
    }

    Remove-Item -LiteralPath $StageRoot -Recurse -Force
    Log-Group
}

Package
