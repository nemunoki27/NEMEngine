using System.ComponentModel;
using System.Runtime.InteropServices;
using System.Runtime.Versioning;
using System.Text;
using System.Text.Json;

namespace NEMEngine.Distribution;

// 認証情報はWindows資格情報へ保存する
[SupportedOSPlatform("windows")]
public sealed class WindowsCredentialStore : IGitHubCredentialStore {
    [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
    private struct Credential {
        public uint Flags;
        public uint Type;
        public string TargetName;
        public string? Comment;
        public long LastWritten;
        public uint BlobSize;
        public IntPtr Blob;
        public uint Persist;
        public uint AttributeCount;
        public IntPtr Attributes;
        public string? TargetAlias;
        public string UserName;
    }

    public GitHubToken? Read(string key) {
        if (!CredReadW(Target(key), 1, 0, out IntPtr result)) {
            int error = Marshal.GetLastWin32Error();
            if (error == 1168) return null;
            throw new Win32Exception(error, "GitHubの資格情報を読み込めません");
        }
        try {
            var credential = Marshal.PtrToStructure<Credential>(result);
            var bytes = new byte[credential.BlobSize];
            Marshal.Copy(credential.Blob, bytes, 0, bytes.Length);
            try { return JsonSerializer.Deserialize<GitHubToken>(bytes, DistributionStorage.JsonOptions); }
            finally { System.Security.Cryptography.CryptographicOperations.ZeroMemory(bytes); }
        } finally { CredFree(result); }
    }

    public void Write(string key, GitHubToken token) {
        byte[] bytes = Encoding.UTF8.GetBytes(JsonSerializer.Serialize(token, DistributionStorage.JsonOptions));
        IntPtr blob = Marshal.AllocCoTaskMem(bytes.Length);
        try {
            Marshal.Copy(bytes, 0, blob, bytes.Length);
            var credential = new Credential { Type = 1, TargetName = Target(key), UserName = "NEMLauncher", Persist = 2,
                Blob = blob, BlobSize = (uint)bytes.Length };
            if (!CredWriteW(ref credential, 0)) throw new Win32Exception(Marshal.GetLastWin32Error(), "GitHubの資格情報を保存できません");
        } finally {
            System.Security.Cryptography.CryptographicOperations.ZeroMemory(bytes);
            Marshal.Copy(bytes, 0, blob, bytes.Length);
            Marshal.FreeCoTaskMem(blob);
        }
    }

    public void Delete(string key) {
        if (!CredDeleteW(Target(key), 1, 0) && Marshal.GetLastWin32Error() != 1168)
            throw new Win32Exception(Marshal.GetLastWin32Error(), "GitHubの資格情報を削除できません");
    }

    private static string Target(string key) => "NEMLauncher/GitHub/" + key;
    [DllImport("advapi32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    [return: MarshalAs(UnmanagedType.Bool)] private static extern bool CredReadW(string target, uint type, uint flags, out IntPtr credential);
    [DllImport("advapi32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    [return: MarshalAs(UnmanagedType.Bool)] private static extern bool CredWriteW(ref Credential credential, uint flags);
    [DllImport("advapi32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    [return: MarshalAs(UnmanagedType.Bool)] private static extern bool CredDeleteW(string target, uint type, uint flags);
    [DllImport("advapi32.dll")] private static extern void CredFree(IntPtr credential);
}
