#include "views/split_view.hpp"

namespace dfiles::views {

SplitView::SplitView() {
    setupUI();
}

SplitView::~SplitView() {
    onActivePaneChanged = nullptr;
    if (m_leftPane) {
        m_leftPane->onActivated = nullptr;
        m_leftPane->onDirectoryChanged = nullptr;
    }
    if (m_rightPane) {
        m_rightPane->onActivated = nullptr;
        m_rightPane->onDirectoryChanged = nullptr;
    }
    m_activePane = nullptr;
    m_leftPane.reset();
    m_rightPane.reset();
}

GtkWidget* SplitView::getWidget() const noexcept {
    return m_paned;
}

PaneView* SplitView::getLeftPane() const noexcept {
    return m_leftPane.get();
}

PaneView* SplitView::getRightPane() const noexcept {
    return m_rightPane.get();
}

PaneView* SplitView::getActivePane() const noexcept {
    return m_activePane ? m_activePane : m_leftPane.get();
}

PaneView* SplitView::getInactivePane() const noexcept {
    if (m_activePane == m_leftPane.get()) {
        return m_rightPane.get();
    }
    return m_leftPane.get();
}

void SplitView::setActivePane(PaneView* pane) {
    if (!pane) {
        return;
    }
    if (m_activePane == pane && pane->isActive()) {
        pane->grabFocus();
        return;
    }

    m_activePane = pane;
    m_leftPane->setActive(m_leftPane.get() == m_activePane);
    m_rightPane->setActive(m_rightPane.get() == m_activePane);
    m_activePane->grabFocus();

    if (onActivePaneChanged) {
        onActivePaneChanged(m_activePane);
    }
}

void SplitView::cycleFocus() {
    if (!m_isSplit) {
        return;
    }
    if (m_activePane == m_leftPane.get()) {
        setActivePane(m_rightPane.get());
    } else {
        setActivePane(m_leftPane.get());
    }
}

void SplitView::toggleSplit() {
    m_isSplit = !m_isSplit;

    if (m_isSplit) {
        gtk_widget_set_visible(m_rightPane->getWidget(), TRUE);

        // Position divider at 50% width
        int width = gtk_widget_get_width(m_paned);
        if (width <= 1) {
            width = 800;
        }
        gtk_paned_set_position(GTK_PANED(m_paned), width / 2);

        // Navigate right pane to left pane's directory or $HOME
        if (!m_rightPane->getCurrentDirectory()) {
            GFile* leftDir = m_leftPane->getCurrentDirectory();
            if (leftDir) {
                m_rightPane->navigateTo(leftDir);
            } else {
                const char* home = g_get_home_dir();
                if (home) {
                    g_autoptr(GFile) homeDir = g_file_new_for_path(home);
                    m_rightPane->navigateTo(homeDir);
                }
            }
        }
    } else {
        gtk_widget_set_visible(m_rightPane->getWidget(), FALSE);
        setActivePane(m_leftPane.get());
    }
}

void SplitView::transferSelection(bool isMove) {
    // Phase 5 full integration hook
    (void)isMove;
}

void SplitView::setupUI() {
    m_paned = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_widget_set_hexpand(m_paned, TRUE);
    gtk_widget_set_vexpand(m_paned, TRUE);

    m_leftPane = std::make_unique<PaneView>();
    m_rightPane = std::make_unique<PaneView>();

    gtk_paned_set_start_child(GTK_PANED(m_paned), m_leftPane->getWidget());
    gtk_paned_set_resize_start_child(GTK_PANED(m_paned), TRUE);
    gtk_paned_set_shrink_start_child(GTK_PANED(m_paned), TRUE);

    gtk_paned_set_end_child(GTK_PANED(m_paned), m_rightPane->getWidget());
    gtk_paned_set_resize_end_child(GTK_PANED(m_paned), TRUE);
    gtk_paned_set_shrink_end_child(GTK_PANED(m_paned), TRUE);

    // Initially secondary pane is hidden
    m_isSplit = false;
    gtk_widget_set_visible(m_rightPane->getWidget(), FALSE);

    // Active pane starts as left pane
    m_activePane = m_leftPane.get();
    m_leftPane->setActive(true);
    m_rightPane->setActive(false);

    // Wire pane activation callbacks
    m_leftPane->onActivated = [this](PaneView* pane) {
        setActivePane(pane);
    };
    m_rightPane->onActivated = [this](PaneView* pane) {
        setActivePane(pane);
    };
}

} // namespace dfiles::views
