// Desert Roulette: first-person 3D Russian roulette vs 4 bots (C++ / raylib 5.0).

#include "raylib.h"
#include "raymath.h"
#include "rlgl.h"
#include <vector>
#include <string>
#include <cmath>
#include <ctime>
#include <algorithm>

static const int   N        = 5;      // 1 human + 4 bots
static const float SEAT_R   = 5.2f;
static const float TABLE_R  = 4.0f;
static const float TABLE_Y  = 1.5f;
static const float HEAD_Y   = 2.35f;
static const float AIM_TIME = 1.6f;
static const float RESULT_TIME = 2.8f;

static float frand(float a, float b) { return a + (b - a) * (GetRandomValue(0, 10000) / 10000.0f); }

struct Player   { const char* name; Color shirt; Color hat; bool human; bool alive; Vector3 pos; float angle; };
struct Confetti { Vector3 p, v, axis; Color c; float life, rot, rotSpeed; bool landed; };
struct Dune     { Vector3 p, s; Color c; };
struct Cactus   { Vector3 p; float h; };
struct Rock     { Vector3 p; float s; };

enum Phase { SELECT, AIM, RESULT, GAMEOVER };

static Player pl[N];
static std::vector<Confetti> confetti;
static std::vector<Dune> dunes;
static std::vector<Cactus> cacti;
static std::vector<Rock> rocks;

static bool chamber[6];
static int  chamberIdx = 0;
static int  turn = 0, shooter = 0, target = 1, selTarget = 1, winner = -1;
static Phase phase = SELECT;
static float timer = 0, botThink = 1.5f, shake = 0, flash = 0, burstT = 99, gTime = 0;
static Vector3 burstPos = {0, 0, 0};
static bool lastBang = false, bonus = false;
static std::string msg;
static float camYaw = PI, camPitch = -0.12f;   // first person, facing the table center

// ---------------------------------------------------------------- helpers
static Vector3 HeadPos(int i) { return { pl[i].pos.x, HEAD_Y, pl[i].pos.z }; }
static Vector3 ToCenter(int i) { return Vector3Normalize({ -pl[i].pos.x, 0, -pl[i].pos.z }); }
static std::string Nm(int i) { return pl[i].name; }
static std::string Obj(int i) { return pl[i].human ? "you" : Nm(i); }

static int AliveCount() { int c = 0; for (int i = 0; i < N; i++) if (pl[i].alive) c++; return c; }
static int NextAlive(int from) {
    for (int k = 1; k <= N; k++) { int j = (from + k) % N; if (pl[j].alive) return j; }
    return from;
}

static void Reload() {
    for (int i = 0; i < 6; i++) chamber[i] = false;
    chamber[GetRandomValue(0, 5)] = true;   // one bullet in a random chamber
    chamberIdx = 0;
}

static void StartTurnMsg() {
    if (pl[turn].human) msg = "Your turn! Choose who to shoot.";
    else msg = Nm(turn) + "'s turn...";
}

static void NewGame() {
    static const char* names[N] = { "You", "Bob", "Sara", "Jack", "Emma" };
    static const Color shirts[N] = { {60,120,230,255}, {220,70,70,255}, {80,180,100,255}, {240,170,40,255}, {170,90,200,255} };
    static const Color hats[N]   = { {120,80,40,255}, {70,50,40,255}, {200,170,120,255}, {50,50,60,255}, {150,60,60,255} };
    for (int i = 0; i < N; i++) {
        float th = i * (2.0f * PI / N);
        pl[i].name = names[i]; pl[i].shirt = shirts[i]; pl[i].hat = hats[i];
        pl[i].human = (i == 0); pl[i].alive = true;
        pl[i].pos = { SEAT_R * sinf(th), 0, SEAT_R * cosf(th) };
        pl[i].angle = th * RAD2DEG + 180.0f;   // face the table center
    }
    confetti.clear();
    Reload();
    turn = 0; shooter = 0; phase = SELECT; timer = 0; winner = -1;
    selTarget = NextAlive(0); lastBang = false; bonus = false; shake = 0; flash = 0; burstT = 99;
    botThink = frand(1.2f, 2.2f);
    StartTurnMsg();
}

