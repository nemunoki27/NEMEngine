using System.Security.Cryptography;
using System.Text;

namespace NEMEngine.Distribution;

// Git本体を消した後も初期化の続きが分かるようにする
public sealed record GitProjectReset(string Root, GitProjectBinding Binding) {
    private static string StatePath(string storeRoot, string projectRoot) => Path.Combine(storeRoot, "GitResets",
        Convert.ToHexString(SHA256.HashData(Encoding.UTF8.GetBytes(Path.GetFullPath(projectRoot).ToUpperInvariant()))) + ".json");

    public static GitProjectReset? Read(string storeRoot, string projectRoot) {
        string file = StatePath(storeRoot, projectRoot);
        if (!File.Exists(file)) return null;
        var state = DistributionStorage.Read<GitProjectReset>(file);
        if (!Path.GetFullPath(state.Root).Equals(Path.GetFullPath(projectRoot), StringComparison.OrdinalIgnoreCase) || state.Binding.Repository == null)
            throw new InvalidDataException("Git設定の初期化記録が一致しません");
        return state;
    }

    public void Save(string storeRoot) => DistributionStorage.Write(StatePath(storeRoot, Root), this);
    public void Complete(string storeRoot) => File.Delete(StatePath(storeRoot, Root));

    internal static void Restore(string root) {
        GitProjectAttributes.Reset(root);
        foreach (string name in new[] { ".git", ".github", ".gitmodules", ".lfsconfig" }) {
            string target = DistributionStorage.Child(root, name);
            if (Directory.Exists(target)) DistributionStorage.RemoveTree(target, root);
            else if (File.Exists(target)) { File.SetAttributes(target, FileAttributes.Normal); File.Delete(target); }
        }
    }
}
