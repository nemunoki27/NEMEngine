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

public abstract class ScriptBehaviour {

    private Entity owner;
    private Transform? cachedTransform;
    // owner Entity 内で自身の runtime entry を識別する scriptSlotID（CreateInstance 時に native から渡される）。
    // runtime managed instance handle ではない（混同しない）。Enabled 制御の identity に使う
    internal ulong scriptSlotId;
    // record 未生成時のフォールバック用。runtime entry があればそちらが正
    private bool enabledFallback = true;

    public Entity entity {
        get => owner;
        internal set {
            owner = value;
            cachedTransform = null;
        }
    }
    public Transform transform => cachedTransform ??= new Transform(owner);

    // runtime の有効/無効。lifecycle multi-pass の同期境界で OnEnable/OnDisable へ反映される。
    // authoring の ScriptEntry.enabled へは書き戻さない（Play終了で authoring に戻る）
    public bool Enabled {
        get {
            int state = NativeApi.ReadScriptEnabled(owner.native, scriptSlotId);
            return state >= 0 ? state != 0 : enabledFallback;
        }
        set {
            enabledFallback = value;
            NativeApi.WriteScriptEnabled(owner.native, scriptSlotId, value);
        }
    }

    // owner が active hierarchy にあり、かつ Enabled なとき true
    public bool IsActiveAndEnabled => owner.activeInHierarchy && Enabled;

    //========================================================================
    //	generic component access（owner Entity への委譲。hot path で reflection しない）
    //========================================================================
    protected bool Has<T>() where T : struct, IComponentRef<T> => owner.Has<T>();
    protected bool TryGet<T>(out T component) where T : struct, IComponentRef<T> => owner.TryGet(out component);
    protected T Get<T>() where T : struct, IComponentRef<T> => owner.Get<T>();
    protected void Add<T>() where T : struct, IComponentRef<T> => owner.Add<T>();
    protected void Remove<T>() where T : struct, IComponentRef<T> => owner.Remove<T>();

    // 自身や子孫 / 祖先から component を辿って取得する（owner Entity への委譲）
    protected bool TryGetInChildren<T>(out T component) where T : struct, IComponentRef<T> => owner.TryGetInChildren(out component);
    protected T GetInChildren<T>() where T : struct, IComponentRef<T> => owner.GetInChildren<T>();
    protected bool TryGetInParent<T>(out T component) where T : struct, IComponentRef<T> => owner.TryGetInParent(out component);
    protected T GetInParent<T>() where T : struct, IComponentRef<T> => owner.GetInParent<T>();
    protected List<T> GetAllInChildren<T>() where T : struct, IComponentRef<T> => owner.GetAllInChildren<T>();

    // 同 Entity 上の C# スクリプトを型で取得する（Unity の GetComponent<Script> 相当・owner Entity への委譲）
    protected T? GetComponent<T>() where T : ScriptBehaviour => owner.GetComponent<T>();
    protected bool TryGetComponent<T>(out T script) where T : ScriptBehaviour => owner.TryGetComponent(out script);
    // 自身か子孫 / 祖先から script を辿って取得する（owner Entity への委譲）
    protected T? GetComponentInChildren<T>() where T : ScriptBehaviour => owner.GetComponentInChildren<T>();
    protected T? GetComponentInParent<T>() where T : ScriptBehaviour => owner.GetComponentInParent<T>();

    // 指定 Entity を破棄する（WorldCommandBuffer 経由で遅延）
    protected void Destroy(Entity entity) => entity.Destroy();
    // 自分の owner Entity を破棄する
    protected void DestroySelf() => owner.Destroy();

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
}
