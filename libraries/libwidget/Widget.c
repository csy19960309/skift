#include <libgraphic/Painter.h>
#include <libmath/MinMax.h>
#include <libsystem/assert.h>
#include <libsystem/io/Stream.h>
#include <libsystem/logger.h>
#include <libwidget/Event.h>
#include <libwidget/Theme.h>
#include <libwidget/Widget.h>
#include <libwidget/Window.h>

static Font *_widget_font = NULL;
Font *widget_font(void)
{
    if (_widget_font == NULL)
    {
        _widget_font = font_create("sans");
    }

    return _widget_font;
}

void widget_initialize(
    Widget *widget,
    const WidgetClass *klass,
    Widget *parent)
{
    assert(widget != NULL);
    assert(klass != NULL);

    widget->enabled = true;
    widget->klass = klass;
    widget->childs = list_create();
    widget->bound = RECTANGLE_SIZE(32, 32);

    if (parent != NULL)
    {
        widget->window = parent->window;
        widget_add_child(parent, widget);
    }
}

void widget_destroy(Widget *widget)
{
    if (widget->klass->destroy)
    {
        widget->klass->destroy(widget);
    }

    Widget *child = NULL;
    while (list_peek(widget->childs, (void **)&child))
    {
        widget_destroy(child);
    }

    list_destroy(widget->childs);

    if (widget->parent)
    {
        widget_remove_child(widget->parent, widget);
    }

    if (widget->window)
    {
        window_widget_removed(widget->window, widget);
    }

    free(widget);
}

void widget_invalidate_layout(Widget *widget)
{
    if (widget->window)
    {
        window_schedule_layout(widget->window);
    }
}

void widget_add_child(Widget *widget, Widget *child)
{
    assert(child->parent == NULL);

    child->parent = widget;
    child->window = widget->window;
    list_pushback(widget->childs, child);

    widget_invalidate_layout(widget);
}

void widget_remove_child(Widget *widget, Widget *child)
{
    assert(child->parent == widget);

    child->parent = NULL;
    child->window = NULL;
    list_remove(widget->childs, child);

    widget_invalidate_layout(widget);
}

void widget_dump(Widget *widget, int depth)
{
    for (int i = 0; i < depth; i++)
    {
        printf("\t");
    }

    if (widget == NULL)
    {
        printf("<null>\n");
        return;
    }

    printf("%s(0x%08x) (%d, %d) %dx%d\n",
           widget->klass->name,
           widget,
           widget->bound.x,
           widget->bound.y,
           widget->bound.width,
           widget->bound.height);

    list_foreach(Widget, child, widget->childs)
    {
        widget_dump(child, depth + 1);
    }
}

void widget_event(Widget *widget, Event *event)
{
    if (widget->klass->event)
    {
        widget->klass->event(widget, event);
    }

    if (!event->accepted && widget->handlers[event->type].callback != NULL)
    {
        event->accepted = true;
        widget->handlers[event->type].callback(
            widget->handlers[event->type].target,
            widget,
            event);
    }

    if (!event->accepted && widget->parent)
    {
        widget_event(widget->parent, event);
    }
}

void widget_paint(Widget *widget, Painter *painter, Rectangle rectangle)
{
    painter_push_clip(painter, widget_get_bound(widget));

    if (widget->klass->paint)
    {
        widget->klass->paint(widget, painter, rectangle);
    }

    list_foreach(Widget, child, widget->childs)
    {
        if (rectangle_colide(rectangle, child->bound))
        {
            widget_paint(child, painter, rectangle);
        }
    }

    //painter_draw_rectangle(painter, widget_get_bound(widget), COLOR_CYAN);

    painter_pop_clip(painter);
}

static Vec2i widget_compute_size(Widget *widget)
{
    int width = 0;
    int height = 0;

    if (widget->klass->size)
    {
        return widget->klass->size(widget);
    }
    else if (widget->layout.type == LAYOUT_STACK)
    {

        width = widget->bound.width;
        height = widget->bound.height;

        list_foreach(Widget, child, widget->childs)
        {
            Vec2i child_size = widget_compute_size(child);

            width = MAX(width, child_size.x);
            height = MAX(height, child_size.y);
        }
    }
    else
    {
        if (widget->layout.type == LAYOUT_STACK)
        {
            width = widget->bound.width;
            height = widget->bound.height;
        }

        list_foreach(Widget, child, widget->childs)
        {
            Vec2i child_size = widget_compute_size(child);

            switch (widget->layout.type)
            {
            case LAYOUT_HFLOW:
            case LAYOUT_HGRID:
                width += child_size.x;
                height = MAX(height, child_size.y);
                break;

            case LAYOUT_VFLOW:
            case LAYOUT_VGRID:
                width = MAX(width, child_size.x);
                height += child_size.y;
                break;

            default:
                width = MAX(width, child_size.x);
                height = MAX(height, child_size.y);
                break;
            }
        }

        if (widget->layout.type == LAYOUT_HFLOW || widget->layout.type == LAYOUT_HGRID)
        {
            width += widget->layout.spacing.x * (list_count(widget->childs) - 1);
        }

        if (widget->layout.type == LAYOUT_VFLOW || widget->layout.type == LAYOUT_VGRID)
        {
            height += widget->layout.spacing.y * (list_count(widget->childs) - 1);
        }
    }

    width += widget->insets.left;
    width += widget->insets.right;

    height += widget->insets.top;
    height += widget->insets.bottom;

    if (widget->max_width)
    {
        width = MIN(width, widget->max_width);
    }

    if (widget->max_height)
    {
        height = MIN(height, widget->max_height);
    }

    if (widget->min_width)
    {
        width = MAX(width, widget->min_width);
    }

    if (widget->min_height)
    {
        height = MAX(height, widget->min_height);
    }

    return vec2i(width, height);
}

