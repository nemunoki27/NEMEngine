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
    internal static void Configure(string typeName, NativeEntity entity, ulong slotID) {
        selectedType = typeName;
        selectedEntity = entity;
        selectedSlotID = slotID;
        mainThreadID = Environment.CurrentManagedThreadId;
    }

    // 所有者を明示し、別スクリプトから直接呼ばれた処理も正しく分類する
    public static SampleScope Sample(ScriptBehaviour owner, string name) {
        if (selectedType.Length == 0 || Environment.CurrentManagedThreadId != mainThreadID ||
            ReferenceEquals(owner, null) || owner.GetType().FullName != selectedType ||
            string.IsNullOrEmpty(name) || NativeApi.BeginScriptSample == null) {
            return default;
        }
        NativeEntity entity = owner.entity.native;
        if (selectedEntity.world.generation != 0 &&
            (entity.world.index != selectedEntity.world.index ||
             entity.world.generation != selectedEntity.world.generation ||
             entity.index != selectedEntity.index || entity.generation != selectedEntity.generation ||
             owner.scriptSlotId != selectedSlotID)) {
            return default;
        }
        int size = Encoding.UTF8.GetByteCount(name);
        if (size > 511) {
            return default;
        }
        byte* text = stackalloc byte[512];
        Encoding.UTF8.GetBytes(name.AsSpan(), new Span<byte>(text, size));
        text[size] = 0;
        return new SampleScope(NativeApi.BeginScriptSample(entity, owner.scriptSlotId, text));
    }

    // usingで早期returnや例外時にも終了通知を行う
    public readonly struct SampleScope : IDisposable {
        private readonly ulong token;

        internal SampleScope(ulong token) {
            this.token = token;
        }

        public void Dispose() {
            if (token != 0 && Environment.CurrentManagedThreadId == mainThreadID && NativeApi.EndScriptSample != null) {
                NativeApi.EndScriptSample(token);
            }
        }
    }
}
