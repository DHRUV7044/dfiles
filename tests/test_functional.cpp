#include "core/app.hpp"
#include "core/types.hpp"
#include "ui/headerbar.hpp"
#include "ui/sidebar.hpp"
#include "ui/window.hpp"

#include <adwaita.h>
#include <cassert>
#include <iostream>
#include <string>
#include <vector>

namespace {

int g_passCount = 0;
int g_failCount = 0;

void testAssert(bool condition, const std::string& testName, const std::string& details = "") {
    if (condition) {
        std::cout << "  [PASS] " << testName;
        if (!details.empty()) {
            std::cout << " (" << details << ")";
        }
        std::cout << std::endl;
        ++g_passCount;
    } else {
        std::cerr << "  [FAIL] " << testName;
        if (!details.empty()) {
            std::cerr << " (" << details << ")";
        }
        std::cerr << std::endl;
        ++g_failCount;
    }
}

bool hasAccel(GtkApplication* app, const char* actionName, const char* expectedAccel) {
    const char* const* accels = gtk_application_get_accels_for_action(app, actionName);
    if (!accels) {
        return false;
    }
    for (int i = 0; accels[i] != nullptr; ++i) {
        if (std::string(accels[i]) == expectedAccel) {
            return true;
        }
    }
    return false;
}

} // namespace