static void widget_do_vhgrid_layout(Widget *widget, Layout layout, Dimension dim)
{
    Dimension ivdim = dimension_invert_xy(dim);

    int current = widget_get_content_bound(widget).position.components[dim];

    int used_space_without_spacing =
        widget_get_content_bound(widget).size.components[dim] -
        (layout.spacing.components[dim] * (list_count(widget->childs) - 1));

    int child_size = used_space_without_spacing / list_count(widget->childs);

    int used_space_with_spacing =
        child_size * list_count(widget->childs) +
        (layout.spacing.components[dim] * (list_count(widget->childs) - 1));

    int correction_space =
        widget_get_content_bound(widget).size.components[dim] -
        used_space_with_spacing;

    list_foreach(Widget, child, widget->childs)
    {
        int current_child_size = MAX(1, child_size);
        if (correction_space > 0)
        {
            current_child_size++;
            correction_space--;
        }

        child->bound.position.components[dim] = current;

        child->bound.position.components[ivdim] =
            widget_get_content_bound(widget).position.components[ivdim];

        child->bound.size.components[dim] = current_child_size;

        child->bound.size.components[ivdim] =
            widget_get_content_bound(widget).size.components[ivdim];

        current += current_child_size + layout.spacing.components[dim];
    }
}

void widget_layout(Widget *widget)
{
    if (widget->klass->layout)
    {
        widget->klass->layout(widget);
        return;
    }

    if (list_count(widget->childs) == 0)
        return;

    Layout layout = widget->layout;

    switch (layout.type)
    {
    case LAYOUT_STACK:
        list_foreach(Widget, child, widget->childs)
        {
            child->bound = widget_get_content_bound(widget);
        }
        break;
    case LAYOUT_GRID:
    {
        int originX = widget_get_content_bound(widget).x;
        int originY = widget_get_content_bound(widget).y;

        int child_width = (widget_get_content_bound(widget).width - (layout.spacing.components[DIM_X] * (layout.hcell - 1))) / layout.hcell;
        int child_height = (widget_get_content_bound(widget).height - (layout.spacing.components[DIM_Y] * (layout.vcell - 1))) / layout.vcell;

        int index = 0;
        list_foreach(Widget, child, widget->childs)
        {
            int x = index % layout.hcell;
            int y = index / layout.hcell;

            child->bound = RECTANGLE(
                originX + x * (child_width + layout.spacing.components[DIM_X]),
                originY + y * (child_height + layout.spacing.components[DIM_Y]),
                child_width,
                child_height);

            index++;
        }
    }
    break;

    case LAYOUT_HGRID:
        widget_do_vhgrid_layout(widget, layout, DIM_X);
        break;

    case LAYOUT_VGRID:
        widget_do_vhgrid_layout(widget, layout, DIM_Y);
        break;

    case LAYOUT_HFLOW:
    {
        int fixed_child_count = 0;
        int fixed_child_total_width = 0;

        int fill_child_count = 0;

        list_foreach(Widget, child, widget->childs)
        {
            if (child->layout_attributes & LAYOUT_FILL)
            {
                fill_child_count++;
            }
            else
            {
                fixed_child_count++;
                fixed_child_total_width += widget_compute_size(child).x;
            }
        }

        int usable_space =
            widget_get_content_bound(widget).width -
            layout.spacing.components[DIM_X] * (list_count(widget->childs) - 1);

        int fill_child_total_width = MAX(0, usable_space - fixed_child_total_width);

        int fill_child_width = (fill_child_total_width) / MAX(1, fill_child_count);

        int current = widget_get_content_bound(widget).x;

        list_foreach(Widget, child, widget->childs)
        {
            if (child->layout_attributes & LAYOUT_FILL)
            {
                child->bound = RECTANGLE(
                    current,
                    widget_get_content_bound(widget).position.y,
                    fill_child_width,
                    widget_get_content_bound(widget).height);

                current += fill_child_width + layout.spacing.components[DIM_X];
            }
            else
            {
                child->bound = RECTANGLE(
                    current,
                    widget_get_content_bound(widget).position.y,
                    widget_compute_size(child).x,
                    widget_get_content_bound(widget).height);

                current += widget_compute_size(child).x + layout.spacing.components[DIM_X];
            }
        }
    }
    break;

    case LAYOUT_VFLOW:
    {
        int fixed_child_count = 0;
        int fixed_child_total_height = 0;

        int fill_child_count = 0;

        list_foreach(Widget, child, widget->childs)
        {
            if (child->layout_attributes & LAYOUT_FILL)
            {
                fill_child_count++;
            }
            else
            {
                fixed_child_count++;
                fixed_child_total_height += widget_compute_size(child).y;
            }
        }

        int usable_space =
            widget_get_content_bound(widget).height -
            layout.spacing.components[DIM_Y] * (list_count(widget->childs) - 1);

        int fill_child_total_height = MAX(0, usable_space - fixed_child_total_height);

        int fill_child_height = (fill_child_total_height) / MAX(1, fill_child_count);

        int current = widget_get_content_bound(widget).y;

        list_foreach(Widget, child, widget->childs)
        {
            if (child->layout_attributes & LAYOUT_FILL)
            {
                child->bound = RECTANGLE(
                    widget_get_content_bound(widget).position.x,
                    current,
                    widget_get_content_bound(widget).width,
                    fill_child_height);

                current += fill_child_height + layout.spacing.components[DIM_Y];
            }
            else
            {
                child->bound = RECTANGLE(
                    widget_get_content_bound(widget).position.x,
                    current,
                    widget_get_content_bound(widget).width,
                    widget_compute_size(child).y);

                current += widget_compute_size(child).y + layout.spacing.components[DIM_Y];
            }
        }
    }
    break;

    default:
        break;
    }

    list_foreach(Widget, child, widget->childs)
    {
        widget_layout(child);
    }
}

