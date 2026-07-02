namespace NEMEngine;

// 基底クラス/インターフェース型のフィールドへ派生型のインスタンスを保存するための属性。
// インスペクターに派生型の選択UIが出て、選択した型のメンバを編集できる(Unityの[SerializeReference]相当)。
// 候補になるのはゲームassembly内の[Serializable]付き具象型のみ。List<T>/T[]へ付けた場合は要素へ適用される。
[AttributeUsage(AttributeTargets.Field, AllowMultiple = false, Inherited = false)]
public sealed class SerializeReferenceAttribute : Attribute {
}
