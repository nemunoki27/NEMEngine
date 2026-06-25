namespace NEMEngine;

// 別Entity上の組み込みコンポーネントTへのauthoring参照、EntityRefを保持し解決時にTを取得する
public readonly struct ComponentRef<T> where T : struct, IComponentRef<T> {

    // 参照先コンポーネントを持つEntity
    public readonly EntityRef entity;

    public ComponentRef(EntityRef entity) {
        this.entity = entity;
    }

    public bool isValid => entity.isValid;

    // 参照先Entityを解決する
    public Entity ResolveEntity() => entity.Resolve();

    // 参照先Entity上のTコンポーネントを取得する
    public bool TryResolve(out T component) {
        Entity resolved = entity.Resolve();
        if (resolved.isAlive) {
            return resolved.TryGet(out component);
        }
        component = default;
        return false;
    }

    public static ComponentRef<T> Null => new(EntityRef.Null);
}
