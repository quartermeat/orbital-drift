// Renders every prop alone on a plain ground, one PNG each, for the vision
// model to judge:  build/propsheet artifacts/props
#include "propdraw.hpp"
#include <cstdio>
#include <string>

using namespace orbital;

int main(int argc, char** argv) {
    std::string dir = argc > 1 ? argv[1] : "artifacts/props";
    SetTraceLogLevel(LOG_WARNING);
    SetConfigFlags(FLAG_WINDOW_HIDDEN);
    InitWindow(640, 640, "props");
    if (!IsWindowReady()) { std::fprintf(stderr, "no window\n"); return 1; }
    RenderTexture2D sheet = LoadRenderTexture(640, 640);
    // A neutral ground and generous framing: the test is whether the object
    // reads, not whether it survives a busy background.
    for (int kind = 0; kind < PropKindCount; ++kind) {
        Prop prop{};
        prop.kind = PropKind(kind);
        prop.size = 360;
        BeginTextureMode(sheet);
        ClearBackground(Color{238, 236, 230, 255});
        drawProp(prop, {320, 470}, 360, Rgb{176, 96, 82});
        EndTextureMode();
        Image out = LoadImageFromTexture(sheet.texture);
        ImageFlipVertical(&out);
        std::string path = dir + "/" + std::to_string(kind) + "-" + propName(PropKind(kind)) + ".png";
        for (char& c : path) if (c == ' ') c = '-';
        if (!ExportImage(out, path.c_str())) { std::fprintf(stderr, "cannot write %s\n", path.c_str()); return 1; }
        UnloadImage(out);
    }
    UnloadRenderTexture(sheet);
    CloseWindow();
    std::printf("wrote %d prop images to %s\n", PropKindCount, dir.c_str());
    return 0;
}
