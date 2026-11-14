#include "../Source/MpcFxEngine.h"
#include <cassert>
#include <cmath>
#include <limits>

int main() {
    vsl::MpcFxEngine fx;
    fx.prepare(48000.0);
    for (int type = 0; type <= 4; ++type) {
        fx.reset();
        float l[256]{}, r[256]{};
        l[0] = 1.0f;
        r[0] = -1.0f;
        fx.process(l, r, 256, type, 0.75f, 1.0f);
        for (int i = 0; i < 256; ++i) {
            assert(std::isfinite(l[i]) && std::isfinite(r[i]));
            assert(std::fabs(l[i]) <= 4.0f && std::fabs(r[i]) <= 4.0f);
        }
        if (type == 0) assert(l[0] == 1.0f && r[0] == -1.0f);
        if (type == 1) assert(l[0] < 1.0f && l[1] > 0.0f);
        if (type == 2) assert(l[0] < 1.0f && l[1] < 0.0f);
        if (type == 4) assert(l[0] != 0.0f && l[1] == l[0]);
    }
    fx.reset();
    float silenceL[128]{}, silenceR[128]{};
    fx.process(silenceL, silenceR, 128, 3, 1.0f, 1.0f);
    for (int i = 0; i < 128; ++i) assert(silenceL[i] == 0.0f && silenceR[i] == 0.0f);

    float dryL[2]{0.125f, -0.25f}, dryR[2]{-0.125f, 0.25f};
    fx.process(dryL, dryR, 2, 3, 1.0f, 0.0f);
    assert(dryL[0] == 0.125f && dryR[1] == 0.25f);
    fx.process(dryL, dryR, 2, 777, 1.0f, 1.0f);
    assert(dryL[0] == 0.125f && dryR[1] == 0.25f);
    float malformed[3]{std::numeric_limits<float>::quiet_NaN(),
                       std::numeric_limits<float>::infinity(), 0.5f};
    fx.process(malformed, malformed, 3, 3, std::numeric_limits<float>::quiet_NaN(), 1.0f);
    for (float x : malformed) assert(std::isfinite(x));
    fx.process(nullptr, malformed, 3, 1, 1.0f, 1.0f);
    fx.process(malformed, nullptr, 3, 1, 1.0f, 1.0f);
    fx.process(malformed, malformed, -1, 1, 1.0f, 1.0f);
}
