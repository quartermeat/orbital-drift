// Renders a sheet of people so the art style can be judged at real sizes:
//   make figures
#include "figure.hpp"
#include <iostream>

using namespace orbital;

int main() {
    const int W = 2400, H = 1980;
    SetTraceLogLevel(LOG_WARNING);
    SetConfigFlags(FLAG_WINDOW_HIDDEN);
    InitWindow(900, 600, "figures");
    if (!IsWindowReady()) { std::cerr << "no window\n"; return 1; }
    Font font = LoadFontEx("assets/font.ttf", 48, nullptr, 0);
    RenderTexture2D sheet = LoadRenderTexture(W, H);
    auto label = [&](const char* s, float x, float y, float size, Color c) {
        DrawTextEx(font, s, {x, y}, size, 1.f, c);
    };
    Scene ground = generateScene(4, Rgb{175, 149, 246});
    Color grass{ground.grass.r, ground.grass.g, ground.grass.b, 255};

    BeginTextureMode(sheet);
    ClearBackground(Color{26, 32, 40, 255});
    DrawRectangle(0, 112, W, H - 112, grass);
    label("PEOPLE IN THIS ART STYLE", 40, 30, 38, Color{236, 242, 246, 255});
    label("no faces, skull-cap headwear, 24 poses", 40, 76, 22, Color{150, 170, 186, 255});

    // Every pose, named, at a size where the body is unambiguous.
    label("POSES", 40, 140, 22, Color{58, 72, 60, 255});
    for (int i = 0; i < PoseCount; ++i) {
        float x = 150 + float(i % 6) * 380, y = 300 + float(i / 6) * 248;
        Rng rng(4000u + uint64_t(i) * 31u);
        Figure figure = rollFigure(rng);
        figure.pose = static_cast<unsigned char>(i);
        drawFigure(figure, {x, y}, 150);
        label(Poses[i].name, x - 46, y + 16, 19, Color{52, 66, 54, 255});
    }

    // The same people at the sizes they will actually be drawn.
    float y = 1330;
    Rng rng(20260920u);
    for (float size : {104.f, 64.f, 44.f, 30.f, 20.f}) {
        char note[32];
        snprintf(note, sizeof note, "%dpx", int(size));
        label(note, 40, y - size * .5f, 20, Color{52, 66, 54, 255});
        float x = 150;
        for (int i = 0; i < 18; ++i) {
            drawFigure(rollFigure(rng), {x, y}, size);
            x += size * .82f + 26;
        }
        y += size * .55f + 74;
    }

    // A crowd with one Waldo in it: does the conjunction still work?
    float crowdY = H - 292;
    DrawRectangle(0, int(crowdY - 46), W, 292, Color{44, 56, 46, 255});
    label("CROWD AT 40px - one figure wears red/white stripes AND a red cap", 40, crowdY - 38, 22,
          Color{184, 198, 190, 255});
    Rng crowd(20260921u);
    int waldo = 12 + crowd.below(50);
    for (int i = 0; i < 72; ++i) {
        Figure figure = rollFigure(crowd);
        if (i == waldo) {
            figure.pattern = Pattern::Stripe;
            figure.shirt = 7; figure.stripe = 0;
            figure.wearsHat = true; figure.hat = 0;
        } else if (figure.pattern == Pattern::Stripe && figure.shirt == 7 && figure.stripe == 0
                   && figure.wearsHat && figure.hat == 0) {
            figure.stripe = 1;      // never a second true match
        }
        drawFigure(figure, {96.f + float(i % 18) * 116.f, crowdY + 60.f + float(i / 18) * 62.f}, 40);
    }
    EndTextureMode();

    Image out = LoadImageFromTexture(sheet.texture);
    ImageFlipVertical(&out);
    bool ok = ExportImage(out, "artifacts/figures.png");
    UnloadImage(out);
    UnloadRenderTexture(sheet);
    UnloadFont(font);
    CloseWindow();
    std::cout << (ok ? "artifacts/figures.png written\n" : "export failed\n");
    return ok ? 0 : 1;
}
