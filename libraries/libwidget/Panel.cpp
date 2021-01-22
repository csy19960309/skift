#include <libgraphic/Painter.h>
#include <libsystem/Logger.h>
#include <libwidget/Panel.h>

Panel::Panel(Widget *parent)
    : Widget(parent)
{
}

void Panel::paint(Painter &painter, const WidgetMetrics &metrics, const Recti &)
{
    if (_border_radius > 0)
    {
        painter.fill_rectangle_rounded(metrics.bound, _border_radius, color(THEME_MIDDLEGROUND));
    }
    else
    {
        painter.clear(metrics.bound, color(THEME_MIDDLEGROUND));
    }
}
