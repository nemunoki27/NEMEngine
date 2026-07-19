namespace NEMEngine;

// GameViewカメラの座標変換とレイ生成、前フレーム描画時点のスナップショットを参照する
public static class Camera {

    // ワールド座標をGameViewピクセル座標へ変換する、失敗時はゼロを返す
    public static Vector3 WorldToScreenPoint(Vector3 worldPosition) {

        TryWorldToScreenPoint(worldPosition, out Vector3 screenPosition);
        return screenPosition;
    }

    // ワールド座標をGameViewピクセル座標へ変換しXYのみ返す
    public static Vector2 WorldToScreenPoint2D(Vector3 worldPosition) {

        TryWorldToScreenPoint(worldPosition, out Vector2 screenPosition);
        return screenPosition;
    }

    // ワールド座標をGameViewピクセル座標へ変換する、Zはカメラ前方距離
    public static bool TryWorldToScreenPoint(
        Vector3 worldPosition, out Vector3 screenPosition) {

        return NativeApi.ReadWorldToScreenPoint(worldPosition, out screenPosition);
    }

    // ワールド座標をGameViewピクセル座標へ変換しXYのみ返す
    public static bool TryWorldToScreenPoint(
        Vector3 worldPosition, out Vector2 screenPosition) {

        if (NativeApi.ReadWorldToScreenPoint(worldPosition, out Vector3 position)) {
            screenPosition = new Vector2(position.x, position.y);
            return true;
        }
        screenPosition = Vector2.zero;
        return false;
    }

    // GameViewピクセル座標(描画解像度基準・左上原点)からワールドレイを作る。カメラ未解決はfalse
    public static bool ScreenPointToRay(Vector2 screenPos, out Ray ray) {

        if (NativeApi.ReadScreenPointToRay(screenPos, out Vector3 origin, out Vector3 direction)) {
            ray = new Ray(origin, direction);
            return true;
        }
        ray = default;
        return false;
    }

    // GameView内のマウス座標を描画解像度基準で取得する。View外はfalse
    public static bool TryGetMousePositionInView(out Vector2 position) {
        return NativeApi.ReadMousePositionInView(out position);
    }

    // マウス位置からワールドレイを作る。GameView外やカメラ未解決はfalse
    public static bool TryGetMouseRay(out Ray ray) {

        if (TryGetMousePositionInView(out Vector2 position)) {
            return ScreenPointToRay(position, out ray);
        }
        ray = default;
        return false;
    }
}
