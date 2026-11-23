using NEMEngine;

public static class ProductInteractions {
    private static readonly string? report = Environment.GetEnvironmentVariable("NEM_PRODUCT_INTERACTIONS_REPORT");
    private static int frame;
    private static bool entered, exited, settled, ready, navigated, submitted, clicked, passed;
    private static GameObject canvas = null!, left = null!, right = null!;
    private static GameObject body = null!, sensor = null!, floor = null!;

    private static GameObject Clone2D(GameObject source, string name, float x, float y) {
        var clone = NEMEngine.Object.Instantiate(source, new Vector3(x, y, 0), Quaternion.identity);
        clone.name = name;
        return clone;
    }

    private static void Record(string message) => File.AppendAllText(report!, message + Environment.NewLine);

    private static void Create() {
        var source = GameObject.Find("Heading") ?? throw new InvalidOperationException("Missing 2D heading.");
        body = Clone2D(source, "Fall", 4000, 302);
        sensor = Clone2D(source, "Sensor", 4000, 302);
        floor = Clone2D(source, "Floor", 4000, 340);
        foreach (var item in new[] { body, sensor, floor }) item.AddComponent<CollisionComponent>();
        body.AddComponent<Rigidbody2D>();

        canvas = new GameObject("Interaction Canvas");
        canvas.AddComponent<Canvas>();
        left = Clone2D(source, "Left", 160, 260);
        right = Clone2D(source, "Right", 760, 260);
        foreach (var item in new[] { left, right }) {
            item.SetParent(canvas);
            item.AddComponent<UISelectable>();
            item.AddComponent<UITextButton>();
        }
    }

    private static void ConfigureQuad(GameObject item, Vector2 halfSize, bool isStatic, bool trigger) {
        var collision = item.GetComponent<CollisionComponent>()!;
        collision.IsStatic = isStatic;
        collision.Shape.Type = ColliderShapeType.Quad2D;
        collision.Shape.HalfSize2D = halfSize;
        collision.Shape.IsTrigger = trigger;
        collision.Shape.Enabled = true;
    }

    private static void Configure() {
        ConfigureQuad(body, new Vector2(4, 4), false, false);
        ConfigureQuad(sensor, new Vector2(16, 5), true, true);
        ConfigureQuad(floor, new Vector2(16, 8), true, false);
        body.transform.localPosition = new Vector3(4000, 302, 0);
        var rigid = body.GetComponent<Rigidbody2D>()!;
        rigid.BodyType = RigidbodyType.Dynamic;
        rigid.UseGravity = false;
        rigid.Restitution = 0;
        rigid.LinearVelocity = Vector2.zero;
        foreach (var item in new[] {left, right}) {
            item.GetComponent<TextRenderer>()!.Text = item.name;
            item.GetComponent<UITextButton>()!.ActionName = item.name;
        }
        var ui = canvas.GetComponent<Canvas>()!;
        ui.Enabled = true;
        ui.KeyboardInputEnabled = true;
        ui.BlockInputAfterSubmit = false;
        ui.FirstSelected = left;
        ui.NavigationMode = CanvasNavigationMode.TransitionTable;
        ui.TransitionTable.Resize(1, 2);
        ui.TransitionTable[0, 0] = left;
        ui.TransitionTable[0, 1] = right;
        ui.SetKeyboardInputs(CanvasInputAction.Right, KeyCode.F6);
        ui.SetKeyboardInputs(CanvasInputAction.Submit, KeyCode.F7);
    }

    private static void Check() {
        var trigger = sensor.GetComponent<CollisionComponent>()!;
        if (!entered && trigger.IsColliding) {
            entered = true;
            Record("2D trigger entered");
            var rigid = body.GetComponent<Rigidbody2D>()!;
            rigid.UseGravity = true;
            rigid.LinearVelocity = new Vector2(0, 60);
        }
        if (entered && !exited && !trigger.IsColliding && 311 < body.transform.localPosition.y) {
            exited = true;
            Record("2D trigger exited");
        }
        if (!settled && exited && floor.GetComponent<CollisionComponent>()!.IsColliding &&
            Math.Abs(body.transform.localPosition.y - 328) < 0.1f && Math.Abs(body.GetComponent<Rigidbody2D>()!.LinearVelocity.y) < 0.01f) {
            settled = true;
            Record("2D solid contact and velocity OK");
        }
        var first = left.GetComponent<UISelectable>()!;
        var second = right.GetComponent<UISelectable>()!;
        if (!ready && first.State == UISelectableState.Selected) {
            ready = true;
            Record("UI ready");
        }
        if (ready && !navigated && second.State == UISelectableState.Selected) {
            navigated = true;
            Record("UI navigated");
        }
        if (navigated && !submitted && right.GetComponent<UITextButton>()!.ClickedThisFrame && second.SubmittedThisFrame) {
            submitted = true;
            Record("UI right button submitted");
        }
        if (submitted && !clicked && left.GetComponent<UITextButton>()!.ClickedThisFrame && first.SubmittedThisFrame) {
            clicked = true;
            Record("UI left button clicked");
        }
        if (clicked && settled) {
            Record("PASS");
            foreach (var item in new[] { canvas, body, sensor, floor }) item.Destroy();
            passed = true;
        }
    }

    public static bool Tick() {
        if (report == null || passed) return true;
        try {
            ++frame;
            if (frame == 1) Create();
            else if (frame == 3) Configure();
            else if (3 < frame) Check();
            if (3600 < frame) throw new TimeoutException($"UI/physics timeout y={body.transform.localPosition.y}, entered={entered}, exited={exited}, settled={settled}, ready={ready}, navigated={navigated}, submitted={submitted}");
            return passed;
        } catch (Exception error) {
            Record("FAIL " + error);
            throw;
        }
    }
}
