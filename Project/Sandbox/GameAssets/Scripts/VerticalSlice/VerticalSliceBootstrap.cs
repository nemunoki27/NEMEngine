using NEMEngine;

namespace SandboxScripts;

//============================================================================
//	VerticalSliceBootstrap
//	縦断確認の結果を bounded に集計し Start で summary を出力する probe
//============================================================================
[DefaultExecutionOrder(-1000)]
public sealed class VerticalSliceBootstrap : ScriptBehaviour
{
    // 集計上限。probe 報告が増えても無制限に伸ばさない
    private const int MaxRecords = 64;

    // 同一 scene 内の probe から参照される単純な共有記録（static・bounded）
    private static readonly List<string> records = new();

    public static void Report(string line)
    {
        if (records.Count >= MaxRecords) {
            return;
        }
        records.Add(line);
    }

    public override void Awake()
    {
        records.Clear();
        Report($"Bootstrap.Awake entity={entity.name}");
    }

    public override void Start()
    {
        Report($"Bootstrap.Start frame summary records={records.Count}");
        Debug.Log($"[VerticalSlice] bootstrap start. {records.Count} record(s):");
        foreach (string line in records) {
            Debug.Log($"[VerticalSlice]   {line}");
        }
    }

    public override void OnDestroy()
    {
        // Play stop 時に共有記録をクリアして次 session に持ち越さない
        records.Clear();
    }
}