void widget_set_focus(Widget *widget)
{
    if (widget->window)
    {
        window_set_focused_widget(widget->window, widget);
    }
}

void widget_update(Widget *widget)
{
    if (widget->window)
    {
        window_schedule_update(widget->window, widget->bound);
    }
}

void widget_update_region(Widget *widget, Rectangle bound)
{
    if (widget->window)
    {
        window_schedule_update(widget->window, bound);
    }
}

Rectangle __widget_get_bound(Widget *widget)
{
    return widget->bound;
}

Rectangle __widget_get_content_bound(Widget *widget)
{
    return rectangle_shrink(__widget_get_bound(widget), widget->insets);
}

Widget *widget_get_child_at(Widget *parent, Vec2i position)
{
    list_foreach(Widget, child, parent->childs)
    {
        if (rectangle_containe_point(widget_get_bound(child), position))
        {
            return widget_get_child_at(child, position);
        }
    }

    return parent;
}

void widget_set_event_handler(Widget *widget, EventType event, EventHandler handler)
{
    assert(event < __EVENT_TYPE_COUNT);

    widget->handlers[event] = handler;
}

void widget_clear_event_handler(Widget *widget, EventType event)
{
    assert(event < __EVENT_TYPE_COUNT);

    widget->handlers[event].target = NULL;
    widget->handlers[event].callback = NULL;
}

Color __widget_get_color(Widget *widget, ThemeColorRole role)
{
    if (!widget->enabled || (widget->parent && !widget->parent->enabled))
    {
        if (role == THEME_FOREGROUND)
        {
            role = THEME_FOREGROUND_INACTIVE;
        }

        if (role == THEME_SELECTION)
        {
            role = THEME_SELECTION_INACTIVE;
        }

        if (role == THEME_ACCENT)
        {
            role = THEME_ACCENT_INACTIVE;
        }
    }

    if (widget->colors[role].overwritten)
    {
        return widget->colors[role].color;
    }

    return window_get_color(widget->window, role);
}

void __widget_set_color(Widget *widget, ThemeColorRole role, Color color)
{
    widget->colors[role].overwritten = true;
    widget->colors[role].color = color;

    widget_update(widget);
}

void widget_set_enable(Widget *widget, bool enable)
{
    if (widget->enabled != enable)
    {
        widget->enabled = enable;
        widget_update(widget);
    }
}

bool widget_is_enable(Widget *widget)
{
    return widget->enabled;
}

void widget_disable(Widget *widget)
{
    widget_set_enable(widget, false);
}

void widget_enable(Widget *widget)
{
    widget_set_enable(widget, true);
}