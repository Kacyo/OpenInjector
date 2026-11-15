#pragma once

#include "core.h"

#include <vector>

namespace bwui {

// 通用命中区域。KindT 由使用方定义（例如页面内控件类型枚举）。
template <typename KindT>
struct Hit {
    uint64_t id = 0;
    D2D1_RECT_F rect{};
    KindT kind{};
    int arg = 0;
    bool enabled = true;
};

// 每帧重建命中表，提交后在上一帧结果中反查，避免绘制与输入使用不同的布局数据。
template <typename KindT>
class HitRegistry {
public:
    void Clear() { current_.clear(); }

    void Add(uint64_t id, KindT kind, int arg, const D2D1_RECT_F& rect, bool enabled = true) {
        Hit<KindT> h;
        h.id = id;
        h.kind = kind;
        h.arg = arg;
        h.rect = rect;
        h.enabled = enabled;
        current_.push_back(h);
    }

    void Commit() {
        previous_.swap(current_);
        current_.clear();
    }

    const Hit<KindT>* HitAt(float x, float y) const {
        for (auto it = previous_.rbegin(); it != previous_.rend(); ++it) {
            if (it->enabled && PointIn(it->rect, x, y)) return &(*it);
        }
        return nullptr;
    }

    void Reset() {
        current_.clear();
        previous_.clear();
    }

private:
    std::vector<Hit<KindT>> current_;
    std::vector<Hit<KindT>> previous_;
};

}  // namespace bwui
