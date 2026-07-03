namespace NEMEngine;

// GameViewカメラのレイ生成。カメラ情報は前フレーム描画時点のスナップショットを参照する
public static class Camera {

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
