namespace NEMEngine;

using static NEMEngine.NativeAPI;

// 接続済みcallbackを用途別に呼び出す
internal static unsafe class NativeUIAPI {

    internal static NativeUISelectableRuntimeState ReadUISelectableRuntimeState(
        NativeEntity entity) {

        NativeUISelectableRuntimeState state = default;
        if (GetUISelectableRuntimeState != null) {
            GetUISelectableRuntimeState(entity, &state);
        }
        return state;
    }

    internal static NativeUIProgressRuntimeState ReadUIProgressRuntimeState(
        NativeEntity entity) {

        NativeUIProgressRuntimeState state = default;
        if (GetUIProgressRuntimeState != null) {
            GetUIProgressRuntimeState(entity, &state);
        }
        return state;
    }

    internal static int[] CanvasGetInputBindings(
        NativeEntity entity, int action, int device) {

        if (CanvasCopyInputBindings == null) {
            return Array.Empty<int>();
        }
        int count = CanvasCopyInputBindings(entity, action, device, null, 0);
        if (count <= 0) {
            return Array.Empty<int>();
        }

        int[] bindings = new int[count];
        int currentCount;
        fixed (int* values = bindings) {
            currentCount = CanvasCopyInputBindings(
                entity, action, device, values, count);
        }
        if (currentCount < count) {
            Array.Resize(ref bindings, Mathf.Max(currentCount, 0));
        }
        return bindings;
    }

    internal static void CanvasSetInputBindingsValue(
        NativeEntity entity, int action, int device, ReadOnlySpan<int> bindings) {

        if (CanvasSetInputBindings == null) {
            return;
        }
        if (bindings.Length == 0) {
            CanvasSetInputBindings(entity, action, device, null, 0);
            return;
        }
        fixed (int* values = bindings) {
            CanvasSetInputBindings(
                entity, action, device, values, bindings.Length);
        }
    }

    internal static int ReadCanvasNavigationTableSize(
        NativeEntity entity, out int rows, out int columns) {

        int rowValue = 0;
        int columnValue = 0;
        if (CanvasGetNavigationTableSize == null) {
            rows = 0;
            columns = 0;
            return 1;
        }
        int result = CanvasGetNavigationTableSize(
            entity, &rowValue, &columnValue);
        rows = rowValue;
        columns = columnValue;
        return result;
    }

    internal static int ResizeCanvasNavigationTableValue(
        NativeEntity entity, int rows, int columns) {

        return CanvasResizeNavigationTable != null ?
            CanvasResizeNavigationTable(entity, rows, columns) : 1;
    }

    internal static int ReadCanvasNavigationCell(
        NativeEntity entity, int row, int column, out GameObject? target) {

        NativeEntity nativeTarget = NativeEntity.Null;
        int result = CanvasGetNavigationCell != null ?
            CanvasGetNavigationCell(entity, row, column, &nativeTarget) : 1;
        target = GameObject.FromNative(nativeTarget);
        return result;
    }

    internal static int WriteCanvasNavigationCell(
        NativeEntity entity, int row, int column, GameObject? target) {

        return CanvasSetNavigationCell != null ?
            CanvasSetNavigationCell(entity, row, column, GameObject.RawNative(target)) : 1;
    }

    internal static bool ReadCanvasScreenToLocalPoint(
        NativeEntity entity, Vector2 screenPosition, out Vector2 localPosition) {

        localPosition = Vector2.zero;
        if (CanvasScreenToLocalPoint == null) {
            return false;
        }
        NativeVector2 nativePosition = default;
        if (CanvasScreenToLocalPoint(
            entity, NativeVector2.From(screenPosition), &nativePosition) == 0) {
            return false;
        }
        localPosition = nativePosition.ToVector2();
        return true;
    }
}