static void InitScenery() {
    for (int i = 0; i < 18; i++) {
        float a = frand(0, 2 * PI), d = frand(35, 95);
        Color c = { (unsigned char)GetRandomValue(205, 235), (unsigned char)GetRandomValue(165, 190),
                    (unsigned char)GetRandomValue(105, 130), 255 };
        dunes.push_back({ { d * sinf(a), 0, d * cosf(a) }, { frand(14, 30), frand(2.5f, 7), frand(10, 22) }, c });
    }
    for (int i = 0; i < 12; i++) {
        float a = frand(0, 2 * PI), d = frand(12, 32);
        cacti.push_back({ { d * sinf(a), 0, d * cosf(a) }, frand(1.5f, 3.5f) });
    }
    for (int i = 0; i < 14; i++) {
        float a = frand(0, 2 * PI), d = frand(10, 40);
        rocks.push_back({ { d * sinf(a), 0, d * cosf(a) }, frand(0.3f, 0.9f) });
    }
}

// ---------------------------------------------------------------- confetti
static void SpawnConfetti(Vector3 at, int count) {
    for (int i = 0; i < count; i++) {
        Confetti c;
        c.p = { at.x + frand(-0.2f, 0.2f), at.y + frand(-0.2f, 0.3f), at.z + frand(-0.2f, 0.2f) };
        Vector3 d = Vector3Normalize({ frand(-1, 1), frand(-0.2f, 1.3f), frand(-1, 1) });
        c.v = Vector3Add(Vector3Scale(d, frand(3, 9)), { 0, 3, 0 });
        c.c = ColorFromHSV(frand(0, 360), 0.8f, 1.0f);
        c.life = 0; c.rot = frand(0, 360); c.rotSpeed = frand(-500, 500);
        c.axis = Vector3Normalize({ frand(-1, 1), frand(-1, 1), frand(-1, 1) });
        c.landed = false;
        confetti.push_back(c);
    }
    if (confetti.size() > 2500) confetti.erase(confetti.begin(), confetti.begin() + (confetti.size() - 2500));
}

static void UpdateConfetti(float dt) {
    for (auto& c : confetti) {
        c.life += dt;
        if (c.landed) continue;
        c.v.y -= 7.0f * dt;
        if (c.v.y < -2.2f) c.v.y = -2.2f;                 // flutter like paper
        c.v.x += sinf(gTime * 3 + c.rot) * dt * 2.0f;
        c.v.z += cosf(gTime * 3 + c.rot) * dt * 2.0f;
        c.v.x *= (1.0f - 0.8f * dt); c.v.z *= (1.0f - 0.8f * dt);
        Vector3 prev = c.p;
        c.p = Vector3Add(c.p, Vector3Scale(c.v, dt));
        c.rot += c.rotSpeed * dt;
        float d = sqrtf(c.p.x * c.p.x + c.p.z * c.p.z);
        float floorY = (d < TABLE_R && prev.y >= TABLE_Y) ? TABLE_Y + 0.04f : 0.02f;
        if (c.p.y <= floorY) { c.p.y = floorY; c.landed = true; }
    }
}

// ---------------------------------------------------------------- game logic
static void BeginAim(int t) {
    shooter = turn; target = t; phase = AIM; timer = 0;
    std::string who = (t == shooter) ? (pl[shooter].human ? "yourself" : "themself") : Obj(t);
    msg = (pl[shooter].human ? std::string("You aim") : Nm(shooter) + " aims") + " the gun at " + who + "...";
}

