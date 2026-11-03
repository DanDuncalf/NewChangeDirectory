<#
.SYNOPSIS
    Add or remove a directory on the Windows PATH, preserving the registry value kind.

.DESCRIPTION
    Used by install.bat. Appends (or removes) a whole entry rather than doing a
    substring match, and preserves REG_EXPAND_SZ so existing %VAR% entries keep
    expanding. Broadcasts WM_SETTINGCHANGE so newly launched processes pick up
    the change without a reboot.

.PARAMETER Directory
    Directory to add/remove (e.g. "C:\Program Files\NCD\bin").

.PARAMETER Scope
    User  -> HKCU\Environment (per-user PATH)
    Machine -> HKLM\SYSTEM\CurrentControlSet\Control\Session Manager\Environment
    Machine scope requires an elevated process.

.PARAMETER Remove
    Remove the directory instead of adding it.
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$Directory,
    [ValidateSet('User', 'Machine')][string]$Scope = 'Machine',
    [switch]$Remove
)

$ErrorActionPreference = 'Stop'

$key = if ($Scope -eq 'Machine') {
    'HKLM:\SYSTEM\CurrentControlSet\Control\Session Manager\Environment'
} else {
    'HKCU:\Environment'
}

$item = Get-Item -Path $key
try { $kind = $item.GetValueKind('Path') } catch { $kind = 'ExpandString' }

$current = $item.GetValue('Path', '', [Microsoft.Win32.RegistryValueOptions]::DoNotExpandEnvironmentNames)
$entries = @($current -split ';' | Where-Object { $_ -and $_ -ne '' })
$target = $Directory.TrimEnd('\')
$present = @($entries | Where-Object { $_.TrimEnd('\') -ieq $target }).Count -gt 0

if ($Remove) {
    if (-not $present) {
        Write-Output "  [INFO] $Directory is not in $Scope PATH"
        exit 0
    }
    $entries = @($entries | Where-Object { $_.TrimEnd('\') -ine $target })
    $newValue = $entries -join ';'
    Set-ItemProperty -Path $key -Name 'Path' -Value $newValue -Type $kind
    Write-Output "  [OK] Removed $Directory from $Scope PATH"
    exit 0
}

if ($present) {
    Write-Output "  [OK] $Directory already in $Scope PATH"
    exit 0
}

$entries += $Directory
$newValue = $entries -join ';'
Set-ItemProperty -Path $key -Name 'Path' -Value $newValue -Type $kind
Write-Output "  [OK] Added $Directory to $Scope PATH"

# Notify Explorer/other processes that the environment changed.
Add-Type -Namespace NcdEnv -Name NativeMethods -MemberDefinition @'
[DllImport("user32.dll", SetLastError = true, CharSet = CharSet.Auto)]
public static extern IntPtr SendMessageTimeout(IntPtr hWnd, uint Msg, UIntPtr wParam, string lParam, uint fuFlags, uint uTimeout, out UIntPtr lpdwResult);
'@
$result = [UIntPtr]::Zero
[NcdEnv.NativeMethods]::SendMessageTimeout([IntPtr]0xFFFF, 0x001A, [UIntPtr]::Zero, 'Environment', 2, 5000, [ref]$result) | Out-Null
