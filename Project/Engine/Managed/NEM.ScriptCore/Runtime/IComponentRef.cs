namespace NEMEngine;

// 全 component facade の共通基底。owner Entity だけを持つ opaque handle。
// native component pointer / ECS chunk pointer は一切保持しない。
public interface IComponentRef {

    Entity entity { get; }
}

// 09_component_binding_codegen が生成する class wrapper が実装する自己参照 interface。
// compact native component type id の解決(安定なコンポーネント名)と Entity からの生成を static abstract で提供し、
// GetComponent<T> 等の generic access が reflection 無しで dispatch できるようにする。
public interface IComponentRef<TSelf> : IComponentRef where TSelf : Component, IComponentRef<TSelf> {

    // native ComponentTypeRegistry の登録名（compact id 解決のキー。Scene/Prefab へは保存しない）
    static abstract string componentTypeName { get; }
    // owner Entity から wrapper を構築する
    static abstract TSelf FromEntity(Entity entity);
}

// type ごとに compact native component type id を一度だけ解決して static にキャッシュする。
// native component type id は engine セッション中で固定（native ComponentTypeRegistry。C# reload で変わらない）なので、
// reload を跨いでも安全。GameScripts 側 T の static は collectible ALC ごとに作り直されるため、こちらも安全に再解決される。
internal static class ComponentType<T> where T : Component, IComponentRef<T> {

    // -2 = 未解決, -1 = 未登録(該当 native component 無し)
    private static int cachedId = -2;

    internal static int Id {
        get {
            if (cachedId == -2) {
                cachedId = NativeApi.ResolveComponentTypeId(T.componentTypeName);
            }
            return cachedId;
        }
    }
}