static void Fire() {
    bool bang = chamber[chamberIdx++];
    lastBang = bang;
    bonus = false;
    timer = 0; phase = RESULT;
    if (bang) {
        pl[target].alive = false;
        burstPos = HeadPos(target); burstT = 0;
        SpawnConfetti(burstPos, 420);
        shake = 1.0f; flash = 0.15f;
        if (pl[target].human) msg = "BANG! Your head explodes into confetti!";
        else msg = "BANG! " + Nm(target) + "'s head explodes into confetti!";
    } else {
        shake = 0.15f;
        if (target == shooter) {
            bonus = true;
            msg = pl[shooter].human ? "*click* Empty! You survive and get another turn!"
                                    : "*click* Empty! " + Nm(shooter) + " survives and gets another turn!";
        } else {
            msg = "*click* Empty! " + (pl[target].human ? std::string("You survive.") : Nm(target) + " survives.");
        }
    }
}

static void BotChoose() {
    std::vector<int> others;
    for (int i = 0; i < N; i++) if (pl[i].alive && i != turn) others.push_back(i);
    int t = turn;
    if (!others.empty() && frand(0, 1) > 0.25f) t = others[GetRandomValue(0, (int)others.size() - 1)];
    selTarget = t;
    BeginAim(t);
}

static void EndResult() {
    if (lastBang) Reload();
    if (AliveCount() <= 1) {
        phase = GAMEOVER; timer = 0;
        for (int i = 0; i < N; i++) if (pl[i].alive) winner = i;
        msg = (winner == 0) ? "CONGRATULATIONS! You win!" : "Winner: " + Nm(winner) + "!";
        return;
    }
    if (bonus && pl[shooter].alive) turn = shooter;
    else turn = NextAlive(shooter);
    phase = SELECT; timer = 0; botThink = frand(1.2f, 2.2f);
    if (!pl[selTarget].alive) selTarget = NextAlive(selTarget);
    StartTurnMsg();
}

static void UpdateGame(float dt, const Camera3D& cam) {
    gTime += dt; timer += dt;
    shake = std::max(0.0f, shake - dt * 1.8f);
    flash -= dt; burstT += dt;
    UpdateConfetti(dt);

    // first-person mouse look
    Vector2 md = GetMouseDelta();
    camYaw   -= md.x * 0.003f;
    camPitch  = Clamp(camPitch - md.y * 0.003f, -1.45f, 1.45f);

    switch (phase) {
    case SELECT:
        shooter = turn;
        if (pl[turn].human) {
            if (!pl[selTarget].alive) selTarget = NextAlive(selTarget);
            if (IsKeyPressed(KEY_RIGHT)) { do { selTarget = (selTarget + 1) % N; } while (!pl[selTarget].alive); }
            if (IsKeyPressed(KEY_LEFT))  { do { selTarget = (selTarget + N - 1) % N; } while (!pl[selTarget].alive); }
            for (int k = 0; k < N; k++) if (IsKeyPressed(KEY_ONE + k) && pl[k].alive) selTarget = k;
            if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
                Ray r = GetMouseRay({ GetScreenWidth() / 2.0f, GetScreenHeight() / 2.0f }, cam);   // crosshair
                int best = -1; float bd = 1e9f;
                for (int i = 0; i < N; i++) {
                    if (!pl[i].alive || i == 0) continue;
                    RayCollision rc = GetRayCollisionSphere(r, { pl[i].pos.x, 1.7f, pl[i].pos.z }, 1.1f);
                    if (rc.hit && rc.distance < bd) { bd = rc.distance; best = i; }
                }
                if (best >= 0) { if (best == selTarget) BeginAim(selTarget); else selTarget = best; }
            }
            if (phase == SELECT && (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_SPACE))) BeginAim(selTarget);
        } else if (timer > botThink) {
            BotChoose();
        }
        break;
    case AIM:
        if (timer >= AIM_TIME) Fire();
        break;
    case RESULT:
        if (timer >= RESULT_TIME) EndResult();
        break;
    case GAMEOVER:
        if (frand(0, 1) < 0.9f) SpawnConfetti({ frand(-4, 4), 9, frand(-4, 4) }, 3);
        if (IsKeyPressed(KEY_R)) NewGame();
        break;
    }
}

