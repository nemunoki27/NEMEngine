namespace NEMEngine;

// 別 Entity 上の ScriptBehaviour への authoring 参照。
// runtime の managed instance handle / compact type ID は保存しない。
// owner EntityRef + scriptSlotId（同 type 複数 attach の識別子）+ 期待 script 型 GUID を保持する。
// Missing Script でも参照 identity を保持し、reload 後に再解決できる。
public readonly struct ScriptRef<T> where T : ScriptBehaviour {

    // 参照先 script を持つ Entity
    public readonly EntityRef entity;
    // owner Entity 内で script slot を一意に識別する ID（ScriptEntry.scriptSlotID）
    public readonly Uuid scriptSlotId;
    // 期待する Stable Script Type GUID（128bit 文字列）。型制約 T の検証・候補絞り込みに使う
    public readonly string scriptTypeId;

    public ScriptRef(EntityRef entity, Uuid scriptSlotId, string scriptTypeId) {
        this.entity = entity;
        this.scriptSlotId = scriptSlotId;
        this.scriptTypeId = scriptTypeId ?? string.Empty;
    }

    public bool isValid => entity.isValid && scriptSlotId.isValid;

    public static ScriptRef<T> Null => new(EntityRef.Null, Uuid.None, string.Empty);
}
