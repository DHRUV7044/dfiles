#pragma once

#include "views/pane_view.hpp"

#include <gtk/gtk.h>
#include <functional>
#include <memory>

namespace dfiles::views {

/**
 * @brief Coordinates dual-pane split browsing (F3).
 *
 * Encapsulates a horizontal GtkPaned containing a primary (left) pane
 * and a secondary (right) pane. Handles toggle visibility, focus cycling (Tab),
 * and active pane notification.
 */
class SplitView {
public:
    SplitView();
    ~SplitView();

    SplitView(const SplitView&) = delete;
    SplitView& operator=(const SplitView&) = delete;
    SplitView(SplitView&&) noexcept = default;
    SplitView& operator=(SplitView&&) noexcept = default;

    [[nodiscard]] GtkWidget* getWidget() const noexcept;
    [[nodiscard]] PaneView* getLeftPane() const noexcept;
    [[nodiscard]] PaneView* getRightPane() const noexcept;

    void toggleSplit();
    [[nodiscard]] bool isSplit() const noexcept { return m_isSplit; }

    [[nodiscard]] PaneView* getActivePane() const noexcept;
    [[nodiscard]] PaneView* getInactivePane() const noexcept;
    void setActivePane(PaneView* pane);
    void cycleFocus();

    // Cross-pane transfer placeholder (for Phase 5)
    void transferSelection(bool isMove);

    std::function<void(PaneView* activePane)> onActivePaneChanged;

private:
    void setupUI();

    GtkWidget* m_paned{nullptr};
    std::unique_ptr<PaneView> m_leftPane;
    std::unique_ptr<PaneView> m_rightPane;
    PaneView* m_activePane{nullptr};
    bool m_isSplit{false};
};

} // namespace dfiles::views