// ---------------------------------------------------------------- drawing
static void DrawTextC(const char* t, int cx, int y, int sz, Color c) {
    int w = MeasureText(t, sz);
    DrawText(t, cx - w / 2 + 2, y + 2, sz, Fade(BLACK, 0.7f));
    DrawText(t, cx - w / 2, y, sz, c);
}

static void DrawDesert() {
    DrawPlane({ 0, 0, 0 }, { 400, 400 }, { 236, 200, 140, 255 });
    // sun
    DrawSphere({ -60, 14, -150 }, 14, Fade({255, 240, 160, 255}, 0.25f));
    DrawSphere({ -60, 14, -150 }, 8, { 255, 245, 180, 255 });
    for (auto& d : dunes) {
        rlPushMatrix();
        rlTranslatef(d.p.x, d.p.y, d.p.z);
        rlScalef(d.s.x, d.s.y, d.s.z);
        DrawSphereEx({ 0, 0, 0 }, 1.0f, 20, 20, d.c);
        rlPopMatrix();
    }
    for (auto& r : rocks) {
        rlPushMatrix();
        rlTranslatef(r.p.x, 0, r.p.z);
        rlScalef(r.s * 1.4f, r.s, r.s * 1.1f);
        DrawSphereEx({ 0, 0.3f, 0 }, 1.0f, 8, 8, { 140, 110, 85, 255 });
        rlPopMatrix();
    }
    Color green = { 60, 130, 70, 255 };
    for (auto& c : cacti) {
        DrawCylinder(c.p, 0.35f, 0.35f, c.h, 10, green);
        DrawSphere({ c.p.x, c.h, c.p.z }, 0.35f, green);
        float y = c.h * 0.5f;
        DrawCylinderEx({ c.p.x, y, c.p.z }, { c.p.x + 0.8f, y, c.p.z }, 0.2f, 0.2f, 8, green);
        DrawCylinderEx({ c.p.x + 0.8f, y, c.p.z }, { c.p.x + 0.8f, y + c.h * 0.35f, c.p.z }, 0.2f, 0.2f, 8, green);
        DrawSphere({ c.p.x + 0.8f, y + c.h * 0.35f, c.p.z }, 0.2f, green);
    }
}

static void DrawTable() {
    DrawCylinder({ 0, 0.01f, 0 }, 7.0f, 7.0f, 0.01f, 48, Fade(BLACK, 0.2f));     // shadow
    Color wood = { 120, 76, 40, 255 };
    DrawCylinder({ 0, 0, 0 }, 0.6f, 1.0f, TABLE_Y - 0.15f, 20, { 90, 55, 30, 255 });
    DrawCylinder({ 0, TABLE_Y - 0.15f, 0 }, TABLE_R, TABLE_R, 0.15f, 64, wood);
    DrawCylinder({ 0, TABLE_Y, 0 }, TABLE_R - 0.3f, TABLE_R - 0.3f, 0.03f, 64, { 30, 110, 70, 255 });
    DrawCylinder({ 0, TABLE_Y + 0.03f, 0 }, 0.8f, 0.8f, 0.01f, 32, { 25, 90, 58, 255 });
}

