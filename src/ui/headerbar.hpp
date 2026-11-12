#pragma once

#include <adwaita.h>
#include <functional>
#include <string>

namespace dfiles::ui {

/**
 * @brief Header bar for the dfiles application window.
 *
 * Encapsulates AdwHeaderBar, AdwWindowTitle, toggle controls for Split View (F3),
 * Terminal (F4), Inspector (F11), and the primary application menu.
 */
class DFilesHeaderBar {
public:
    DFilesHeaderBar();
    ~DFilesHeaderBar();

    DFilesHeaderBar(const DFilesHeaderBar&) = delete;
    DFilesHeaderBar& operator=(const DFilesHeaderBar&) = delete;
    DFilesHeaderBar(DFilesHeaderBar&&) noexcept = default;
    DFilesHeaderBar& operator=(DFilesHeaderBar&&) noexcept = default;

    [[nodiscard]] GtkWidget* getWidget() const;
    [[nodiscard]] AdwHeaderBar* getHeaderBar() const;

    // Title & subtitle manipulation
    void setTitle(const std::string& title, const std::string& subtitle = "");

    // Button states
    void setSplitActive(bool active);
    [[nodiscard]] bool isSplitActive() const;

    void setTerminalActive(bool active);
    [[nodiscard]] bool isTerminalActive() const;

    void setInspectorActive(bool active);
    [[nodiscard]] bool isInspectorActive() const;

    // Callbacks for toggled actions
    std::function<void()> onSplitToggled;
    std::function<void()> onTerminalToggled;
    std::function<void()> onInspectorToggled;

private:
    void setupUI();
    void setupMenu();

    GtkWidget* m_headerBar{nullptr};
    GtkWidget* m_titleWidget{nullptr};
    GtkWidget* m_splitButton{nullptr};
    GtkWidget* m_terminalButton{nullptr};
    GtkWidget* m_inspectorButton{nullptr};
    GtkWidget* m_menuButton{nullptr};

    gulong m_splitToggledId{0};
    gulong m_terminalToggledId{0};
    gulong m_inspectorToggledId{0};

    bool m_blockSignals{false};
};

} // namespace dfiles::ui
