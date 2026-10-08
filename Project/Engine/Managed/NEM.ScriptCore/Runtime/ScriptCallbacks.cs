using System.Collections;
using System.Linq.Expressions;
using System.Reflection;

namespace NEMEngine;

// Assembly登録時にcallbackを解決し、実行中の反射検索を避ける
internal sealed class ScriptCallbacks {

    internal readonly Action<MonoBehaviour>? Awake;
    internal readonly Action<MonoBehaviour>? Start;
    internal readonly Action<MonoBehaviour>? OnEnable;
    internal readonly Action<MonoBehaviour>? OnDisable;
    internal readonly Action<MonoBehaviour>? OnDestroy;
    internal readonly Action<MonoBehaviour>? FixedUpdate;
    internal readonly Action<MonoBehaviour>? Update;
    internal readonly Action<MonoBehaviour>? LateUpdate;
    internal readonly Action<MonoBehaviour, Collision>? OnCollisionEnter;
    internal readonly Action<MonoBehaviour, Collision>? OnCollisionStay;
    internal readonly Action<MonoBehaviour, Collision>? OnCollisionExit;
    internal readonly Action<MonoBehaviour, AnimationEvent>? OnAnimationEvent;

    internal ScriptCallbacks(Type type) {

        Awake = Resolve<Action<MonoBehaviour>>(type, nameof(Awake));
        Start = Resolve<Action<MonoBehaviour>>(type, nameof(Start));
        OnEnable = Resolve<Action<MonoBehaviour>>(type, nameof(OnEnable));
        OnDisable = Resolve<Action<MonoBehaviour>>(type, nameof(OnDisable));
        OnDestroy = Resolve<Action<MonoBehaviour>>(type, nameof(OnDestroy));
        FixedUpdate = Resolve<Action<MonoBehaviour>>(type, nameof(FixedUpdate));
        Update = Resolve<Action<MonoBehaviour>>(type, nameof(Update));
        LateUpdate = Resolve<Action<MonoBehaviour>>(type, nameof(LateUpdate));
        OnCollisionEnter = Resolve<Action<MonoBehaviour, Collision>>(type, nameof(OnCollisionEnter));
        OnCollisionStay = Resolve<Action<MonoBehaviour, Collision>>(type, nameof(OnCollisionStay));
        OnCollisionExit = Resolve<Action<MonoBehaviour, Collision>>(type, nameof(OnCollisionExit));
        OnAnimationEvent = Resolve<Action<MonoBehaviour, AnimationEvent>>(type, nameof(OnAnimationEvent));
    }

    private static T? Resolve<T>(Type type, string name) where T : Delegate {

        var parameters = typeof(T).GetMethod("Invoke")!.GetParameters()
            .Select(p => Expression.Parameter(p.ParameterType, p.Name)).ToArray();
        Type[] arguments = parameters.Skip(1).Select(p => p.Type).ToArray();
        const BindingFlags flags = BindingFlags.Public | BindingFlags.NonPublic | BindingFlags.Instance | BindingFlags.DeclaredOnly;

        // 派生側から探し、未定義なら基底の実装を使う
        for (Type? current = type; current != null && current != typeof(MonoBehaviour); current = current.BaseType) {
            MethodInfo? method = current.GetMethod(name, flags, binder: null, types: arguments, modifiers: null);
            if (method == null) {
                continue;
            }
            if (method.ContainsGenericParameters) {
                throw new InvalidOperationException($"{type.FullName}.{name}に型引数は指定できません");
            }
            Expression body = Expression.Call(Expression.Convert(parameters[0], current), method, parameters.Skip(1));
            if (method.ReturnType != typeof(void)) {
                if (name != nameof(Start) || method.ReturnType != typeof(IEnumerator)) {
                    throw new InvalidOperationException($"{type.FullName}.{name}の戻り値が不正です");
                }

                // IEnumerator Startを所有ScriptのCoroutineへ渡す
                MethodInfo start = typeof(Coroutines).GetMethod(nameof(Coroutines.Start), BindingFlags.Static | BindingFlags.NonPublic)!;
                body = Expression.Block(Expression.Call(start, parameters[0], body), Expression.Empty());
            }
            return Expression.Lambda<T>(body, parameters).Compile();
        }
        return null;
    }
}