static void DrawPlayer(int i, bool holdingGun) {
    const Player& p = pl[i];
    Color skin = { 240, 200, 160, 255 };
    Color shirt = p.alive ? p.shirt : Color{ (unsigned char)(p.shirt.r / 2), (unsigned char)(p.shirt.g / 2), (unsigned char)(p.shirt.b / 2), 255 };

    rlPushMatrix();
    rlTranslatef(p.pos.x, 0, p.pos.z);
    rlRotatef(p.angle, 0, 1, 0);

    // chair
    Color chair = { 100, 65, 35, 255 };
    DrawCube({ 0, 0.55f, -0.05f }, 1.2f, 0.12f, 1.0f, chair);
    DrawCube({ 0, 1.2f, -0.6f }, 1.2f, 1.3f, 0.12f, chair);
    for (int sx = -1; sx <= 1; sx += 2) for (int sz = -1; sz <= 1; sz += 2)
        DrawCube({ 0.5f * sx, 0.25f, -0.05f + 0.4f * sz }, 0.1f, 0.5f, 0.1f, chair);

    // body
    DrawCube({ 0, 1.25f, 0 }, 1.0f, 1.3f, 0.6f, shirt);

    if (p.alive) {
        // left arm always; right arm only when not holding the gun
        DrawCube({ -0.65f, 1.55f, 0.5f }, 0.25f, 0.25f, 1.0f, shirt);
        DrawSphere({ -0.65f, 1.55f, 1.05f }, 0.15f, skin);
        if (!holdingGun) {
            DrawCube({ 0.65f, 1.55f, 0.5f }, 0.25f, 0.25f, 1.0f, shirt);
            DrawSphere({ 0.65f, 1.55f, 1.05f }, 0.15f, skin);
        }
        // head + hat (skipped for the local player)
        if (!p.human) {
        DrawSphere({ 0, HEAD_Y, 0 }, 0.45f, skin);
        DrawSphere({ -0.15f, HEAD_Y + 0.07f, 0.38f }, 0.06f, BLACK);
        DrawSphere({ 0.15f, HEAD_Y + 0.07f, 0.38f }, 0.06f, BLACK);
        DrawCube({ 0, HEAD_Y - 0.12f, 0.43f }, 0.2f, 0.04f, 0.04f, { 150, 60, 60, 255 });
        DrawCylinder({ 0, HEAD_Y + 0.28f, 0 }, 0.85f, 0.85f, 0.05f, 24, p.hat);
        DrawCylinder({ 0, HEAD_Y + 0.3f, 0 }, 0.42f, 0.38f, 0.35f, 18, p.hat);
        }
    } else {
        // headless: neck stump, arms hanging
        DrawCylinder({ 0, 1.85f, 0 }, 0.18f, 0.18f, 0.12f, 10, skin);
        DrawCube({ -0.65f, 1.1f, 0.15f }, 0.25f, 0.9f, 0.25f, shirt);
        DrawCube({ 0.65f, 1.1f, 0.15f }, 0.25f, 0.9f, 0.25f, shirt);
    }
    rlPopMatrix();
}

static void GunPose(Vector3& gp, Vector3& gd) {
    int s = shooter;
    Vector3 tc = ToCenter(s);
    Vector3 rest  = Vector3Add(pl[s].pos, { tc.x * 1.0f, 1.75f, tc.z * 1.0f });
    Vector3 selfP = Vector3Add(pl[s].pos, { tc.x * 1.4f, HEAD_Y, tc.z * 1.4f });
    float e = 0;
    if (phase == AIM) e = Clamp(timer / 1.1f, 0, 1);
    else if (phase == RESULT) e = 1;
    e = e * e * (3 - 2 * e);
    Vector3 aimP = (target == s) ? selfP : rest;
    gp = Vector3Lerp(rest, aimP, e);
    if (phase == AIM && timer > 1.1f) {   // shaky hand
        gp.x += sinf(gTime * 55) * 0.015f; gp.y += cosf(gTime * 47) * 0.015f;
    }
    Vector3 neutral = Vector3Normalize({ tc.x, -0.15f, tc.z });
    Vector3 toT = Vector3Normalize(Vector3Subtract(HeadPos(target), gp));
    gd = Vector3Normalize(Vector3Lerp(neutral, toT, e));
}

