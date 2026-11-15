#include "beterwindow/ui/theme.h"

namespace bwui {

Theme Theme::Dark() {
    Theme t{};
    t.window = Rgb(0x0F1013);
    t.sidebar = Rgb(0x14151A);
    t.card = Rgb(0x17181D);
    t.cardDeep = Rgb(0x131419);
    t.hover = Rgb(0x1D1E25);
    t.border = Rgb(0x24252C);
    t.borderHi = Rgb(0x30323B);
    t.text = Rgb(0xECEDF2);
    t.sub = Rgb(0x9A9BA5);
    t.muted = Rgb(0x62646E);
    t.accent = Rgb(0x7C6CFF);
    t.accent2 = Rgb(0x4F8CFF);
    t.ok = Rgb(0x4ADE80);
    t.danger = Rgb(0xF87171);
    return t;
}

}  // namespace bwui
