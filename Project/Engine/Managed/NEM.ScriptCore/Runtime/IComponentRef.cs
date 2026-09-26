namespace NEMEngine;

// 全 component facade の共通基底。owner GameObject だけを持つ opaque handle。
// native component pointer / ECS chunk pointer は一切保持しない。
public interface IComponentRef {

    GameObject gameObject { get; }
}

// ComponentManifestから生成するclass wrapperが実装する自己参照interface。
// 固定component type IDとGameObjectからの生成をstatic abstractで提供する。
public interface IComponentRef<TSelf> : IComponentRef where TSelf : Component, IComponentRef<TSelf> {

    static abstract int componentTypeID { get; }
    // owner GameObject から wrapper を構築する
    static abstract TSelf FromEntity(GameObject gameObject);
}

// Manifestの固定IDを型ごとに保持する。
internal static class ComponentType<T> where T : Component, IComponentRef<T> {

    internal static int ID => T.componentTypeID;
}
