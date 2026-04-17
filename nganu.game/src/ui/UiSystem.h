#pragma once

/* ------------------------------------------------------------------ */
/* UiSystem.h — root widget manager: z-order, focus, input dispatch   */
/* ------------------------------------------------------------------ */

#include "ui/UiTypes.h"
#include "ui/UiWindowConfig.h"
#include "raylib.h"
#include <memory>
#include <string>
#include <vector>

namespace Ui {

/* ------------------------------------------------------------------ */
/* Base Widget                                                         */
/* ------------------------------------------------------------------ */
class Widget {
public:
    explicit Widget(std::string id) : id_(std::move(id)) {}
    virtual ~Widget() = default;

    Widget(const Widget&) = delete;
    Widget& operator=(const Widget&) = delete;

    const std::string& Id() const { return id_; }
    bool IsVisible() const { return visible_; }
    void SetVisible(bool v) { visible_ = v; }
    void Toggle() { visible_ = !visible_; }
    virtual void ApplyWindowConfig(const WindowConfig& config) { (void)config; }
    Layer GetLayer() const { return layer_; }
    void SetLayer(Layer layer) { layer_ = layer; }
    virtual bool HitTest(Vector2 point) const { return CheckCollisionPointRec(point, bounds); }
    virtual bool BlocksLowerLayers() const { return true; }

    Rect bounds {};   /* screen-space rect, set by parent or system */

    virtual void Update(float dt) = 0;
    virtual void Draw() const = 0;

protected:
    std::string id_;
    bool        visible_ = true;
    Layer       layer_ = Layer::Window;
};

/* ------------------------------------------------------------------ */
/* UiSystem — owns widgets, dispatches input, draw in z-order         */
/* ------------------------------------------------------------------ */
class UiSystem {
public:
    UiSystem() = default;
    ~UiSystem() = default;

    /* Add a widget (system takes ownership) */
    void Add(std::unique_ptr<Widget> widget);

    /* Remove by id */
    void Remove(const std::string& id);

    /* Find a widget by id (raw ptr, nullptr if not found) */
    Widget* Find(const std::string& id) const;

    void Update(float dt);
    void Draw() const;

    /* Returns true if any visible UI widget consumed the input this frame */
    bool ConsumedInput() const { return inputConsumed_; }
    bool HasVisibleWidget() const;

private:
    std::vector<std::unique_ptr<Widget>> widgets_;
    bool inputConsumed_ = false;

    void SortByLayer();
    void BringToFront(Widget* widget);
};

} // namespace Ui
