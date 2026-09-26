using System.Reflection;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using NEMEngine;

namespace NEM.ScriptAnalyzers.Tests;

internal static unsafe class ComponentQueryTests {
    private interface IProbe { }
    private abstract class BaseProbe : MonoBehaviour { }
    private sealed class Probe : BaseProbe, IProbe { }

    internal static void Run() {
        var alive = NativeAPI.IsAlive;
        var instanceID = NativeAPI.GetComponentInstanceID;
        var has = NativeAPI.HasComponent;
        var active = NativeAPI.GetActiveInHierarchy;
        var parent = NativeAPI.GetParent;
        var child = NativeAPI.GetFirstChild;
        var sibling = NativeAPI.GetNextSibling;
        NativeAPI.IsAlive = &Alive;
        NativeAPI.GetComponentInstanceID = &InstanceID;
        NativeAPI.HasComponent = &Has;
        NativeAPI.GetActiveInHierarchy = &Active;
        NativeAPI.GetParent = &Parent;
        NativeAPI.GetFirstChild = &Child;
        NativeAPI.GetNextSibling = &Sibling;
        var store = (ScriptInstanceStore)typeof(HostBridge).GetField("instances", BindingFlags.NonPublic | BindingFlags.Static)!.GetValue(null)!;
        NativeScriptInstanceHandle first = NativeScriptInstanceHandle.Null;
        NativeScriptInstanceHandle second = NativeScriptInstanceHandle.Null;
        try {
            var root = GameObject.FromNative(Entity(1))!;
            var inactive = GameObject.FromNative(Entity(2))!;
            var leaf = GameObject.FromNative(Entity(4))!;
            var a = new Probe { gameObject = root };
            var b = new Probe { gameObject = root };
            first = store.AllocateSlot(a);
            second = store.AllocateSlot(b);
            Check(root.GetComponent<BaseProbe>() == a && ReferenceEquals(root.GetComponent<IProbe>(), a));
            Check(root.GetComponents<Probe>().Length == 2 && root.GetComponents<Component>().Length == 3);

            // 非activeの枝を除外し、呼出し元自身は常に含める
            Check(Indices(root.GetComponentsInChildren<Transform>()).SequenceEqual(new uint[] { 1, 3 }));
            Check(Indices(root.GetComponentsInChildren<Transform>(true)).SequenceEqual(new uint[] { 1, 2, 4, 3 }));
            Check(Indices(inactive.GetComponentsInChildren<Transform>()).SequenceEqual(new uint[] { 2 }));
            Check(Indices(leaf.GetComponentsInParent<Transform>()).SequenceEqual(new uint[] { 4, 1 }));
            Check(Indices(leaf.GetComponentsInParent<Transform>(true)).SequenceEqual(new uint[] { 4, 2, 1 }));
            var results = new List<Transform> { leaf.transform };
            root.GetComponentsInChildren(false, results);
            Check(Indices(results).SequenceEqual(new uint[] { 1, 3 }));
            Check(root.transform.GetChild(0).gameObject == inactive);
            bool failed = false;
            try { root.transform.GetChild(2); } catch (ArgumentOutOfRangeException) { failed = true; }
            Check(failed);
        } finally {
            store.ReleaseSlot(first);
            store.ReleaseSlot(second);
            NativeAPI.IsAlive = alive;
            NativeAPI.GetComponentInstanceID = instanceID;
            NativeAPI.HasComponent = has;
            NativeAPI.GetActiveInHierarchy = active;
            NativeAPI.GetParent = parent;
            NativeAPI.GetFirstChild = child;
            NativeAPI.GetNextSibling = sibling;
        }
    }

    private static IEnumerable<uint> Indices(IEnumerable<Transform> values) => values.Select(value => value.gameObject.native.index);
    private static NativeEntity Entity(uint index) => new() { world = new() { index = 20, generation = 1 }, index = index, generation = 1 };
    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    private static int Alive(NativeEntity entity) => entity.world.index == 20 && entity.index is >= 1 and <= 4 ? 1 : 0;
    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    private static ulong InstanceID(NativeEntity entity, int typeID) => typeID == Transform.componentTypeID ? entity.index + 100ul : 0;
    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    private static int Has(NativeEntity entity, int typeID) => typeID == Transform.componentTypeID ? 1 : 0;
    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    private static int Active(NativeEntity entity) => entity.index is 1 or 3 ? 1 : 0;
    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    private static NativeEntity Parent(NativeEntity entity) => entity.index switch { 2 or 3 => Entity(1), 4 => Entity(2), _ => NativeEntity.Null };
    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    private static NativeEntity Child(NativeEntity entity) => entity.index switch { 1 => Entity(2), 2 => Entity(4), _ => NativeEntity.Null };
    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    private static NativeEntity Sibling(NativeEntity entity) => entity.index == 2 ? Entity(3) : NativeEntity.Null;
    private static void Check(bool value) { if (!value) throw new InvalidOperationException("Component query contract failed."); }
}
