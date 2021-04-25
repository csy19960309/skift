#pragma once

#include <libwidget/Element.h>

namespace Widget
{

struct Separator : public Element
{
    Separator();

    void paint(Graphic::Painter &, const Math::Recti &) override;

    Math::Vec2i size() override;
};

} // namespace Widget