static void DrawGun(Vector3 gp, Vector3 gd) {
    float yaw = atan2f(gd.x, gd.z) * RAD2DEG;
    float pitch = -asinf(Clamp(gd.y, -1, 1)) * RAD2DEG;
    rlPushMatrix();
    rlTranslatef(gp.x, gp.y, gp.z);
    rlRotatef(yaw, 0, 1, 0);
    rlRotatef(pitch, 1, 0, 0);
    DrawCube({ 0, 0, 0.4f }, 0.13f, 0.13f, 0.9f, { 60, 60, 65, 255 });
    DrawCube({ 0, -0.03f, -0.05f }, 0.18f, 0.2f, 0.4f, { 110, 110, 115, 255 });
    DrawSphere({ 0, 0, 0.05f }, 0.17f, { 170, 170, 175, 255 });
    DrawCube({ 0, -0.28f, -0.14f }, 0.12f, 0.4f, 0.15f, { 100, 60, 30, 255 });
    DrawSphere({ 0, -0.3f, -0.14f }, 0.13f, { 240, 200, 160, 255 });
    if (flash > 0 && lastBang) {
        DrawSphere({ 0, 0, 1.1f }, 0.38f, { 255, 190, 40, 255 });
        DrawSphere({ 0, 0, 1.1f }, 0.22f, WHITE);
    }
    rlPopMatrix();
}

static void DrawConfetti() {
    for (auto& c : confetti) {
        float fade = (c.life > 25.0f) ? Clamp(1.0f - (c.life - 25.0f) / 5.0f, 0, 1) : 1.0f;
        if (fade <= 0) continue;
        rlPushMatrix();
        rlTranslatef(c.p.x, c.p.y, c.p.z);
        if (!c.landed) rlRotatef(c.rot, c.axis.x, c.axis.y, c.axis.z);
        else rlRotatef(c.rot, 0, 1, 0);
        DrawCube({ 0, 0, 0 }, 0.14f, 0.02f, 0.09f, Fade(c.c, fade));
        rlPopMatrix();
    }
}

static void DrawHUD(const Camera3D& cam) {
    int W = GetScreenWidth(), H = GetScreenHeight();

    // name labels
    int pointed = -1;
    if (phase == SELECT && pl[turn].human) pointed = selTarget;
    else if (phase == AIM || phase == RESULT) pointed = target;

    for (int i = 0; i < N; i++) {
        if (i == 0) continue;
        Vector3 wp = { pl[i].pos.x, 3.7f, pl[i].pos.z };
        Vector3 fwd = Vector3Subtract(cam.target, cam.position);
        if (Vector3DotProduct(fwd, Vector3Subtract(wp, cam.position)) <= 0) continue;
        Vector2 sp = GetWorldToScreen(wp, cam);
        bool isTurn = (i == turn && phase != GAMEOVER);
        std::string label = pl[i].alive ? Nm(i) : Nm(i) + " (OUT)";
        if (isTurn && pl[i].alive) label = "> " + label + " <";
        Color c = !pl[i].alive ? Color{ 170, 170, 170, 255 } : (isTurn ? GOLD : WHITE);
        if (i == pointed && pl[i].alive) c = { 255, 90, 90, 255 };
        DrawTextC(label.c_str(), (int)sp.x, (int)sp.y, 22, c);
    }

    // main message
    DrawTextC(msg.c_str(), W / 2, 20, 30, WHITE);

    // status line
    std::string sub;
    if (phase == SELECT && pl[turn].human)
        sub = "Target: " + (selTarget == 0 ? std::string("YOURSELF") : Nm(selTarget)) +
              "   |   Click a player / Left,Right / 1-5 = select, click again or ENTER = FIRE";
    else if (phase == GAMEOVER) sub = "Press R to play again";
    else if (phase == SELECT) sub = Nm(turn) + " is choosing a target...";
    if (!sub.empty()) DrawTextC(sub.c_str(), W / 2, 62, 20, { 255, 230, 150, 255 });

    // chamber indicator
    DrawText("REVOLVER", 20, H - 80, 18, WHITE);
    for (int k = 0; k < 6; k++) {
        Vector2 c = { 40.0f + k * 32, (float)H - 40 };
        if (k < chamberIdx) { DrawCircleV(c, 12, DARKGRAY); DrawText("x", (int)c.x - 4, (int)c.y - 9, 18, LIGHTGRAY); }
        else { DrawCircleV(c, 12, Fade(WHITE, 0.25f)); DrawCircleLines((int)c.x, (int)c.y, 12, WHITE); DrawText("?", (int)c.x - 4, (int)c.y - 9, 18, WHITE); }
    }
    DrawText("Mouse = look around  |  1 = aim at yourself  |  ESC = quit", W - 480, H - 28, 16, Fade(BLACK, 0.7f));
    // crosshair
    DrawCircle(W / 2, H / 2, 3, Fade(WHITE, 0.9f));
    DrawCircleLines(W / 2, H / 2, 10, Fade(BLACK, 0.6f));
}

