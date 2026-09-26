namespace NEMEngine;

// MonoBehaviour 型ごとの既定実行順を指定する。値が小さいほど先に実行される。
// 優先順位: Editor project override > [DefaultExecutionOrder] > 0。
// load 時に一度だけ反射で読まれ、native registry へ流れる（gameplay frame では参照しない）。
[AttributeUsage(AttributeTargets.Class, AllowMultiple = false, Inherited = false)]
public sealed class DefaultExecutionOrderAttribute : Attribute {

    public int Order { get; }

    public DefaultExecutionOrderAttribute(int order) {
        Order = order;
    }
}
