namespace NEMEngine;

// 参照復元中の所有GameObjectを保持する
internal sealed class ScriptReferenceContext {

    internal NativeEntity owner = NativeEntity.Null;

    internal Scope Enter(MonoBehaviour script) => new(this, GameObject.RawNative(script.ownerReference));

    internal readonly struct Scope : IDisposable {

        private readonly ScriptReferenceContext context;
        private readonly NativeEntity previous;

        internal Scope(ScriptReferenceContext context, NativeEntity owner) {
            this.context = context;
            previous = context.owner;
            context.owner = owner;
        }

        // 再入前の所有GameObjectへ戻す
        public void Dispose() { context.owner = previous; }
    }
}