// ---------------------------------------------------------------- main
int main() {
    SetConfigFlags(FLAG_MSAA_4X_HINT | FLAG_VSYNC_HINT);
    InitWindow(1280, 720, "Desert Roulette - 3D Russian Roulette");
    SetTargetFPS(60);
    DisableCursor();
    SetRandomSeed((unsigned)time(NULL));
    InitScenery();
    NewGame();

    Camera3D cam = { 0 };
    cam.up = { 0, 1, 0 }; cam.fovy = 70; cam.projection = CAMERA_PERSPECTIVE;

    while (!WindowShouldClose()) {
        float dt = GetFrameTime();
        if (dt > 0.05f) dt = 0.05f;

        Vector3 eye = { pl[0].pos.x, HEAD_Y + 0.1f, pl[0].pos.z };
        Vector3 fwd = { sinf(camYaw) * cosf(camPitch), sinf(camPitch), cosf(camYaw) * cosf(camPitch) };
        if (shake > 0) {
            eye.x += frand(-1, 1) * shake * 0.12f;
            eye.y += frand(-1, 1) * shake * 0.12f;
            eye.z += frand(-1, 1) * shake * 0.12f;
        }
        cam.position = eye;
        cam.target = Vector3Add(eye, fwd);

        UpdateGame(dt, cam);

        BeginDrawing();
        ClearBackground({ 150, 200, 240, 255 });
        DrawRectangleGradientV(0, 0, GetScreenWidth(), GetScreenHeight(), { 70, 140, 225, 255 }, { 252, 225, 175, 255 });

        BeginMode3D(cam);
        DrawDesert();
        DrawTable();

        for (int i = 0; i < N; i++) {
            bool holding = (phase != GAMEOVER) && (i == shooter);
            DrawPlayer(i, holding);
        }
        if (phase != GAMEOVER) {
            Vector3 gp, gd;
            GunPose(gp, gd);
            DrawGun(gp, gd);
            // shooter arm
            Vector3 sh = Vector3Add(pl[shooter].pos,
                Vector3RotateByAxisAngle({ 0.6f, 1.6f, 0.1f }, { 0, 1, 0 }, pl[shooter].angle * DEG2RAD));
            DrawCylinderEx(sh, Vector3Add(gp, { 0, -0.25f, 0 }), 0.13f, 0.11f, 8, pl[shooter].shirt);
        }

        // brief white flash (no blood)
        if (burstT < 0.35f) {
            float t = burstT / 0.35f;
            DrawSphere(burstPos, 0.4f + t * 1.6f, Fade(WHITE, 0.8f * (1 - t)));
        }
        DrawConfetti();

        // target arrow
        int pointed = -1;
        if (phase == SELECT && pl[turn].human) pointed = selTarget;
        else if (phase == AIM) pointed = target;
        if (pointed > 0 && pl[pointed].alive) {
            float bob = sinf(gTime * 6) * 0.12f;
            Vector3 h = HeadPos(pointed);
            DrawCylinderEx({ h.x, 3.55f + bob, h.z }, { h.x, 3.05f + bob, h.z }, 0.3f, 0.0f, 12, RED);
        }
        EndMode3D();

        DrawHUD(cam);
        DrawFPS(10, 10);
        EndDrawing();
    }
    CloseWindow();
    return 0;
}