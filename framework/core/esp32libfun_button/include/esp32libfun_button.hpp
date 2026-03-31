#pragma once

namespace esp32libfun {

/// Base placeholder for the future button input API.
class Button {
public:
    Button() = default;

private:
    int pin_ = -1;
    bool active_low_ = true;
    bool configured_ = false;
};

extern Button button;

} // namespace esp32libfun

using esp32libfun::Button;
using esp32libfun::button;
