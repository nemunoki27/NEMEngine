using System;
using System.Text;

namespace NEMEngine;

//============================================================================
//	ScriptProfiler
//============================================================================
public static unsafe class ScriptProfiler {

    private static string selectedType = string.Empty;
    private static NativeEntity selectedEntity;
    private static ulong selectedSlotID;
    private static int mainThreadID;

    // 対象変更時だけNative側から設定を受け取る
    internal static void Configure(string typeName, NativeEntity gameObject, ulong slotID) {
        selectedType = typeName;
        selectedEntity = gameObject;
        selectedSlotID = slotID;
        mainThreadID = Environment.CurrentManagedThreadId;
    }

    // 所有者を明示し、別スクリプトから直接呼ばれた処理も正しく分類する
    public static SampleScope Sample(MonoBehaviour owner, string name) {
        if (selectedType.Length == 0 || Environment.CurrentManagedThreadId != mainThreadID ||
            ReferenceEquals(owner, null) || owner.GetType().FullName != selectedType ||
            string.IsNullOrEmpty(name) || NativeAPI.BeginScriptSample == null) {
            return default;
        }
        NativeEntity gameObject = owner.gameObject.native;
        if (selectedEntity.world.generation != 0 &&
            (gameObject.world.index != selectedEntity.world.index ||
             gameObject.world.generation != selectedEntity.world.generation ||
             gameObject.index != selectedEntity.index || gameObject.generation != selectedEntity.generation ||
             owner.scriptSlotID != selectedSlotID)) {
            return default;
        }
        int size = Encoding.UTF8.GetByteCount(name);
        if (size > 511) {
            return default;
        }
        byte* text = stackalloc byte[512];
        Encoding.UTF8.GetBytes(name.AsSpan(), new Span<byte>(text, size));
        text[size] = 0;
        return new SampleScope(NativeAPI.BeginScriptSample(gameObject, owner.scriptSlotID, text));
    }

    // usingで早期returnや例外時にも終了通知を行う
    public readonly struct SampleScope : IDisposable {
        private readonly ulong token;

        internal SampleScope(ulong token) {
            this.token = token;
        }

        public void Dispose() {
            if (token != 0 && Environment.CurrentManagedThreadId == mainThreadID && NativeAPI.EndScriptSample != null) {
                NativeAPI.EndScriptSample(token);
            }
        }
    }
}
