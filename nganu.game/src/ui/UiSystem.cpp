#include "ui/UiSystem.h"

#include <algorithm>

namespace Ui {

void UiSystem::Add(std::unique_ptr<Widget> widget) {
    widgets_.push_back(std::move(widget));
    SortByLayer();
}

void UiSystem::Remove(const std::string& id) {
    widgets_.erase(
        std::remove_if(widgets_.begin(), widgets_.end(),
            [&id](const std::unique_ptr<Widget>& w) { return w->Id() == id; }),
        widgets_.end());
    SortByLayer();
}

Widget* UiSystem::Find(const std::string& id) const {
    for (const auto& w : widgets_) {
        if (w->Id() == id) return w.get();
    }
    return nullptr;
}

void UiSystem::Update(float dt) {
    inputConsumed_ = false;
    Vector2 mouse = GetMousePosition();
    Widget* topHit = nullptr;

    for (auto it = widgets_.rbegin(); it != widgets_.rend(); ++it) {
        Widget* widget = it->get();
        if (!widget->IsVisible()) continue;
        if (!widget->HitTest(mouse)) continue;
        topHit = widget;
        if (widget->BlocksLowerLayers()) {
            inputConsumed_ = true;
        }
        break;
    }

    if (topHit != nullptr && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
        BringToFront(topHit);
    }

    for (auto& w : widgets_) {
        if (!w->IsVisible()) continue;
        w->Update(dt);
    }
}

void UiSystem::Draw() const {
    for (const auto& w : widgets_) {
        if (w->IsVisible()) w->Draw();
    }
}

bool UiSystem::HasVisibleWidget() const {
    for (const auto& w : widgets_) {
        if (w->IsVisible()) {
            return true;
        }
    }
    return false;
}

void UiSystem::SortByLayer() {
    std::stable_sort(widgets_.begin(), widgets_.end(),
                     [](const std::unique_ptr<Widget>& a, const std::unique_ptr<Widget>& b) {
                         return static_cast<int>(a->GetLayer()) < static_cast<int>(b->GetLayer());
                     });
}

void UiSystem::BringToFront(Widget* widget) {
    if (widget == nullptr) {
        return;
    }

    size_t fromIndex = widgets_.size();
    for (size_t i = 0; i < widgets_.size(); ++i) {
        if (widgets_[i].get() == widget) {
            fromIndex = i;
            break;
        }
    }
    if (fromIndex >= widgets_.size()) {
        return;
    }

    const Layer layer = widgets_[fromIndex]->GetLayer();
    size_t targetIndex = fromIndex;
    for (size_t i = fromIndex + 1; i < widgets_.size(); ++i) {
        if (widgets_[i]->GetLayer() != layer) {
            break;
        }
        targetIndex = i;
    }

    if (targetIndex == fromIndex) {
        return;
    }

    std::unique_ptr<Widget> owned = std::move(widgets_[fromIndex]);
    widgets_.erase(widgets_.begin() + static_cast<std::ptrdiff_t>(fromIndex));
    widgets_.insert(widgets_.begin() + static_cast<std::ptrdiff_t>(targetIndex), std::move(owned));
}

} // namespace Ui
