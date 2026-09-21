namespace NEMEngine;

// 全 component facade の共通基底。owner Entity だけを持つ opaque handle。
// native component pointer / ECS chunk pointer は一切保持しない。
public interface IComponentRef {

    Entity entity { get; }
}

// ComponentManifestから生成するclass wrapperが実装する自己参照interface。
// 固定component type IDとEntityからの生成をstatic abstractで提供する。
public interface IComponentRef<TSelf> : IComponentRef where TSelf : Component, IComponentRef<TSelf> {

    static abstract int componentTypeID { get; }
    // owner Entity から wrapper を構築する
    static abstract TSelf FromEntity(Entity entity);
}

// Manifestの固定IDを型ごとに保持する。
internal static class ComponentType<T> where T : Component, IComponentRef<T> {

    internal static int ID => T.componentTypeID;
}
