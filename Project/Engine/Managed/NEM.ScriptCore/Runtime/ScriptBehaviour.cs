namespace NEMEngine;

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
}
