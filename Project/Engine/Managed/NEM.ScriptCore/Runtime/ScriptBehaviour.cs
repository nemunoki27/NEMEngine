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

    public Entity entity { get; internal set; }
    public Transform transform => entity.transform;

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
