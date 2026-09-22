using System.Collections;

namespace NEMEngine;

public readonly struct Collision {

    // コールバックを受け取るEntityと相手Entity
    public readonly Entity self;
    public readonly Entity entity;

    // 接触情報
    public readonly Vector3 normal;
    public readonly Vector3 point;
    public readonly float penetration;

    // 衝突した形状インデックス
    public readonly int selfShapeIndex;
    public readonly int otherShapeIndex;

    // Trigger接触か
    public readonly bool isTrigger;

    internal Collision(NativeCollisionEvent collision) {
        self = new Entity(collision.self);
        entity = new Entity(collision.other);
        normal = collision.normal.ToVector3();
        point = collision.point.ToVector3();
        penetration = collision.penetration;
        selfShapeIndex = collision.selfShapeIndex;
        otherShapeIndex = collision.otherShapeIndex;
        isTrigger = collision.isTrigger != 0;
    }
}

public abstract class ScriptBehaviour : Component {

    // owner Entity 内で自身の runtime entry を識別する scriptSlotID（CreateInstance 時に native から渡される）。
    // runtime managed instance handle ではない（混同しない）。Enabled 制御の identity に使う
    internal ulong scriptSlotID;
    // record 未生成時のフォールバック用。runtime entry があればそちらが正
    private bool enabledFallback = true;

    // 同型複数attachがあるためscriptは参照同一性で比較する（destroyed時のnull等値はObject側が担う）
    private protected override bool EqualsObject(Object other) => ReferenceEquals(this, other);
    public override int GetHashCode() => System.Runtime.CompilerServices.RuntimeHelpers.GetHashCode(this);

    // runtime の有効/無効。lifecycle multi-pass の同期境界で OnEnable/OnDisable へ反映される。
    // authoring の ScriptEntry.enabled へは書き戻さない（Play終了で authoring に戻る）
    public bool Enabled {
        get {
            int state = NativeEntityAPI.ReadScriptEnabled(entity.native, scriptSlotID);
            return state >= 0 ? state != 0 : enabledFallback;
        }
        set {
            enabledFallback = value;
            NativeEntityAPI.WriteScriptEnabled(entity.native, scriptSlotID, value);
        }
    }

    // owner が active hierarchy にあり、かつ Enabled なとき true
    public bool IsActiveAndEnabled => entity.activeInHierarchy && Enabled;

    // component access(GetComponent<T>等)はComponent基底が提供する（owner Entityへの委譲）

    // 指定 Entity を破棄する（WorldCommandBuffer 経由で遅延）
    protected void Destroy(Entity entity) => entity.Destroy();
    // 自分の owner Entity を破棄する
    protected void DestroySelf() => entity.Destroy();

    //========================================================================
    //	Prefab 実体化（Unity の Instantiate 相当の糖衣。nullは安全にnull Entity）
    //========================================================================
    protected static Entity Instantiate(Prefab? prefab)
        => prefab != null ? prefab.Instantiate() : Entity.nullEntity;
    protected static Entity Instantiate(Prefab? prefab, Vector3 position, Quaternion rotation)
        => prefab != null ? prefab.Instantiate(position, rotation) : Entity.nullEntity;
    protected static Entity Instantiate(Prefab? prefab, Vector3 position, Quaternion rotation, Entity parent)
        => prefab != null ? prefab.Instantiate(position, rotation, parent) : Entity.nullEntity;

    //========================================================================
    //	Coroutine（owner=this。owner 破棄 / DLL unload / Play Stop で停止）
    //========================================================================
    protected CoroutineHandle StartCoroutine(IEnumerator routine) => Coroutines.Start(this, routine);
    protected bool StopCoroutine(CoroutineHandle handle) => Coroutines.Stop(handle);
    protected void StopAllCoroutines() => Coroutines.StopAllForOwner(this);

    //========================================================================
    //	Timer（owner=this 紐付け。owner 破棄で自動 cancel）
    //========================================================================
    protected TimerHandle ScheduleTimer(float delaySeconds, Action callback) => Timers.Schedule(delaySeconds, callback, this);
    protected TimerHandle ScheduleRepeatingTimer(float intervalSeconds, Action callback) => Timers.ScheduleRepeating(intervalSeconds, callback, this);
    protected TimerHandle ScheduleUnscaledTimer(float delaySeconds, Action callback) => Timers.ScheduleUnscaled(delaySeconds, callback, this);

    //========================================================================
    //	EventBus（owner=this で購読し、owner 破棄時に自動解除される）
    //========================================================================
    protected EventSubscription Subscribe<T>(Action<T> handler) => EventBus.Subscribe(this, handler);
    protected void Publish<T>(in T evt) => EventBus.Publish(evt);
    protected void PublishDeferred<T>(in T evt) => EventBus.PublishDeferred(evt);

    public virtual void Awake() {}
    public virtual void Start() {}
    public virtual void OnEnable() {}
    public virtual void OnDisable() {}
    public virtual void OnDestroy() {}
    public virtual void FixedUpdate() {}
    public virtual void Update() {}
    public virtual void LateUpdate() {}
    public virtual void OnCollisionEnter(Collision collision) {}
    public virtual void OnCollisionStay(Collision collision) {}
    public virtual void OnCollisionExit(Collision collision) {}
    public virtual void OnAnimationEvent(AnimationEvent evt) {}
}
