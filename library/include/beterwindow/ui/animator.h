#pragma once

#include "core.h"

namespace bwui {

// 简易开关/滑杆缓动动画。Step 在绘制时推进，ConsumeAnimating 供定时器判断是否继续重绘。
class Animator {
public:
    float Step(uint64_t id, bool on, float speed = 0.32f) {
        float target = on ? 1.f : 0.f;
        auto it = values_.find(id);
        if (it == values_.end()) {
            values_[id] = target;
            return target;
        }
        float& v = it->second;
        if (std::fabs(v - target) > 0.002f) {
            v += (target - v) * speed;
            if (std::fabs(v - target) <= 0.002f) v = target;
            animating_ = true;
        }
        return v;
    }

    bool ConsumeAnimating() {
        bool was = animating_;
        animating_ = false;
        return was;
    }

    void Clear() {
        values_.clear();
        animating_ = false;
    }

private:
    std::unordered_map<uint64_t, float> values_;
    bool animating_ = false;
};

}  // namespace bwui
