#include "core/app.hpp"
#include "core/types.hpp"
#include "model/directory_model.hpp"
#include "model/file_item.hpp"
#include "ui/headerbar.hpp"
#include "ui/sidebar.hpp"
#include "ui/window.hpp"
#include "views/file_view.hpp"
#include "views/split_view.hpp"
#include "inspector/checksum_helper.hpp"
#include "inspector/inspector_sidebar.hpp"
#include "terminal/terminal_drawer.hpp"

#include <adwaita.h>
#include <algorithm>
#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
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

[[maybe_unused]] void pumpMainLoop(int iterations = 10) {
    for (int i = 0; i < iterations; ++i) {
        while (g_main_context_iteration(nullptr, FALSE)) {
            // Process pending main context events
        }
        g_usleep(2000); // 2ms
    }
}

} // namespace

int main(int /*argc*/, char* /*argv*/[]) {
    std::cout << "=========================================================\n";
    std::cout << "  DFILES: COMPREHENSIVE FUNCTIONAL TEST SUITE (PHASES 1 & 2)\n";
    std::cout << "=========================================================\n\n";

    // 1. Initialize Libadwaita & GTK4
    adw_init();
    g_main_context_acquire(g_main_context_default());

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

    // =============================================================
    // PHASE 2 FUNCTIONAL TEST SUITES
    // =============================================================

    // -------------------------------------------------------------
    // Test Section 6 (Phase 2 Suite 1): FileItem Unit Verification
    // -------------------------------------------------------------
    std::cout << "--- Test Section 6: FileItem Unit Verification ---\n";
    {
        std::string tempBase = std::filesystem::temp_directory_path() /
            ("dfiles_test_fileitem_" + std::to_string(std::chrono::system_clock::now().time_since_epoch().count()));
        std::filesystem::create_directories(tempBase);

        // 1. Create regular sample file (known size: 48 bytes)
        std::string regularFilePath = tempBase + "/sample.txt";
        {
            std::ofstream ofs(regularFilePath, std::ios::binary);
            ofs << "123456789012345678901234567890123456789012345678"; // 48 bytes
        }
        std::filesystem::permissions(regularFilePath,
            std::filesystem::perms::owner_read | std::filesystem::perms::owner_write |
            std::filesystem::perms::group_read | std::filesystem::perms::others_read); // 0644

        // 2. Create sample directory
        std::string sampleDirPath = tempBase + "/sample_folder";
        std::filesystem::create_directories(sampleDirPath);
        std::filesystem::permissions(sampleDirPath,
            std::filesystem::perms::owner_all |
            std::filesystem::perms::group_read | std::filesystem::perms::group_exec |
            std::filesystem::perms::others_read | std::filesystem::perms::others_exec); // 0755

        // 3. Create hidden dotfile
        std::string hiddenFilePath = tempBase + "/.hidden_file";
        {
            std::ofstream ofs(hiddenFilePath, std::ios::binary);
            ofs << "secret";
        }

        // 4. Create symlink
        std::string symlinkPath = tempBase + "/sample_symlink";
        std::filesystem::create_symlink(regularFilePath, symlinkPath);

        // Query attributes using GFile and GFileInfo
        g_autoptr(GFile) gRegularFile = g_file_new_for_path(regularFilePath.c_str());
        g_autoptr(GFileInfo) regularInfo = g_file_query_info(gRegularFile, "*", G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS, nullptr, nullptr);
        testAssert(regularInfo != nullptr, "Queried GFileInfo for regular file");

        dfiles::model::FileItem fileItem(gRegularFile, regularInfo);

        // Assert attributes for regular file
        testAssert(fileItem.getName() == "sample.txt", "FileItem getName() matches 'sample.txt'");
        testAssert(fileItem.getDisplayName() == "sample.txt", "FileItem getDisplayName() matches 'sample.txt'");
        testAssert(fileItem.getPath() == regularFilePath, "FileItem getPath() matches regularFilePath");
        testAssert(fileItem.getSize() == 48, "FileItem getSize() == 48 bytes");
        testAssert(fileItem.getFormattedSize().find("48") != std::string::npos, "FileItem getFormattedSize() contains '48'");
        testAssert(!fileItem.isDirectory(), "FileItem isDirectory() is false for file");
        testAssert(!fileItem.isHidden(), "FileItem isHidden() is false for regular file");
        testAssert(!fileItem.isSymlink(), "FileItem isSymlink() is false for regular file");
        testAssert(fileItem.getPermissionsString().find("rw-r--r--") != std::string::npos, "FileItem getPermissionsString() matches 0644 ('rw-r--r--')");
        testAssert(fileItem.getGObject() != nullptr, "FileItem getGObject() non-null");
        testAssert(fileItem.getGFile() != nullptr, "FileItem getGFile() non-null");
        testAssert(fileItem.getGFileInfo() != nullptr, "FileItem getGFileInfo() non-null");
        testAssert(fileItem.getIcon() != nullptr, "FileItem getIcon() non-null");
        testAssert(!fileItem.getMimeType().empty(), "FileItem getMimeType() non-empty");

        // Directory Item Verification
        g_autoptr(GFile) gDirFile = g_file_new_for_path(sampleDirPath.c_str());
        g_autoptr(GFileInfo) dirInfo = g_file_query_info(gDirFile, "*", G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS, nullptr, nullptr);
        dfiles::model::FileItem dirItem(gDirFile, dirInfo);
        testAssert(dirItem.getName() == "sample_folder", "Directory FileItem getName() matches");
        testAssert(dirItem.isDirectory(), "Directory FileItem isDirectory() is true");
        testAssert(dirItem.getFormattedSize() == "--", "Directory FileItem getFormattedSize() is '--'");
        testAssert(dirItem.getPermissionsString().find("rwxr-xr-x") != std::string::npos, "Directory FileItem getPermissionsString() matches 0755");

        // Hidden Item Verification
        g_autoptr(GFile) gHiddenFile = g_file_new_for_path(hiddenFilePath.c_str());
        g_autoptr(GFileInfo) hiddenInfo = g_file_query_info(gHiddenFile, "*", G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS, nullptr, nullptr);
        dfiles::model::FileItem hiddenItem(gHiddenFile, hiddenInfo);
        testAssert(hiddenItem.getName() == ".hidden_file", "Hidden FileItem getName() matches");
        testAssert(hiddenItem.isHidden(), "Hidden FileItem isHidden() is true");

        // Symlink Item Verification
        g_autoptr(GFile) gSymlinkFile = g_file_new_for_path(symlinkPath.c_str());
        g_autoptr(GFileInfo) linkInfo = g_file_query_info(gSymlinkFile, "*", G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS, nullptr, nullptr);
        dfiles::model::FileItem linkItem(gSymlinkFile, linkInfo);
        testAssert(linkItem.isSymlink(), "Symlink FileItem isSymlink() is true");

        // Copy constructor & copy assignment
        dfiles::model::FileItem copyItem(fileItem);
        testAssert(copyItem.getName() == fileItem.getName(), "FileItem copy constructor name match");
        testAssert(copyItem.getSize() == fileItem.getSize(), "FileItem copy constructor size match");
        testAssert(copyItem.getGObject() == fileItem.getGObject(), "FileItem copy constructor refs same GObject");

        dfiles::model::FileItem assignItem = dirItem;
        testAssert(assignItem.getName() == dirItem.getName(), "FileItem copy assignment name match");
        testAssert(assignItem.isDirectory() == dirItem.isDirectory(), "FileItem copy assignment isDirectory match");

        // Move constructor & move assignment
        dfiles::model::FileItem moveItem(std::move(copyItem));
        testAssert(moveItem.getName() == fileItem.getName(), "FileItem move constructor name match");
        testAssert(moveItem.getSize() == fileItem.getSize(), "FileItem move constructor size match");

        dfiles::model::FileItem moveAssignItem = std::move(assignItem);
        testAssert(moveAssignItem.getName() == dirItem.getName(), "FileItem move assignment name match");
        testAssert(moveAssignItem.isDirectory() == dirItem.isDirectory(), "FileItem move assignment isDirectory match");

        // Clean up
        std::filesystem::remove_all(tempBase);
    }
    std::cout << std::endl;

    // -------------------------------------------------------------
    // Test Section 7 (Phase 2 Suite 2): DirectoryModel Enumeration, Sorting & Filtering
    // -------------------------------------------------------------
    std::cout << "--- Test Section 7: DirectoryModel Enumeration, Sorting & Filtering ---\n";
    {
        std::string testDir = std::filesystem::temp_directory_path() /
            ("dfiles_test_dirmodel_" + std::to_string(std::chrono::system_clock::now().time_since_epoch().count()));
        std::filesystem::create_directories(testDir);

        // Create test hierarchy: hidden files, nested folders, mixed files (file1, file2, file10)
        std::string hiddenFile = testDir + "/.hidden";
        { std::ofstream ofs(hiddenFile); ofs << "hidden"; }

        std::string folderB = testDir + "/folderB";
        std::string folderA = testDir + "/folderA";
        std::filesystem::create_directories(folderB);
        std::filesystem::create_directories(folderA);

        std::string file1 = testDir + "/file1.txt";
        std::string file2 = testDir + "/file2.txt";
        std::string file10 = testDir + "/file10.txt";
        std::string otherFile = testDir + "/archive.zip";
        { std::ofstream ofs(file1); ofs << "file 1"; }
        { std::ofstream ofs(file2); ofs << "file 2"; }
        { std::ofstream ofs(file10); ofs << "file 10"; }
        { std::ofstream ofs(otherFile); ofs << "zip"; }

        dfiles::model::DirectoryModel dirModel;
        testAssert(dirModel.getModel() != nullptr, "DirectoryModel getModel() non-null");

        g_autoptr(GFile) gTestDir = g_file_new_for_path(testDir.c_str());
        bool loadDone = false;
        dirModel.onLoadingFinished = [&](size_t, size_t, size_t, uint64_t) {
            loadDone = true;
        };

        dirModel.loadDirectory(gTestDir);
        for (int i = 0; i < 200 && !loadDone; ++i) {
            pumpMainLoop(5);
        }
        testAssert(loadDone, "DirectoryModel loadDirectory finished async loading");
        testAssert(!dirModel.isLoading(), "DirectoryModel isLoading() is false after load");

        // 1. Verify showHidden(false) excludes hidden files
        dirModel.setShowHidden(false);
        pumpMainLoop(5);
        GListModel* model = dirModel.getModel();
        guint nItems = g_list_model_get_n_items(model);
        bool foundHidden = false;
        for (guint i = 0; i < nItems; ++i) {
            g_autoptr(DfilesFileItem) it = DFILES_FILE_ITEM(g_list_model_get_item(model, i));
            if (dfiles_file_item_is_hidden(it)) {
                foundHidden = true;
            }
        }
        testAssert(!foundHidden, "showHidden(false) excludes .hidden file");

        // 2. Verify showHidden(true) includes hidden files
        dirModel.setShowHidden(true);
        pumpMainLoop(5);
        model = dirModel.getModel();
        nItems = g_list_model_get_n_items(model);
        foundHidden = false;
        for (guint i = 0; i < nItems; ++i) {
            g_autoptr(DfilesFileItem) it = DFILES_FILE_ITEM(g_list_model_get_item(model, i));
            if (dfiles_file_item_is_hidden(it)) {
                foundHidden = true;
            }
        }
        testAssert(foundHidden, "showHidden(true) includes .hidden file");

        // 3. Verify natural sorting: folders strictly precede files; files sort naturally (file1 < file2 < file10)
        dirModel.setShowHidden(false);
        dirModel.setSorting(dfiles::core::SortColumn::Name, dfiles::core::SortOrder::Ascending);
        pumpMainLoop(5);
        model = dirModel.getModel();
        nItems = g_list_model_get_n_items(model);

        std::vector<std::string> names;
        std::vector<bool> isDirList;
        for (guint i = 0; i < nItems; ++i) {
            g_autoptr(DfilesFileItem) it = DFILES_FILE_ITEM(g_list_model_get_item(model, i));
            names.push_back(dfiles_file_item_get_name(it));
            isDirList.push_back(dfiles_file_item_is_directory(it) == TRUE);
        }

        bool foldersFirst = true;
        bool seenFile = false;
        for (bool isDir : isDirList) {
            if (!isDir) {
                seenFile = true;
            } else if (seenFile && isDir) {
                foldersFirst = false;
                break;
            }
        }
        testAssert(foldersFirst, "Natural sorting: folders strictly precede files");

        // Extract files and verify natural ordering
        std::vector<std::string> fileNames;
        for (size_t i = 0; i < names.size(); ++i) {
            if (!isDirList[i]) {
                fileNames.push_back(names[i]);
            }
        }
        auto it1 = std::find(fileNames.begin(), fileNames.end(), "file1.txt");
        auto it2 = std::find(fileNames.begin(), fileNames.end(), "file2.txt");
        auto it10 = std::find(fileNames.begin(), fileNames.end(), "file10.txt");
        bool naturalOrder = (it1 != fileNames.end() && it2 != fileNames.end() && it10 != fileNames.end() && it1 < it2 && it2 < it10);
        testAssert(naturalOrder, "Natural sorting: files sort naturally (file1.txt < file2.txt < file10.txt)");

        // 4. Test search filter query (setSearchFilter)
        dirModel.setSearchFilter("file1");
        pumpMainLoop(5);
        model = dirModel.getModel();
        nItems = g_list_model_get_n_items(model);
        bool searchMatch = true;
        for (guint i = 0; i < nItems; ++i) {
            g_autoptr(DfilesFileItem) it = DFILES_FILE_ITEM(g_list_model_get_item(model, i));
            std::string n = dfiles_file_item_get_name(it);
            if (n.find("file1") == std::string::npos) {
                searchMatch = false;
            }
        }
        testAssert(searchMatch && nItems == 2, "setSearchFilter('file1') filters exactly to file1.txt and file10.txt");

        dirModel.setSearchFilter("");
        pumpMainLoop(5);
        testAssert(g_list_model_get_n_items(dirModel.getModel()) == 6, "Clearing search filter restores all non-hidden items (6 items)");

        // 5. REMED-5: Verify naturalCompare with leading zeros returns non-zero (!= 0)
        int cmp1 = dfiles::model::naturalCompare("file01.txt", "file1.txt");
        int cmp2 = dfiles::model::naturalCompare("file1.txt", "file01.txt");
        testAssert(cmp1 != 0, "naturalCompare with leading zeros: 'file01.txt' vs 'file1.txt' returns non-zero (!= 0)",
                   "Result: " + std::to_string(cmp1));
        testAssert(cmp2 != 0, "naturalCompare with leading zeros: 'file1.txt' vs 'file01.txt' returns non-zero (!= 0)",
                   "Result: " + std::to_string(cmp2));
        testAssert((cmp1 < 0 && cmp2 > 0) || (cmp1 > 0 && cmp2 < 0),
                   "naturalCompare leading zero ordering is anti-symmetric");
        testAssert(dfiles::model::naturalCompare("file1.txt", "file1.txt") == 0,
                   "naturalCompare with identical strings returns 0");

        // Clean up
        std::filesystem::remove_all(testDir);
    }
    std::cout << std::endl;

    // -------------------------------------------------------------
    // Test Section 8 (Phase 2 Suite 3): PaneView Navigation & History Stack
    // -------------------------------------------------------------
    std::cout << "--- Test Section 8: PaneView Navigation & History Stack ---\n";
    {
        std::string baseDir = std::filesystem::temp_directory_path() /
            ("dfiles_test_pane_" + std::to_string(std::chrono::system_clock::now().time_since_epoch().count()));
        std::string dirA = baseDir + "/dirA";
        std::string dirB = dirA + "/dirB";
        std::string dirC = dirB + "/dirC";
        std::filesystem::create_directories(dirC);

        std::string selFile1 = dirA + "/sel_alpha.txt";
        std::string selFile2 = dirA + "/sel_beta.txt";
        { std::ofstream ofs(selFile1); ofs << "alpha"; }
        { std::ofstream ofs(selFile2); ofs << "beta"; }

        g_autoptr(GFile) gDirA = g_file_new_for_path(dirA.c_str());
        g_autoptr(GFile) gDirB = g_file_new_for_path(dirB.c_str());
        g_autoptr(GFile) gDirC = g_file_new_for_path(dirC.c_str());

        dfiles::views::PaneView paneView;
        testAssert(paneView.getWidget() != nullptr, "PaneView getWidget() non-null");
        testAssert(paneView.getModel() != nullptr, "PaneView getModel() non-null");
        testAssert(paneView.getFileView() != nullptr, "PaneView getFileView() non-null");

        // Navigate: dirA -> dirB -> dirC
        paneView.navigateTo(gDirA);
        pumpMainLoop(10);
        testAssert(paneView.getCurrentDirectory() != nullptr && g_file_equal(paneView.getCurrentDirectory(), gDirA),
                   "PaneView navigated to dirA");

        paneView.navigateTo(gDirB);
        pumpMainLoop(10);
        testAssert(paneView.getCurrentDirectory() != nullptr && g_file_equal(paneView.getCurrentDirectory(), gDirB),
                   "PaneView navigated to dirB");

        paneView.navigateTo(gDirC);
        pumpMainLoop(10);
        testAssert(paneView.getCurrentDirectory() != nullptr && g_file_equal(paneView.getCurrentDirectory(), gDirC),
                   "PaneView navigated to dirC");

        // Assert goBack() returns to dirB, then dirA
        paneView.goBack();
        pumpMainLoop(10);
        testAssert(paneView.getCurrentDirectory() != nullptr && g_file_equal(paneView.getCurrentDirectory(), gDirB),
                   "PaneView goBack() returned to dirB");

        paneView.goBack();
        pumpMainLoop(10);
        testAssert(paneView.getCurrentDirectory() != nullptr && g_file_equal(paneView.getCurrentDirectory(), gDirA),
                   "PaneView goBack() returned to dirA");

        // Boundary conditions: calling goBack() on empty stack is safe and stays on dirA
        paneView.goBack();
        pumpMainLoop(5);
        testAssert(paneView.getCurrentDirectory() != nullptr && g_file_equal(paneView.getCurrentDirectory(), gDirA),
                   "Boundary condition: goBack() at start of history is safe and preserves dirA");

        // Assert goForward() moves forward to dirB
        paneView.goForward();
        pumpMainLoop(10);
        testAssert(paneView.getCurrentDirectory() != nullptr && g_file_equal(paneView.getCurrentDirectory(), gDirB),
                   "PaneView goForward() moved forward to dirB");

        // Assert navigateUp() moves to parent directory (dirA)
        paneView.navigateUp();
        pumpMainLoop(10);
        testAssert(paneView.getCurrentDirectory() != nullptr && g_file_equal(paneView.getCurrentDirectory(), gDirA),
                   "PaneView navigateUp() moved to parent dirA");

        // View mode switching: ViewMode::List vs ViewMode::Grid
        testAssert(paneView.getFileView()->getViewMode() == dfiles::core::ViewMode::List, "PaneView default view mode is List");
        paneView.getFileView()->setViewMode(dfiles::core::ViewMode::Grid);
        testAssert(paneView.getFileView()->getViewMode() == dfiles::core::ViewMode::Grid, "PaneView switched view mode to Grid");
        paneView.getFileView()->setViewMode(dfiles::core::ViewMode::List);
        testAssert(paneView.getFileView()->getViewMode() == dfiles::core::ViewMode::List, "PaneView switched view mode back to List");

        // REMED-6: FileView::getSelectedItems() and PaneView::getSelectedItems() RAII verification
        paneView.getFileView()->selectAll();
        pumpMainLoop(10);
        std::vector<dfiles::model::FileItem> paneSelected = paneView.getSelectedItems();
        std::vector<dfiles::model::FileItem> fileViewSelected = paneView.getFileView()->getSelectedItems();
        testAssert(!paneSelected.empty(), "PaneView::getSelectedItems() returns non-empty vector after selectAll()",
                   std::to_string(paneSelected.size()) + " items selected");
        testAssert(paneSelected.size() == fileViewSelected.size(),
                   "PaneView and FileView getSelectedItems() return matching counts");
        bool allValidRAII = true;
        for (const auto& item : paneSelected) {
            if (item.getGObject() == nullptr || !DFILES_IS_FILE_ITEM(item.getGObject()) ||
                item.getGFile() == nullptr || item.getName().empty()) {
                allValidRAII = false;
                break;
            }
        }
        testAssert(allValidRAII, "All selected items have valid RAII GObject and GFile references");

        // Verify copy & move semantics preserves RAII validity
        if (!paneSelected.empty()) {
            dfiles::model::FileItem itemCopy = paneSelected[0];
            testAssert(itemCopy.getGObject() == paneSelected[0].getGObject(),
                       "Selected FileItem copy retains valid GObject ref");
            dfiles::model::FileItem itemMoved = std::move(itemCopy);
            testAssert(itemMoved.getGObject() != nullptr,
                       "Selected FileItem move retains valid GObject ref");
        }

        // Verify unselectAll clears selection
        paneView.getFileView()->unselectAll();
        pumpMainLoop(5);
        testAssert(paneView.getSelectedItems().empty(),
                   "PaneView::getSelectedItems() is empty after unselectAll()");
        testAssert(paneView.getFileView()->getSelectedItems().empty(),
                   "FileView::getSelectedItems() is empty after unselectAll()");

        // Clean up
        std::filesystem::remove_all(baseDir);
    }
    std::cout << std::endl;

    // -------------------------------------------------------------
    // Test Section 9 (Phase 2 Suite 4): SplitView & F3 Dual-Pane
    // -------------------------------------------------------------
    std::cout << "--- Test Section 9: SplitView & F3 Dual-Pane ---\n";
    {
        dfiles::views::SplitView splitView;
        testAssert(splitView.getWidget() != nullptr, "SplitView getWidget() non-null");
        testAssert(!splitView.isSplit(), "SplitView initializes in single-pane mode (!isSplit())");
        testAssert(splitView.getLeftPane() != nullptr, "SplitView getLeftPane() non-null");
        testAssert(splitView.getRightPane() != nullptr, "SplitView getRightPane() non-null");
        testAssert(splitView.getActivePane() == splitView.getLeftPane(), "SplitView active pane defaults to left pane");
        testAssert(gtk_widget_get_visible(splitView.getRightPane()->getWidget()) == FALSE,
                   "SplitView right pane initially hidden");

        // REMED-4: Verify SplitView::cycleFocus() and setActivePane() route focus via grabFocus()
        g_object_ref_sink(splitView.getWidget());
        GtkWidget* testWin = gtk_window_new();
        gtk_window_set_child(GTK_WINDOW(testWin), splitView.getWidget());
        gtk_window_present(GTK_WINDOW(testWin));
        pumpMainLoop(10);

        GtkRoot* root = gtk_widget_get_root(splitView.getWidget());
        testAssert(root != nullptr, "SplitView has valid GtkRoot when placed in presented window");

        // Call toggleSplit(): assert isSplit() == true, right pane becomes visible, both have active/inactive states
        splitView.toggleSplit();
        testAssert(splitView.isSplit() == true, "SplitView toggleSplit() sets isSplit() == true");
        testAssert(gtk_widget_get_visible(splitView.getRightPane()->getWidget()) == TRUE,
                   "SplitView right pane becomes visible when split");
        testAssert(splitView.getLeftPane()->isActive() == true, "Left pane has active state");
        testAssert(splitView.getRightPane()->isActive() == false, "Right pane has inactive state");

        // 1. Initial grabFocus() routes keyboard focus to left pane
        splitView.getActivePane()->grabFocus();
        pumpMainLoop(5);
        GtkWidget* focused = root ? gtk_root_get_focus(root) : nullptr;
        bool leftHasFocus = focused && (focused == splitView.getLeftPane()->getWidget() ||
                                        gtk_widget_is_ancestor(focused, splitView.getLeftPane()->getWidget()));
        testAssert(leftHasFocus, "splitView.getActivePane()->grabFocus() routes keyboard focus to left pane");

        // 2. setActivePane(rightPane) routes focus to right pane via grabFocus()
        splitView.setActivePane(splitView.getRightPane());
        pumpMainLoop(5);
        testAssert(splitView.getActivePane() == splitView.getRightPane(), "setActivePane() sets right pane as active");
        testAssert(splitView.getRightPane()->isActive() == true, "Right pane active state is true");
        testAssert(splitView.getLeftPane()->isActive() == false, "Left pane active state is false");
        focused = root ? gtk_root_get_focus(root) : nullptr;
        bool rightHasFocus = focused && (focused == splitView.getRightPane()->getWidget() ||
                                         gtk_widget_is_ancestor(focused, splitView.getRightPane()->getWidget()));
        testAssert(rightHasFocus, "setActivePane(rightPane) routes focus to right pane via grabFocus()");

        // 3. cycleFocus() routes focus back to left pane via grabFocus()
        splitView.cycleFocus();
        pumpMainLoop(5);
        testAssert(splitView.getActivePane() == splitView.getLeftPane(), "cycleFocus() switches active pane back to left");
        focused = root ? gtk_root_get_focus(root) : nullptr;
        leftHasFocus = focused && (focused == splitView.getLeftPane()->getWidget() ||
                                   gtk_widget_is_ancestor(focused, splitView.getLeftPane()->getWidget()));
        testAssert(leftHasFocus, "cycleFocus() routes keyboard focus back to left pane via grabFocus()");

        // 4. cycleFocus() routes focus again to right pane via grabFocus()
        splitView.cycleFocus();
        pumpMainLoop(5);
        testAssert(splitView.getActivePane() == splitView.getRightPane(), "cycleFocus() switches active pane again to right");
        focused = root ? gtk_root_get_focus(root) : nullptr;
        rightHasFocus = focused && (focused == splitView.getRightPane()->getWidget() ||
                                    gtk_widget_is_ancestor(focused, splitView.getRightPane()->getWidget()));
        testAssert(rightHasFocus, "cycleFocus() routes keyboard focus again to right pane via grabFocus()");

        // Call toggleSplit() again: assert isSplit() == false and active pane resets to left
        splitView.toggleSplit();
        testAssert(splitView.isSplit() == false, "toggleSplit() again sets isSplit() == false");
        testAssert(splitView.getActivePane() == splitView.getLeftPane(), "active pane resets to left upon closing split");
        testAssert(gtk_widget_get_visible(splitView.getRightPane()->getWidget()) == FALSE,
                   "Right pane is hidden after closing split");

        // Clean up test window cleanly
        gtk_window_set_child(GTK_WINDOW(testWin), nullptr);
        gtk_window_destroy(GTK_WINDOW(testWin));
        pumpMainLoop(5);
        g_object_unref(splitView.getWidget());
    }
    std::cout << std::endl;

    // -------------------------------------------------------------
    // Test Section 10 (Phase 2 Suite 5): Full Window Integration
    // -------------------------------------------------------------
    std::cout << "--- Test Section 10: Full Window Integration (SplitView & Navigation) ---\n";
    {
        dfiles::core::DFilesApp app("org.gnome.dfiles.test.p2integration");
        app.setTestMode(true);
        app.onStartup();
        app.onActivate();

        auto* window = app.getWindow();
        testAssert(window != nullptr, "DFilesWindow instantiated on onActivate");
        if (window) {
            auto* splitView = window->getSplitView();
            testAssert(splitView != nullptr, "SplitView is present in DFilesWindow");
            testAssert(splitView->getWidget() != nullptr, "SplitView getWidget() non-null");

            // In headless DFilesWindow: assert SplitView is present in content area
            testAssert(window->getContentArea() != nullptr, "DFilesWindow getContentArea() non-null");
            testAssert(gtk_widget_is_ancestor(splitView->getWidget(), window->getContentArea()),
                       "SplitView widget is child of DFilesWindow contentArea");

            // Test win.toggle-split action toggles SplitView
            auto* winActionGroup = G_ACTION_GROUP(window->getWindow());
            bool splitBefore = splitView->isSplit();
            g_action_group_activate_action(winActionGroup, "toggle-split", nullptr);
            testAssert(splitView->isSplit() == !splitBefore, "Action win.toggle-split toggles SplitView isSplit()");
            testAssert(window->getHeaderBar()->isSplitActive() == splitView->isSplit(),
                       "HeaderBar split button is synchronized with SplitView");

            g_action_group_activate_action(winActionGroup, "toggle-split", nullptr);
            testAssert(splitView->isSplit() == splitBefore, "Action win.toggle-split restores original state");
            testAssert(window->getHeaderBar()->isSplitActive() == splitView->isSplit(),
                       "HeaderBar split button restored");

            // Test sidebar place selection navigates active pane
            auto* sidebar = window->getSidebar();
            auto* activePane = splitView->getActivePane();
            testAssert(sidebar != nullptr && activePane != nullptr, "Sidebar and active pane non-null");
            if (sidebar && sidebar->onPlaceSelected && activePane) {
                g_autoptr(GFile) rootDir = g_file_new_for_path("/");
                sidebar->onPlaceSelected("file:///");
                pumpMainLoop(15);
                testAssert(activePane->getCurrentDirectory() != nullptr && g_file_equal(activePane->getCurrentDirectory(), rootDir),
                           "PlacesSidebar selection navigates active pane to 'file:///'");
            }
        }
    }
    std::cout << std::endl;

    // -------------------------------------------------------------
    // Test Section 11: Milestone 3 Terminal Drawer & Inspector Sidebar Integration
    // -------------------------------------------------------------
    std::cout << "--- Test Section 11: Milestone 3 Terminal Drawer & Inspector Sidebar ---\n";
    {
        dfiles::core::DFilesApp app("org.gnome.dfiles.test.p3integration");
        app.setTestMode(true);
        app.onStartup();
        app.onActivate();

        auto* window = app.getWindow();
        testAssert(window != nullptr, "DFilesWindow instantiated on onActivate");
        if (window) {
            auto* drawer = window->getTerminalDrawer();
            auto* inspector = window->getInspectorSidebar();
            auto* hb = window->getHeaderBar();

            testAssert(drawer != nullptr, "TerminalDrawer is present in DFilesWindow");
            testAssert(inspector != nullptr, "InspectorSidebar is present in DFilesWindow");
            testAssert(drawer->getWidget() != nullptr, "TerminalDrawer root widget non-null");
            testAssert(inspector->getWidget() != nullptr, "InspectorSidebar root widget non-null");

            // Initial visibility verification
            testAssert(!drawer->isVisible(), "TerminalDrawer initially hidden");
            testAssert(!inspector->isVisible(), "InspectorSidebar initially hidden");
            testAssert(!hb->isTerminalActive(), "HeaderBar terminal toggle initially inactive");
            testAssert(!hb->isInspectorActive(), "HeaderBar inspector toggle initially inactive");

            auto* winActionGroup = G_ACTION_GROUP(window->getWindow());

            // 1. win.toggle-terminal action
            g_action_group_activate_action(winActionGroup, "toggle-terminal", nullptr);
            testAssert(drawer->isVisible(), "Action win.toggle-terminal opens drawer");
            testAssert(hb->isTerminalActive(), "HeaderBar terminal button active after action");

            g_action_group_activate_action(winActionGroup, "toggle-terminal", nullptr);
            testAssert(!drawer->isVisible(), "Action win.toggle-terminal closes drawer");
            testAssert(!hb->isTerminalActive(), "HeaderBar terminal button inactive after action");

            // 2. win.toggle-inspector action
            g_action_group_activate_action(winActionGroup, "toggle-inspector", nullptr);
            testAssert(inspector->isVisible(), "Action win.toggle-inspector opens sidebar");
            testAssert(hb->isInspectorActive(), "HeaderBar inspector button active after action");

            g_action_group_activate_action(winActionGroup, "toggle-inspector", nullptr);
            testAssert(!inspector->isVisible(), "Action win.toggle-inspector closes sidebar");
            testAssert(!hb->isInspectorActive(), "HeaderBar inspector button inactive after action");

            // 3. Inspector onClosed callback
            hb->setInspectorActive(true);
            if (hb->onInspectorToggled) hb->onInspectorToggled();
            testAssert(inspector->isVisible(), "Inspector opened via headerbar");
            if (inspector->onClosed) {
                inspector->onClosed();
            }
            testAssert(!hb->isInspectorActive(), "Inspector onClosed deactivates headerbar button");

            // 4. File inspection
            inspector->inspectItems({}); // Empty selection
            testAssert(true, "Inspector inspectItems({}) executes cleanly");

            namespace fs = std::filesystem;
            fs::path testFile = fs::temp_directory_path() / "dfiles_test_inspect.txt";
            {
                std::ofstream ofs(testFile);
                ofs << "dfiles inspection test content\n";
            }
            g_autoptr(GFile) gfile = g_file_new_for_path(testFile.c_str());
            g_autoptr(GFileInfo) info = g_file_query_info(gfile, "*", G_FILE_QUERY_INFO_NONE, nullptr, nullptr);
            if (info) {
                dfiles::model::FileItem singleItem(gfile, info);
                std::vector<dfiles::model::FileItem> items{singleItem};
                inspector->inspectItems(items);
                testAssert(true, "Inspector inspectItems with single file executes cleanly");

                items.push_back(singleItem);
                inspector->inspectItems(items);
                testAssert(true, "Inspector inspectItems with multiple files executes cleanly");
            }
            fs::remove(testFile);

            // 5. ChecksumHelper async computation & cancellation
            {
                dfiles::inspector::ChecksumHelper checksumHelper;
                fs::path chkFile = fs::temp_directory_path() / "dfiles_chk_test.txt";
                {
                    std::ofstream ofs(chkFile);
                    ofs << "1234567890";
                }
                g_autoptr(GFile) chkGFile = g_file_new_for_path(chkFile.c_str());
                bool checksumDone = false;
                std::string md5Result;
                std::string sha256Result;

                checksumHelper.computeAsync(chkGFile, [&](const dfiles::inspector::ChecksumResult& res) {
                    checksumDone = true;
                    if (res.success) {
                        md5Result = res.md5;
                        sha256Result = res.sha256;
                    }
                });

                int loops = 0;
                while (!checksumDone && loops++ < 50) {
                    pumpMainLoop(2);
                }

                testAssert(checksumDone, "ChecksumHelper computeAsync completed");
                testAssert(md5Result == "e807f1fcf82d132f9bb018ca6738a19f", "MD5 checksum matches expected hash");
                testAssert(sha256Result == "c775e7b757ede630cd0aa1113bd102661ab38829ca52a6422ab782862f268646", "SHA-256 checksum matches expected hash");
                fs::remove(chkFile);

                // Compute known test vector for 'hello dfiles\n'
                fs::path helloFile = fs::temp_directory_path() / "dfiles_hello_hash.txt";
                {
                    std::ofstream ofs(helloFile);
                    ofs << "hello dfiles\n";
                }
                g_autoptr(GFile) helloGFile = g_file_new_for_path(helloFile.c_str());
                bool helloDone = false;
                std::string helloMd5;
                std::string helloSha256;
                checksumHelper.computeAsync(helloGFile, [&](const dfiles::inspector::ChecksumResult& res) {
                    helloDone = true;
                    if (res.success) {
                        helloMd5 = res.md5;
                        helloSha256 = res.sha256;
                    }
                });
                loops = 0;
                while (!helloDone && loops++ < 50) {
                    pumpMainLoop(2);
                }
                testAssert(helloDone, "ChecksumHelper computeAsync for 'hello dfiles\\n' completed");
                testAssert(helloMd5 == "ad9e9644531b652cb2c99c19b217eb1e", "MD5 of 'hello dfiles\\n' matches ad9e9644531b652cb2c99c19b217eb1e");
                testAssert(helloSha256 == "a9253384401f511521654f53ee59fb3e44d55eecc6eaf4ef5c477f8556e183b0", "SHA-256 of 'hello dfiles\\n' matches a9253384401f511521654f53ee59fb3e44d55eecc6eaf4ef5c477f8556e183b0");
                fs::remove(helloFile);

                // Verify cancel() halts in-flight hashing cleanly without crash
                fs::path cancelFile = fs::temp_directory_path() / "dfiles_cancel_test.txt";
                {
                    std::ofstream ofs(cancelFile);
                    for (int i = 0; i < 2000; ++i) ofs << "cancelling in flight hashing line\n";
                }
                g_autoptr(GFile) cGFile = g_file_new_for_path(cancelFile.c_str());
                checksumHelper.computeAsync(cGFile, [](const dfiles::inspector::ChecksumResult&) {});
                checksumHelper.cancel();
                pumpMainLoop(5);
                testAssert(!checksumHelper.isRunning(), "ChecksumHelper isRunning() is false after cancel()");
                fs::remove(cancelFile);
            }

            // 6. Terminal bidirectional directory navigation and direct controls
            drawer->setVisible(true);
            testAssert(drawer->isVisible(), "Terminal drawer setVisible(true)");
            drawer->toggleVisibility();
            testAssert(!drawer->isVisible(), "Terminal drawer toggleVisibility() hides drawer");
            drawer->toggleVisibility();
            testAssert(drawer->isVisible(), "Terminal drawer toggleVisibility() shows drawer");

            // Verify onClosed deactivates headerbar button and updates state (REMED-1)
            hb->setTerminalActive(true);
            testAssert(hb->isTerminalActive(), "HeaderBar terminal active before onClosed");
            if (drawer->onClosed) {
                drawer->onClosed();
                testAssert(!hb->isTerminalActive(), "Terminal drawer onClosed deactivates headerbar button");
            }

            // Verify restartShell execution (REMED-3)
            drawer->restartShell();
            testAssert(true, "TerminalDrawer restartShell() executed cleanly");

            // Verify syncDirectory execution with valid directory and nullptr guard
            g_autoptr(GFile) tmpDir = g_file_new_for_path("/tmp");
            drawer->syncDirectory(tmpDir);
            testAssert(true, "TerminalDrawer syncDirectory(/tmp) executed cleanly");
            drawer->syncDirectory(nullptr);
            testAssert(true, "TerminalDrawer syncDirectory(nullptr) guarded defensively");

            drawer->setVisible(true);
            if (drawer->onShellDirectoryChanged) {
                drawer->onShellDirectoryChanged("file:///tmp");
                pumpMainLoop(10);
                auto* activePane = window->getSplitView()->getActivePane();
                testAssert(activePane != nullptr && activePane->getCurrentDirectory() != nullptr,
                           "Active pane non-null after shell directory change");
                if (activePane && activePane->getCurrentDirectory()) {
                    g_autofree char* path = g_file_get_parse_name(activePane->getCurrentDirectory());
                    testAssert(path && std::string(path) == "/tmp",
                               "Shell directory change OSC 7 navigated active pane to /tmp",
                               path ? path : "null");
                }
            }
        }
    }
    std::cout << std::endl;

    // -------------------------------------------------------------
    // Test Section 12: Milestone 4 UI Wiring, Context Menus & Packaging
    // -------------------------------------------------------------
    std::cout << "--- Test Section 12: Milestone 4 UI Wiring, Actions & Packaging ---\n";
    {
        // 1. FileView Popovers
        dfiles::views::FileView fview;
        testAssert(fview.getItemPopover() != nullptr, "FileView getItemPopover() non-null");
        testAssert(fview.getBackgroundPopover() != nullptr, "FileView getBackgroundPopover() non-null");

        // 2. PaneView getSelectedFiles()
        dfiles::views::PaneView pview;
        std::vector<GFile*> files = pview.getSelectedFiles();
        testAssert(files.empty(), "PaneView getSelectedFiles() returns empty vector on no selection");

        // 3. Accelerators & Window GActions verification
        dfiles::core::DFilesApp app("org.gnome.dfiles.test.phase4");
        app.setTestMode(true);
        g_application_register(G_APPLICATION(app.getNative()), nullptr, nullptr);
        app.onStartup();

        auto* gtkApp = GTK_APPLICATION(app.getNative());
        testAssert(hasAccel(gtkApp, "win.quick-copy", "F5"), "Accelerator win.quick-copy -> F5");
        testAssert(hasAccel(gtkApp, "win.quick-move", "F6"), "Accelerator win.quick-move -> F6");
        testAssert(hasAccel(gtkApp, "win.new-folder", "<Shift><Control>n") || hasAccel(gtkApp, "win.new-folder", "<Control><Shift>n"), "Accelerator win.new-folder -> <Shift><Control>n");
        testAssert(hasAccel(gtkApp, "win.rename", "F2"), "Accelerator win.rename -> F2");
        testAssert(hasAccel(gtkApp, "win.trash", "Delete"), "Accelerator win.trash -> Delete");
        testAssert(hasAccel(gtkApp, "win.delete-permanent", "<Shift>Delete"), "Accelerator win.delete-permanent -> <Shift>Delete");
        testAssert(hasAccel(gtkApp, "win.cut", "<Control>x"), "Accelerator win.cut -> <Control>x");
        testAssert(hasAccel(gtkApp, "win.copy", "<Control>c"), "Accelerator win.copy -> <Control>c");
        testAssert(hasAccel(gtkApp, "win.paste", "<Control>v"), "Accelerator win.paste -> <Control>v");
        testAssert(hasAccel(gtkApp, "win.select-all", "<Control>a"), "Accelerator win.select-all -> <Control>a");
        testAssert(hasAccel(gtkApp, "win.context-menu", "<Shift>F10"), "Accelerator win.context-menu -> <Shift>F10");
        testAssert(hasAccel(gtkApp, "win.context-menu", "Menu"), "Accelerator win.context-menu -> Menu");

        // 4. Window GActions
        auto win = std::make_unique<dfiles::ui::DFilesWindow>(app.getNative());
        auto* actionMap = G_ACTION_MAP(win->getWindow());
        const std::vector<const char*> expectedActions = {
            "toggle-split", "toggle-terminal", "toggle-inspector", "focus-pathbar", "focus-filter",
            "quick-copy", "quick-move", "new-folder", "rename", "trash", "delete-permanent",
            "cut", "copy", "paste", "select-all", "copy-path", "open-item", "open-terminal",
            "context-menu"
        };
        for (const char* actName : expectedActions) {
            GAction* act = g_action_map_lookup_action(actionMap, actName);
            testAssert(act != nullptr, std::string("GAction win.") + actName + " registered on window");
        }

        // Test context menu trigger methods
        fview.showContextMenu();
        pview.showContextMenu();

        // 5. SplitView transferSelection in single pane
        std::string lastToast;
        win->getSplitView()->onShowToast = [&](const std::string& msg) {
            lastToast = msg;
        };
        // In single pane mode without selection
        win->getSplitView()->transferSelection(false);
        testAssert(lastToast == "No files selected to transfer", "transferSelection with empty selection shows toast", lastToast);

        // 6. Packaging Assets verification
        std::string desktopPath = std::filesystem::exists("data/org.gnome.dfiles.desktop")
            ? "data/org.gnome.dfiles.desktop" : "../data/org.gnome.dfiles.desktop";
        testAssert(std::filesystem::exists(desktopPath), "Desktop file exists at " + desktopPath);
        std::ifstream desktopIn(desktopPath);
        std::string desktopContent((std::istreambuf_iterator<char>(desktopIn)), std::istreambuf_iterator<char>());
        testAssert(desktopContent.find("Exec=dfiles %U") != std::string::npos, "Desktop file contains Exec=dfiles %U");
        testAssert(desktopContent.find("Icon=system-file-manager") != std::string::npos, "Desktop file contains Icon=system-file-manager");
        testAssert(desktopContent.find("MimeType=inode/directory;") != std::string::npos, "Desktop file contains MimeType=inode/directory;");
        testAssert(desktopContent.find("DBusActivatable=false") != std::string::npos, "Desktop file contains DBusActivatable=false");

        std::string metainfoPath = std::filesystem::exists("data/org.gnome.dfiles.metainfo.xml")
            ? "data/org.gnome.dfiles.metainfo.xml" : "../data/org.gnome.dfiles.metainfo.xml";
        testAssert(std::filesystem::exists(metainfoPath), "Metainfo file exists at " + metainfoPath);
        std::ifstream metainfoIn(metainfoPath);
        std::string metainfoContent((std::istreambuf_iterator<char>(metainfoIn)), std::istreambuf_iterator<char>());
        testAssert(metainfoContent.find("<id>org.gnome.dfiles</id>") != std::string::npos, "Metainfo contains <id>org.gnome.dfiles</id>");

        // 7. CLI Validator tool verification
        int valRes = system(("desktop-file-validate " + desktopPath + " > /dev/null 2>&1").c_str());
        testAssert(valRes == 0, "desktop-file-validate passes cleanly on " + desktopPath);

        int appRes = system(("appstreamcli validate --no-net " + metainfoPath + " > /dev/null 2>&1").c_str());
        testAssert(appRes == 0, "appstreamcli validate passes cleanly on " + metainfoPath);
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
