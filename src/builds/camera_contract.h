// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo
#pragma once
#include "discovery_image.h"

namespace rc_ht::builds::discovery {

struct TransformContract { unsigned update = 0, initialize = 0; };
struct FovContract {
    unsigned input = 0, output = 0, aspect = 0, update = 0, tangent = 0, arctangent = 0;
    bool horizontal = false;
};
bool InspectTransform(const Image& image, unsigned function, TransformContract& out);
bool InspectFov(const Image& image, unsigned function, FovContract& out);

}
