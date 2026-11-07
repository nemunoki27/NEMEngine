using System.IO.Compression;
using System.Net;
using System.Net.Http.Headers;

namespace NEMEngine.Distribution;

// 通信と展開を配布先の公開から分離する
public sealed class PackageTransfer(HttpClient client) {
    public async Task<string> DownloadAsync(PackageInfo package, string cacheRoot,
        IProgress<TransferProgress>? progress, CancellationToken token) {
        ContentValidation.Validate(package);
        var uri = new Uri(package.Url);
        Directory.CreateDirectory(cacheRoot);
        string archive = DistributionStorage.Child(cacheRoot, package.ID + "-" + package.Sha256 + ".zip");
        if (File.Exists(archive) && await DistributionStorage.HashAsync(archive, token) == package.Sha256.ToLowerInvariant()) return archive;
        string partial = archive + ".partial";
        long offset = File.Exists(partial) ? new FileInfo(partial).Length : 0;
        if (offset >= package.Bytes) { File.Delete(partial); offset = 0; }
        using var request = new HttpRequestMessage(HttpMethod.Get, uri);
        if (offset > 0) request.Headers.Range = new RangeHeaderValue(offset, null);
        using var response = await client.SendAsync(request, HttpCompletionOption.ResponseHeadersRead, token);
        response.EnsureSuccessStatusCode();
        if (response.StatusCode == HttpStatusCode.PartialContent) {
            if (response.Content.Headers.ContentRange?.From != offset || response.Content.Headers.ContentRange?.Length != package.Bytes)
                throw new InvalidDataException("再開位置が配布物と一致しません");
        } else { offset = 0; }
        if (response.Content.Headers.ContentLength is long length && length != package.Bytes - offset)
            throw new InvalidDataException("配布物のサイズが一致しません");
        // アーカイブ全体をメモリへ読み込まない
        await using (var target = new FileStream(partial, offset > 0 ? FileMode.Append : FileMode.Create, FileAccess.Write, FileShare.None)) {
            await using var source = await response.Content.ReadAsStreamAsync(token);
            byte[] buffer = new byte[81920];
            long completed = offset;
            int read;
            while ((read = await source.ReadAsync(buffer, token)) != 0) {
                completed += read;
                if (completed > package.Bytes) throw new InvalidDataException("配布物が指定サイズを超えています");
                await target.WriteAsync(buffer.AsMemory(0, read), token);
                progress?.Report(new("ダウンロード中: " + package.ID, completed, package.Bytes));
            }
            if (completed != package.Bytes) throw new IOException("ダウンロードが途中で終了しました 再試行してください");
        }
        progress?.Report(new("ダウンロード内容を検証中"));
        if (await DistributionStorage.HashAsync(partial, token) != package.Sha256.ToLowerInvariant()) {
            File.Delete(partial);
            throw new InvalidDataException("ダウンロード内容のSHA-256が一致しません");
        }
        File.Move(partial, archive, true);
        return archive;
    }

    public static async Task ExtractAsync(string archive, string destination, long expectedBytes,
        IProgress<TransferProgress>? progress, CancellationToken token) {
        if (Directory.Exists(destination)) throw new IOException("展開先が既に存在します");
        using var zip = ZipFile.OpenRead(archive);
        var paths = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
        long total = 0;
        // 保存先と容量を先に確認する
        foreach (var entry in zip.Entries) {
            string target = DistributionStorage.Child(destination, entry.FullName);
            int kind = (entry.ExternalAttributes >> 16) & 0xf000;
            if (kind == 0xa000 || (entry.ExternalAttributes & (int)FileAttributes.ReparsePoint) != 0 || !paths.Add(target))
                throw new InvalidDataException("リンクまたは重複した保存先を含む配布物です");
            total = checked(total + entry.Length);
        }
        if (total != expectedBytes) throw new InvalidDataException("展開後のサイズが配布情報と一致しません");
        Directory.CreateDirectory(destination);
        long completed = 0;
        foreach (var entry in zip.Entries) {
            token.ThrowIfCancellationRequested();
            string target = DistributionStorage.Child(destination, entry.FullName);
            if (entry.FullName.EndsWith('/') || entry.FullName.EndsWith('\\')) { Directory.CreateDirectory(target); continue; }
            Directory.CreateDirectory(Path.GetDirectoryName(target)!);
            await using var input = entry.Open();
            await using var output = new FileStream(target, FileMode.CreateNew, FileAccess.Write, FileShare.None);
            byte[] buffer = new byte[81920];
            long written = 0;
            int count;
            while ((count = await input.ReadAsync(buffer, token)) != 0) {
                written = checked(written + count);
                if (written > entry.Length) throw new InvalidDataException("展開内容が指定サイズを超えています");
                await output.WriteAsync(buffer.AsMemory(0, count), token);
            }
            if (written != entry.Length) throw new InvalidDataException("展開内容のサイズが一致しません");
            completed += entry.Length;
            progress?.Report(new("展開中", completed, total));
        }
    }
}
