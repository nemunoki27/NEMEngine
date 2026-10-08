namespace NEMEngine;

// 衝突通知の接触段階
internal enum ScriptCollisionCallback {

    Enter,
    Stay,
    Exit,
}

// Script通知の参照解決と例外境界をまとめる
internal sealed unsafe class ScriptCallbackInvoker {

    // 個体管理と保存参照の解決を接続する
    internal ScriptCallbackInvoker(ScriptInstanceStore instances, ManagedAssemblySession session) {

        this.instances = instances;
        this.session = session;
    }

    // 生存するScriptへ通知して例外を診断へ渡す
    internal ManagedStatus Invoke(NativeScriptInstanceHandle handle, string callbackName, Action<MonoBehaviour> body) {

        if (!instances.TryResolveSlot(handle, out MonoBehaviour script)) {
            return ManagedStatus.InvalidInstanceHandle;
        }

        try {
            // 保留値と保存callbackも例外境界内で処理する
            if (!PrepareInvocation(handle, script)) {
                return ManagedStatus.InvalidInstanceHandle;
            }
            body(script);
            return ManagedStatus.Ok;
        }
        catch (Exception ex) {
            ScriptInvocationDiagnostics.LogScriptException(session.registry, script, callbackName, ex);
            return ManagedStatus.ScriptException;
        }
    }

    // 衝突通知の種類に応じたcallbackを実行する
    internal ManagedStatus InvokeCollision(NativeScriptInstanceHandle handle, NativeCollisionEvent collision,
        ScriptCollisionCallback phase) {

        if (!instances.TryResolveSlot(handle, out MonoBehaviour script)) {
            return ManagedStatus.InvalidInstanceHandle;
        }
        string callbackName = phase switch {
            ScriptCollisionCallback.Enter => nameof(ScriptCallbacks.OnCollisionEnter),
            ScriptCollisionCallback.Stay => nameof(ScriptCallbacks.OnCollisionStay),
            _ => nameof(ScriptCallbacks.OnCollisionExit),
        };
        try {
            // 未解決参照の再適用も例外境界へ含める
            if (!PrepareInvocation(handle, script)) {
                return ManagedStatus.InvalidInstanceHandle;
            }
            Action<MonoBehaviour, Collision>? callback = phase switch {
                ScriptCollisionCallback.Enter => script.callbacks.OnCollisionEnter,
                ScriptCollisionCallback.Stay => script.callbacks.OnCollisionStay,
                _ => script.callbacks.OnCollisionExit,
            };
            callback?.Invoke(script, new Collision(collision));
            return ManagedStatus.Ok;
        }
        catch (Exception ex) {
            ScriptInvocationDiagnostics.LogScriptException(session.registry, script, callbackName, ex);
            return ManagedStatus.ScriptException;
        }
    }

    // 文字列の読込とAnimation通知を例外境界へ含める
    internal ManagedStatus InvokeAnimationEvent(NativeScriptInstanceHandle handle,
        byte* name, float floatParam, int intParam, byte* stringParam) {

        if (!instances.TryResolveSlot(handle, out MonoBehaviour script)) {
            return ManagedStatus.InvalidInstanceHandle;
        }
        try {
            if (!PrepareInvocation(handle, script)) {
                return ManagedStatus.InvalidInstanceHandle;
            }
            script.callbacks.OnAnimationEvent?.Invoke(script, new AnimationEvent(
                ManagedUTF8Transfer.PtrToString(name) ?? string.Empty, floatParam, intParam,
                ManagedUTF8Transfer.PtrToString(stringParam) ?? string.Empty));
            return ManagedStatus.Ok;
        }
        catch (Exception ex) {
            ScriptInvocationDiagnostics.LogScriptException(
                session.registry, script, nameof(ScriptCallbacks.OnAnimationEvent), ex);
            return ManagedStatus.ScriptException;
        }
    }

    private readonly ScriptInstanceStore instances;
    private readonly ManagedAssemblySession session;

    // 参照解決後も同じ個体と世代なら通知する
    private bool PrepareInvocation(NativeScriptInstanceHandle handle, MonoBehaviour script) {

        session.codec.FlushPendingReferenceFields();
        return instances.TryResolveSlot(handle, out MonoBehaviour current) && ReferenceEquals(script, current);
    }
}