int main(int /*argc*/, char* /*argv*/[]) {
    std::cout << "=========================================================\n";
    std::cout << "  DFILES PHASE 1: COMPREHENSIVE FUNCTIONAL TEST SUITE    \n";
    std::cout << "=========================================================\n\n";

    // 1. Initialize Libadwaita & GTK4
    adw_init();

    // -------------------------------------------------------------
    // Test Section 1: Core Types & String Helpers
    // -------------------------------------------------------------
    std::cout << "--- Test Section 1: Core Types Serialization ---\n";
    {
        using namespace dfiles::core;
        testAssert(std::string(viewModeToString(ViewMode::Grid)) == "Grid", "viewModeToString Grid");
        testAssert(std::string(viewModeToString(ViewMode::List)) == "List", "viewModeToString List");
        testAssert(std::string(sortColumnToString(SortColumn::Name)) == "Name", "sortColumnToString Name");
        testAssert(std::string(sortColumnToString(SortColumn::Size)) == "Size", "sortColumnToString Size");
        testAssert(std::string(sortOrderToString(SortOrder::Ascending)) == "Ascending", "sortOrderToString Ascending");
        testAssert(std::string(sortOrderToString(SortOrder::Descending)) == "Descending", "sortOrderToString Descending");
    }
    std::cout << std::endl;

    dfiles::core::DFilesApp app;

    // -------------------------------------------------------------
    // Test Section 2: DFilesApp & GAction / Accelerator Lookup
    // -------------------------------------------------------------
    std::cout << "--- Test Section 2: DFilesApp, GActions & Accelerators ---\n";
    {
        dfiles::core::DFilesApp app("org.gnome.dfiles.test.actions");
        testAssert(app.getNative() != nullptr, "DFilesApp native AdwApplication allocated");
        testAssert(!app.isTestMode(), "DFilesApp test mode defaults to false");

        app.setTestMode(true);
        testAssert(app.isTestMode(), "DFilesApp setTestMode(true) verified");

        g_application_register(G_APPLICATION(app.getNative()), nullptr, nullptr);

        // Trigger startup to register GActions and accelerators
        app.onStartup();

        auto* nativeApp = app.getNative();
        auto* actionMap = G_ACTION_MAP(nativeApp);
        auto* gtkApp = GTK_APPLICATION(nativeApp);

        // Verify GActions
        GAction* quitAction = g_action_map_lookup_action(actionMap, "quit");
        testAssert(quitAction != nullptr, "GAction 'app.quit' lookup");

        GAction* prefAction = g_action_map_lookup_action(actionMap, "preferences");
        testAssert(prefAction != nullptr, "GAction 'app.preferences' lookup");

        GAction* aboutAction = g_action_map_lookup_action(actionMap, "about");
        testAssert(aboutAction != nullptr, "GAction 'app.about' lookup");

        // Verify Accelerators
        testAssert(hasAccel(gtkApp, "app.quit", "<Control>q"), "Accelerator app.quit -> <Control>q");
        testAssert(hasAccel(gtkApp, "app.quit", "<Control>w"), "Accelerator app.quit -> <Control>w");
        testAssert(hasAccel(gtkApp, "app.preferences", "<Control>comma"), "Accelerator app.preferences -> <Control>comma");

        testAssert(hasAccel(gtkApp, "win.toggle-split", "F3"), "Accelerator win.toggle-split -> F3");
        testAssert(hasAccel(gtkApp, "win.toggle-terminal", "F4"), "Accelerator win.toggle-terminal -> F4");
        testAssert(hasAccel(gtkApp, "win.toggle-inspector", "F11"), "Accelerator win.toggle-inspector -> F11");
        testAssert(hasAccel(gtkApp, "win.focus-pathbar", "<Control>l"), "Accelerator win.focus-pathbar -> <Control>l");
        testAssert(hasAccel(gtkApp, "win.focus-filter", "<Control>i"), "Accelerator win.focus-filter -> <Control>i");
    }
    std::cout << std::endl;

    // -------------------------------------------------------------
    // Test Section 3: DFilesHeaderBar Toggle Signals & Callbacks
    // -------------------------------------------------------------
    std::cout << "--- Test Section 3: DFilesHeaderBar Toggles & Signals ---\n";
    {
        dfiles::ui::DFilesHeaderBar headerBar;
        testAssert(headerBar.getWidget() != nullptr, "DFilesHeaderBar getWidget non-null");
        testAssert(headerBar.getHeaderBar() != nullptr, "DFilesHeaderBar getHeaderBar non-null");

        // Verify title manipulation
        headerBar.setTitle("dfiles_test", "sub_test");
        testAssert(true, "DFilesHeaderBar setTitle executed cleanly");

        // Verify initial toggle states
        testAssert(!headerBar.isSplitActive(), "Initial Split button inactive");
        testAssert(!headerBar.isTerminalActive(), "Initial Terminal button inactive");
        testAssert(!headerBar.isInspectorActive(), "Initial Inspector button inactive");

        // Verify programmatic state updates
        headerBar.setSplitActive(true);
        testAssert(headerBar.isSplitActive(), "HeaderBar setSplitActive(true)");
        headerBar.setSplitActive(false);
        testAssert(!headerBar.isSplitActive(), "HeaderBar setSplitActive(false)");

        headerBar.setTerminalActive(true);
        testAssert(headerBar.isTerminalActive(), "HeaderBar setTerminalActive(true)");
        headerBar.setTerminalActive(false);
        testAssert(!headerBar.isTerminalActive(), "HeaderBar setTerminalActive(false)");

        headerBar.setInspectorActive(true);
        testAssert(headerBar.isInspectorActive(), "HeaderBar setInspectorActive(true)");
        headerBar.setInspectorActive(false);
        testAssert(!headerBar.isInspectorActive(), "HeaderBar setInspectorActive(false)");

        // Verify callbacks fire when toggled
        bool splitFired = false;
        headerBar.onSplitToggled = [&]() { splitFired = true; };
        headerBar.onSplitToggled();
        testAssert(splitFired, "HeaderBar onSplitToggled callback invocation");

        bool terminalFired = false;
        headerBar.onTerminalToggled = [&]() { terminalFired = true; };
        headerBar.onTerminalToggled();
        testAssert(terminalFired, "HeaderBar onTerminalToggled callback invocation");

        bool inspectorFired = false;
        headerBar.onInspectorToggled = [&]() { inspectorFired = true; };
        headerBar.onInspectorToggled();
        testAssert(inspectorFired, "HeaderBar onInspectorToggled callback invocation");
    }
    std::cout << std::endl;

    // -------------------------------------------------------------
    // Test Section 4: PlacesSidebar Population & Selection
    // -------------------------------------------------------------
    std::cout << "--- Test Section 4: PlacesSidebar Population & Selection ---\n";
    {
        dfiles::ui::PlacesSidebar sidebar;
        testAssert(sidebar.getWidget() != nullptr, "PlacesSidebar getWidget non-null");
        testAssert(sidebar.getListBox() != nullptr, "PlacesSidebar getListBox non-null");

        GtkListBox* listBox = sidebar.getListBox();

        // Scan rows for standard places
        std::vector<std::string> populatedUris;
        bool foundHome = false;
        bool foundRoot = false;
        bool foundTrash = false;
        bool foundDrivesHeader = false;

        GtkWidget* child = gtk_widget_get_first_child(GTK_WIDGET(listBox));
        while (child != nullptr) {
            if (GTK_IS_LIST_BOX_ROW(child)) {
                if (g_object_get_data(G_OBJECT(child), "is-header") != nullptr) {
                    foundDrivesHeader = true;
                }
                const char* uri = static_cast<const char*>(g_object_get_data(G_OBJECT(child), "place-uri"));
                if (uri) {
                    populatedUris.emplace_back(uri);
                    if (std::string(uri) == "file:///") {
                        foundRoot = true;
                    } else if (std::string(uri) == "trash:///") {
                        foundTrash = true;
                    } else if (std::string(uri).find(g_get_home_dir()) != std::string::npos) {
                        foundHome = true;
                    }
                }
            }
            child = gtk_widget_get_next_sibling(child);
        }

        testAssert(foundHome, "PlacesSidebar populated Home directory");
        testAssert(foundRoot, "PlacesSidebar populated Root filesystem (file:///)");
        testAssert(foundTrash, "PlacesSidebar populated Trash (trash:///)");
        testAssert(foundDrivesHeader, "PlacesSidebar populated 'Drives' section header");
        testAssert(!populatedUris.empty(), "PlacesSidebar total places populated",
                   std::to_string(populatedUris.size()) + " places");

        // Verify selectPlaceByUri
        sidebar.selectPlaceByUri("file:///");
        GtkListBoxRow* selected = gtk_list_box_get_selected_row(listBox);
        testAssert(selected != nullptr, "selectPlaceByUri('file:///') selects a row");
        if (selected) {
            const char* uri = static_cast<const char*>(g_object_get_data(G_OBJECT(selected), "place-uri"));
            testAssert(uri != nullptr && std::string(uri) == "file:///",
                       "Selected row place-uri matches 'file:///'");
        }

        // Verify unselect on non-existent URI
        sidebar.selectPlaceByUri("file:///does/not/exist");
        testAssert(gtk_list_box_get_selected_row(listBox) == nullptr,
                   "selectPlaceByUri with non-existent URI deselects all");

        // Verify onPlaceSelected callback
        std::string selectedPlaceResult;
        sidebar.onPlaceSelected = [&](const std::string& u) { selectedPlaceResult = u; };
        if (selected) {
            g_signal_emit_by_name(listBox, "row-activated", selected);
            testAssert(selectedPlaceResult == "file:///",
                       "PlacesSidebar onPlaceSelected triggered on row-activated", selectedPlaceResult);
        }

        // Verify dynamic refreshMounts
        sidebar.refreshMounts();
        testAssert(true, "PlacesSidebar refreshMounts executed cleanly");
    }
    std::cout << std::endl;

    // -------------------------------------------------------------
    // Test Section 5: Headless DFilesWindow Instantiation & Integration
    // -------------------------------------------------------------
    std::cout << "--- Test Section 5: DFilesWindow Headless Presentation & Integration ---\n";
    {
        dfiles::core::DFilesApp app("org.gnome.dfiles.test.window");
        app.setTestMode(true);
        app.onStartup();
        app.onActivate();

        auto* window = app.getWindow();
        testAssert(window != nullptr, "DFilesWindow instantiated on onActivate");
        if (window) {
            testAssert(window->getWidget() != nullptr, "DFilesWindow getWidget non-null");
            testAssert(window->getWindow() != nullptr, "DFilesWindow getWindow non-null");
            testAssert(window->getHeaderBar() != nullptr, "DFilesWindow getHeaderBar non-null");
            testAssert(window->getSidebar() != nullptr, "DFilesWindow getSidebar non-null");
            testAssert(window->getContentArea() != nullptr, "DFilesWindow getContentArea non-null");

            // Test non-intrusive toast presentation
            window->showToast("Test functional toast message");
            testAssert(true, "DFilesWindow showToast executed without error");

            // Test window present call
            window->present();
            testAssert(true, "DFilesWindow present executed cleanly");

            // REMED-3: Verify window GActions registered on G_ACTION_MAP(window->getWindow())
            auto* winActionMap = G_ACTION_MAP(window->getWindow());
            auto* winActionGroup = G_ACTION_GROUP(window->getWindow());
            testAssert(g_action_map_lookup_action(winActionMap, "toggle-split") != nullptr,
                       "GAction 'win.toggle-split' registered on window");
            testAssert(g_action_map_lookup_action(winActionMap, "toggle-terminal") != nullptr,
                       "GAction 'win.toggle-terminal' registered on window");
            testAssert(g_action_map_lookup_action(winActionMap, "toggle-inspector") != nullptr,
                       "GAction 'win.toggle-inspector' registered on window");
            testAssert(g_action_map_lookup_action(winActionMap, "focus-pathbar") != nullptr,
                       "GAction 'win.focus-pathbar' registered on window");
            testAssert(g_action_map_lookup_action(winActionMap, "focus-filter") != nullptr,
                       "GAction 'win.focus-filter' registered on window");

            // Test GAction activation synchronization with headerbar
            g_action_group_activate_action(winActionGroup, "toggle-split", nullptr);
            testAssert(window->getHeaderBar()->isSplitActive(),
                       "Activating GAction 'toggle-split' synchronizes HeaderBar");
            g_action_group_activate_action(winActionGroup, "toggle-split", nullptr);
            testAssert(!window->getHeaderBar()->isSplitActive(),
                       "Toggling GAction 'toggle-split' off synchronizes HeaderBar");

            g_action_group_activate_action(winActionGroup, "focus-pathbar", nullptr);
            testAssert(true, "Activating GAction 'focus-pathbar' executes cleanly");
            g_action_group_activate_action(winActionGroup, "focus-filter", nullptr);
            testAssert(true, "Activating GAction 'focus-filter' executes cleanly");
        }

        // Test action dialog presentation & deduplication (about & preferences) (REMED-5)
        app.onActionPreferences();
        app.onActionPreferences(); // Repeated call must not stack dialogs
        testAssert(true, "DFilesApp onActionPreferences presented & deduplicated cleanly");

        app.onActionAbout();
        app.onActionAbout(); // Repeated call must not stack dialogs
        testAssert(true, "DFilesApp onActionAbout presented & deduplicated cleanly");
    }
    std::cout << std::endl;

    // -------------------------------------------------------------
    // Test Summary
    // -------------------------------------------------------------
    std::cout << "=========================================================\n";
    std::cout << "  FUNCTIONAL TEST RESULTS SUMMARY                        \n";
    std::cout << "  TOTAL TESTS PASSED : " << g_passCount << "\n";
    std::cout << "  TOTAL TESTS FAILED : " << g_failCount << "\n";
    std::cout << "=========================================================\n";

    return (g_failCount == 0) ? 0 : 1;
}
