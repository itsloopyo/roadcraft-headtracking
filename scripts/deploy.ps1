#!/usr/bin/env pwsh
#Requires -Version 5.1
# Dev loop: copy the built .asi and the vendored ASI loader into the game folder.

[CmdletBinding()]
param(
    # Positional so `deploy.ps1 "D:\Games\RoadCraft"` works, matching the
    # positional game path install.cmd takes. Named -Config stays available.
    # With no path, every installed copy is deployed to, not just the first:
    # owning the game on two stores is ordinary, and deploying to whichever one
    # sorts first leaves you testing a build you did not just make.
    [Parameter(Position = 0)][string]$GamePath,
    [ValidateSet('Release', 'Debug')][string]$Config = 'Release'
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$projectDir = Split-Path -Parent $PSScriptRoot

Import-Module (Join-Path $projectDir 'cameraunlock-core/powershell/DevDeploy.psm1') -Force

$loader = Join-Path $projectDir 'vendor/ultimate-asi-loader/dinput8.dll'
if (-not (Test-Path $loader)) { throw "Vendored ASI loader missing. Run 'pixi run update-deps'." }

# No config is deployed: CameraUnlock.ini is the player's file, the mod creates
# it itself when it is missing (importing HeadTracking.ini, the file earlier
# versions read, while it is absent), and a dev loop that overwrote either
# would throw away whatever the current test is configured to do.
#
# Roadcraft - Retail.exe lives under root\bin\pc rather than the install root, and it
# imports DINPUT8.dll directly, so the loader takes that name and the
# game-local copy wins over the system one. That folder comes from
# games.json rather than a path joined here, so a store variant that nests its
# exe somewhere else still lands beside it. The orchestrator resolves every
# installed copy once and prints each exe directory it writes to and what it
# wrote there; a second lookup here to list them could disagree with it.
Invoke-DevDeployASILoader `
    -GameId 'roadcraft' `
    -GameDisplayName 'RoadCraft' `
    -BuildOutputPath (Join-Path $projectDir "build/$Config") `
    -ModDllName 'RoadCraftHeadTracking.asi' `
    -VendorLoaderDll $loader `
    -AsiLoaderName 'dinput8.dll' `
    -GivenPath $GamePath | Out-Null
