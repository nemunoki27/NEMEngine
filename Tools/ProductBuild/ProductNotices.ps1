function Set-ProductNotices {
    param($manifest, [string]$stage, [string]$executable, [string]$toolchainRoot, [bool]$managedScripts = $true)
    $sources = [ordered]@{
        'NEMEngine runtime' = Join-Path $PSScriptRoot 'RuntimeNotices.txt'
    }
    if ($managedScripts) {
        $sources['.NET / LICENSE.txt'] = Join-Path $toolchainRoot 'LICENSE.txt'
        $sources['.NET / ThirdPartyNotices.txt'] = Join-Path $toolchainRoot 'ThirdPartyNotices.txt'
    }
    foreach ($entry in $manifest.files) {
        $relative = [string]$entry.destination
        if ([IO.Path]::GetFileName($relative) -match '^(LICEN[SC]E|COPYING|NOTICE|ThirdPartyNotices|ATTRIBUTION|OFL)([.-].*)?$' -and
            !$relative.EndsWith('.meta', [StringComparison]::OrdinalIgnoreCase)) {
            $sources[$relative] = Get-ChildPath $stage $relative
        }
    }
    $text = [Text.StringBuilder]::new()
    foreach ($source in $sources.GetEnumerator()) {
        [void]$text.AppendLine($source.Key)
        [void]$text.AppendLine([IO.File]::ReadAllText($source.Value))
        [void]$text.AppendLine()
    }

    if (!('NEMEngine.Build.ProductResources' -as [type])) {
        Add-Type -TypeDefinition @"
using System;
using System.ComponentModel;
using System.Runtime.InteropServices;
namespace NEMEngine.Build {
    public static class ProductResources {
        [DllImport("kernel32.dll", CharSet=CharSet.Unicode, SetLastError=true)]
        private static extern IntPtr BeginUpdateResourceW(string file, bool deleteExisting);
        [DllImport("kernel32.dll", CharSet=CharSet.Unicode, SetLastError=true)]
        private static extern bool UpdateResourceW(IntPtr update, IntPtr type, string name, ushort language, byte[] bytes, uint length);
        [DllImport("kernel32.dll", SetLastError=true)]
        private static extern bool EndUpdateResourceW(IntPtr update, bool discard);

        public static void WriteNotices(string file, byte[] bytes) {
            IntPtr update = BeginUpdateResourceW(file, false);
            if (update == IntPtr.Zero) throw new Win32Exception();
            if (!UpdateResourceW(update, new IntPtr(10), "NEM_PRODUCT_NOTICES", 0, bytes, (uint)bytes.Length)) {
                int error = Marshal.GetLastWin32Error();
                EndUpdateResourceW(update, true);
                throw new Win32Exception(error);
            }
            if (!EndUpdateResourceW(update, false)) throw new Win32Exception();
        }
    }
}
"@
    }
    [NEMEngine.Build.ProductResources]::WriteNotices($executable, [Text.Encoding]::UTF8.GetBytes($text.ToString()))
}
