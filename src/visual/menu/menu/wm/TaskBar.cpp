/*
  Created on 06.07.18.
*/
#include <menu/object/container/Container.hpp>
#include <menu/wm/TaskBar.hpp>
#include <menu/wm/WMWindow.hpp>
#include <menu/wm/Task.hpp>
#include <menu/Menu.hpp>

namespace zerokernel_taskbar
{
static settings::RVariable<rgba_t> color_background{ "zk.style.taskbar.color.background", "1d2f40" };
static settings::RVariable<rgba_t> color_border{ "zk.style.taskbar.color.border", "446498ff" };
static settings::RVariable<int> position{ "zk.style.taskbar.position", "1" }; // 0 = top, 1 = bottom
} // namespace zerokernel_taskbar

void zerokernel::TaskBar::reorderElements()
{
    int acc{ 0 };
    for (auto &i : objects)
    {
        acc += i->getBoundingBox().margin.left;
        i->move(acc, i->getBoundingBox().margin.top);
        acc += i->getBoundingBox().getFullBox().width - i->getBoundingBox().margin.left;
    }
}

zerokernel::TaskBar::TaskBar(zerokernel::WindowManager &wm) : BaseMenuObject{}, wm(wm)
{
    bb.width.setFill();
    bb.height.setContent();
    updateLocation();
}

void zerokernel::TaskBar::updateLocation()
{
    if (*zerokernel_taskbar::position == 1) // Bottom
    {
        move(0, Menu::instance->screen_y - getBoundingBox().getFullBox().height);
    }
    else // Top
    {
        move(0, 0);
    }
}

bool zerokernel::TaskBar::isHidden()
{
    return BaseMenuObject::isHidden() || Menu::instance->isInGame();
}

void zerokernel::TaskBar::addWindowButton(zerokernel::WMWindow &window)
{
    addObject(std::make_unique<Task>(window));
}

void zerokernel::TaskBar::render()
{
    updateLocation();
    renderBackground(*zerokernel_taskbar::color_background);
    renderBorder(*zerokernel_taskbar::color_border);

    Container::render();
}
