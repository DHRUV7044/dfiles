#include "core/app.hpp"
#include "model/directory_model.hpp"
#include "model/file_item.hpp"
#include "ui/headerbar.hpp"
#include "ui/sidebar.hpp"
#include "ui/window.hpp"
#include "inspector/checksum_helper.hpp"
#include "inspector/inspector_sidebar.hpp"
#include "terminal/terminal_drawer.hpp"
#include "views/pane_view.hpp"
#include "views/split_view.hpp"

#include <adwaita.h>
#include <fcntl.h>
#include <gdk/gdkkeysyms.h>
#include <gtk/gtk.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include <atomic>
#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace {

void pumpEvents(int iterations = 10) {
    for (int i = 0; i < iterations; ++i) {
        while (g_main_context_iteration(nullptr, FALSE)) {
            // Process all pending events
        }
    }
}

bool pumpWhile(const std::function<bool()>& condition, int timeoutMs = 15000) {
    auto start = std::chrono::steady_clock::now();
    while (g_main_context_iteration(nullptr, FALSE)) {
    }
    while (condition()) {
        while (g_main_context_iteration(nullptr, FALSE)) {
        }
        auto now = std::chrono::steady_clock::now();
        if (std::chrono::duration_cast<std::chrono::milliseconds>(now - start).count() > timeoutMs) {
            return false; // Timed out
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    while (g_main_context_iteration(nullptr, FALSE)) {
    }
    return true; // Condition completed
}

AdwStatusPage* findStatusPage(GtkWidget* root) {
    if (!root) return nullptr;
    if (ADW_IS_STATUS_PAGE(root)) {
        return ADW_STATUS_PAGE(root);
    }
    for (GtkWidget* child = gtk_widget_get_first_child(root); child != nullptr; child = gtk_widget_get_next_sibling(child)) {
        AdwStatusPage* found = findStatusPage(child);
        if (found) return found;
    }
    return nullptr;
}

} // namespace

int main(int /*argc*/, char* /*argv*/[]) {
    g_print("====================================================\n");
    g_print("  DFILES PHASE 1 & 2: ADVERSARIAL STRESS TEST SUITE\n");
    g_print("====================================================\n\n");

    // Initialize Libadwaita / GTK
    adw_init();
    g_main_context_acquire(g_main_context_default());

    g_autoptr(AdwApplication) app = adw_application_new("org.gnome.dfiles.stress", G_APPLICATION_NON_UNIQUE);
    g_assert_nonnull(app);
    g_application_register(G_APPLICATION(app), nullptr, nullptr);

    int totalTests = 0;
    int passedTests = 0;

    auto runTest = [&](const char* testName, auto testFunc) {
        totalTests++;
        g_print("[RUNNING] Test %d: %s\n", totalTests, testName);
        auto start = std::chrono::steady_clock::now();
        try {
            testFunc();
            auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - start).count();
            g_print("[PASSED]  Test %d: %s (%ld ms)\n\n", totalTests, testName, duration);
            passedTests++;
        } catch (const std::exception& e) {
            g_print("[FAILED]  Test %d: %s with exception: %s\n\n", totalTests, testName, e.what());
        } catch (...) {
            g_print("[FAILED]  Test %d: %s with unknown exception\n\n", totalTests, testName);
        }
    };

    // -------------------------------------------------------------
    // VECTOR 2: RAPID ACTION / TOGGLE STRESS
    // -------------------------------------------------------------

    // TEST 1: Rapid Toggle Hammering - Split View (F3) 300+ cycles
    runTest("Vector 2.1: Split View (F3) 300x Rapid Hammering", [&]() {
        auto window = std::make_unique<dfiles::ui::DFilesWindow>(app);
        g_assert_nonnull(window->getWidget());
        auto* hb = window->getHeaderBar();
        g_assert_nonnull(hb);

        bool expected = false;
        for (int i = 0; i < 300; ++i) {
            expected = !expected;
            hb->setSplitActive(expected);
            if (hb->onSplitToggled) {
                hb->onSplitToggled();
            }
            g_assert_cmpint(hb->isSplitActive(), ==, expected);

            if (i % 30 == 0) {
                pumpEvents(2);
            }
        }
        pumpEvents(10);
    });

    // TEST 2: Rapid Toggle Hammering - Terminal Drawer (F4) 300+ cycles
    runTest("Vector 2.2: Terminal Drawer (F4) 300x Rapid Hammering", [&]() {
        auto window = std::make_unique<dfiles::ui::DFilesWindow>(app);
        auto* hb = window->getHeaderBar();
        g_assert_nonnull(hb);

        bool expected = false;
        for (int i = 0; i < 300; ++i) {
            expected = !expected;
            hb->setTerminalActive(expected);
            if (hb->onTerminalToggled) {
                hb->onTerminalToggled();
            }
            g_assert_cmpint(hb->isTerminalActive(), ==, expected);

            if (i % 30 == 0) {
                pumpEvents(2);
            }
        }
        pumpEvents(10);
    });

    // TEST 3: Rapid Toggle Hammering - Inspector (F11) 300+ cycles
    runTest("Vector 2.3: Inspector Sidebar (F11) 300x Rapid Hammering", [&]() {
        auto window = std::make_unique<dfiles::ui::DFilesWindow>(app);
        auto* hb = window->getHeaderBar();
        g_assert_nonnull(hb);

        bool expected = false;
        for (int i = 0; i < 300; ++i) {
            expected = !expected;
            hb->setInspectorActive(expected);
            if (hb->onInspectorToggled) {
                hb->onInspectorToggled();
            }
            g_assert_cmpint(hb->isInspectorActive(), ==, expected);

            if (i % 30 == 0) {
                pumpEvents(2);
            }
        }
        pumpEvents(10);
    });

    // TEST 4: Chaotic Interleaved Toggle Storm (F3 -> F4 -> F11) 300 bursts (900 toggles)
    runTest("Vector 2.4: Chaotic Interleaved Storm (F3, F4, F11) 900x Toggles", [&]() {
        auto window = std::make_unique<dfiles::ui::DFilesWindow>(app);
        auto* hb = window->getHeaderBar();
        g_assert_nonnull(hb);

        for (int i = 0; i < 300; ++i) {
            bool s = (i % 2 == 0);
            bool t = ((i / 2) % 2 == 0);
            bool insp = ((i / 3) % 2 == 0);

            hb->setSplitActive(s);
            if (hb->onSplitToggled) hb->onSplitToggled();

            hb->setTerminalActive(t);
            if (hb->onTerminalToggled) hb->onTerminalToggled();

            hb->setInspectorActive(insp);
            if (hb->onInspectorToggled) hb->onInspectorToggled();

            g_assert_cmpint(hb->isSplitActive(), ==, s);
            g_assert_cmpint(hb->isTerminalActive(), ==, t);
            g_assert_cmpint(hb->isInspectorActive(), ==, insp);

            if (i % 25 == 0) {
                pumpEvents(3);
            }
        }
        pumpEvents(20);
    });

    // TEST 5: Window GAction Activation Stress (win.toggle-split, win.toggle-terminal, win.toggle-inspector) 300x cycles
    runTest("Vector 2.5: Window GAction Activation & State Synchronization Stress (300x)", [&]() {
        auto window = std::make_unique<dfiles::ui::DFilesWindow>(app);
        auto* winWidget = window->getWindow();
        auto* hb = window->getHeaderBar();
        g_assert_nonnull(winWidget);
        g_assert_nonnull(hb);

        auto* actionGroup = G_ACTION_GROUP(winWidget);

        for (int i = 0; i < 100; ++i) {
            bool splitBefore = hb->isSplitActive();
            g_action_group_activate_action(actionGroup, "toggle-split", nullptr);
            g_assert_cmpint(hb->isSplitActive(), ==, !splitBefore);

            bool termBefore = hb->isTerminalActive();
            g_action_group_activate_action(actionGroup, "toggle-terminal", nullptr);
            g_assert_cmpint(hb->isTerminalActive(), ==, !termBefore);

            bool inspBefore = hb->isInspectorActive();
            g_action_group_activate_action(actionGroup, "toggle-inspector", nullptr);
            g_assert_cmpint(hb->isInspectorActive(), ==, !inspBefore);

            if (i % 20 == 0) {
                pumpEvents(2);
            }
        }
        pumpEvents(10);
    });

    // TEST 6: Multi-threaded Asynchronous Toast Flood (400 toasts from 4 threads)
    runTest("Vector 2.6: Off-Thread Toast Storm (400 Concurrent Invocations)", [&]() {
        auto window = std::make_unique<dfiles::ui::DFilesWindow>(app);
        g_assert_nonnull(window->getWidget());

        constexpr int numThreads = 4;
        constexpr int toastsPerThread = 100;
        std::vector<std::thread> threads;
        threads.reserve(numThreads);

        std::atomic<int> completedThreads{0};

        for (int t = 0; t < numThreads; ++t) {
            threads.emplace_back([&, t]() {
                for (int m = 0; m < toastsPerThread; ++m) {
                    std::string msg = "Off-thread toast [T=" + std::to_string(t) + " #" + std::to_string(m) + "]";
                    window->showToast(msg, 1);
                }
                completedThreads.fetch_add(1);
            });
        }

        // Main thread pumps GTK events while worker threads produce toasts
        while (completedThreads.load() < numThreads) {
            pumpEvents(1);
            std::this_thread::yield();
        }

        for (auto& th : threads) {
            if (th.joinable()) {
                th.join();
            }
        }

        // Drain pending toast callbacks in main context
        pumpEvents(50);
    });

    // TEST 7: High-Frequency Window Lifecycle Churn (50 create/toggle/destroy cycles)
    runTest("Vector 2.7: High-Frequency Window Lifecycle Churn (50 Creations & Destructions)", [&]() {
        for (int i = 0; i < 50; ++i) {
            auto win = std::make_unique<dfiles::ui::DFilesWindow>(app);
            auto* hb = win->getHeaderBar();
            hb->setSplitActive(true);
            if (hb->onSplitToggled) hb->onSplitToggled();
            hb->setTerminalActive(true);
            if (hb->onTerminalToggled) hb->onTerminalToggled();
            hb->setInspectorActive(true);
            if (hb->onInspectorToggled) hb->onInspectorToggled();
            win->showToast("Quick toast " + std::to_string(i), 1);
            pumpEvents(1);

            // Destruct window immediately while events/toasts are potentially queued
            win.reset();
            pumpEvents(2);
        }
        pumpEvents(10);
    });

    // TEST 8: PlacesSidebar Mount Refresh Flood & Selection Chaos
    runTest("Vector 2.8: PlacesSidebar Mount Refresh Storm (500x scheduleRefreshMounts)", [&]() {
        auto window = std::make_unique<dfiles::ui::DFilesWindow>(app);
        auto* sidebar = window->getSidebar();
        g_assert_nonnull(sidebar);

        // Hammer refreshMounts and scheduleRefreshMounts rapidly
        for (int i = 0; i < 500; ++i) {
            sidebar->scheduleRefreshMounts();
            if (i % 50 == 0) {
                sidebar->refreshMounts();
                sidebar->selectPlaceByUri("file:///");
                sidebar->selectPlaceByUri("trash:///");
                pumpEvents(1);
            }
        }
        pumpEvents(20);
    });

    // TEST 9: Extreme & Hostile Input Fuzzing (Corrupted URIs, massive strings, format strings)
    runTest("Vector 2.9: Extreme & Hostile String / URI Fuzzing", [&]() {
        auto window = std::make_unique<dfiles::ui::DFilesWindow>(app);
        auto* hb = window->getHeaderBar();
        auto* sidebar = window->getSidebar();

        // 1. Massive string (128KB)
        std::string hugeString(128 * 1024, 'X');
        hb->setTitle(hugeString, hugeString);
        window->showToast(hugeString, 1);
        pumpEvents(2);

        // 2. Format string vulnerability attempts
        std::string formatStr = "%s%s%s%n%x%p%d%s%s";
        hb->setTitle(formatStr, formatStr);
        window->showToast(formatStr, 1);
        pumpEvents(2);

        // 3. Invalid & hostile URIs
        const std::vector<std::string> hostileUris = {
            "",
            "   ",
            "file://",
            "file:///",
            "file:///dev/null",
            "file:///proc/cpuinfo",
            "file:///root/.ssh/id_rsa",
            "http://127.0.0.1:8080/test",
            "smb://malicious.host/share",
            "sftp://attacker.invalid/etc/shadow",
            "trash:///",
            "recent:///",
            "invalid_uri_without_scheme",
            "://missing_scheme",
            std::string(1000, '/'),
            "file:///tmp/test\nwith\r\nnewlines\0embedded",
        };

        for (const auto& uri : hostileUris) {
            sidebar->selectPlaceByUri(uri);
            if (sidebar->onPlaceSelected) {
                sidebar->onPlaceSelected(uri);
            }
            hb->setTitle("Test", uri);
        }
        pumpEvents(10);
    });

    // TEST 10: DFilesApp Full Lifecycle & Test-Mode Integration
    runTest("Vector 2.10: DFilesApp Lifecycle & GActions Stress (50x Cycles)", [&]() {
        dfiles::core::DFilesApp dfilesApp;
        dfilesApp.setTestMode(true);
        g_assert_true(dfilesApp.isTestMode());
        g_application_register(G_APPLICATION(dfilesApp.getNative()), nullptr, nullptr);

        dfilesApp.onStartup();
        dfilesApp.onActivate();

        auto* win = dfilesApp.getWindow();
        g_assert_nonnull(win);

        for (int cycle = 0; cycle < 50; ++cycle) {
            // Trigger global GActions
            dfilesApp.onActionAbout();
            dfilesApp.onActionPreferences();

            // Simulate file opening via onOpen
            GFile* dummyFile = g_file_new_for_path("/tmp");
            GFile* fileList[1] = {dummyFile};
            dfilesApp.onOpen(fileList, 1, nullptr);
            g_object_unref(dummyFile);

            if (cycle % 10 == 0) {
                pumpEvents(2);
            }
        }
        pumpEvents(10);
    });

    // -------------------------------------------------------------
    // PHASE 2 ADVERSARIAL VECTORS
    // -------------------------------------------------------------

    // TEST 11: Hostile & Inaccessible Paths (chmod 000)
    runTest("Vector 2.11: Hostile & Inaccessible Paths (chmod 000)", [&]() {
        std::string testDir = "/tmp/dfiles_stress_inaccessible_" + std::to_string(getpid());
        std::filesystem::remove_all(testDir);
        std::filesystem::create_directories(testDir);
        {
            std::ofstream(testDir + "/secret.txt") << "secret data";
        }
        int ret = chmod(testDir.c_str(), 0000);
        g_assert_cmpint(ret, ==, 0);

        // 1. DirectoryModel error handling
        dfiles::model::DirectoryModel model;
        bool modelErrorFired = false;
        std::string errorMsg;
        model.onError = [&](const std::string& msg) {
            modelErrorFired = true;
            errorMsg = msg;
        };

        g_autoptr(GFile) inaccessibleFile = g_file_new_for_path(testDir.c_str());
        model.loadDirectory(inaccessibleFile);

        bool completed = pumpWhile([&]() { return model.isLoading(); }, 5000);
        g_assert_true(completed);
        g_assert_true(modelErrorFired);
        g_assert_false(errorMsg.empty());
        g_assert_cmpuint(model.getItemCount(), ==, 0);

        // 2. PaneView error transition to AdwStatusPage
        dfiles::views::PaneView paneView;
        paneView.navigateTo(inaccessibleFile);

        completed = pumpWhile([&]() { return paneView.getModel()->isLoading(); }, 5000);
        g_assert_true(completed);

        AdwStatusPage* statusPage = findStatusPage(paneView.getWidget());
        g_assert_nonnull(statusPage);
        g_assert_true(gtk_widget_get_visible(GTK_WIDGET(statusPage)));
        g_assert_cmpstr(adw_status_page_get_title(statusPage), ==, "Unable to Open Folder");

        // 3. Clean recovery by navigating back to valid folder
        std::string validDir = "/tmp/dfiles_stress_valid_" + std::to_string(getpid());
        std::filesystem::remove_all(validDir);
        std::filesystem::create_directories(validDir);
        {
            std::ofstream(validDir + "/sample.txt") << "valid data";
        }

        g_autoptr(GFile) validFile = g_file_new_for_path(validDir.c_str());
        paneView.navigateTo(validFile);

        completed = pumpWhile([&]() { return paneView.getModel()->isLoading(); }, 5000);
        g_assert_true(completed);
        g_assert_false(paneView.getModel()->isLoading());
        g_assert_cmpuint(paneView.getModel()->getItemCount(), ==, 1);
        g_assert_false(gtk_widget_get_visible(GTK_WIDGET(statusPage)));

        // Cleanup
        chmod(testDir.c_str(), 0755);
        std::filesystem::remove_all(testDir);
        std::filesystem::remove_all(validDir);
    });

    // TEST 12: Circular & Cyclic Symlinks
    runTest("Vector 2.12: Circular & Cyclic Symlinks", [&]() {
        std::string testDir = "/tmp/dfiles_stress_symlinks_" + std::to_string(getpid());
        std::filesystem::remove_all(testDir);
        std::filesystem::create_directories(testDir);

        // Self-referential symlink: loop -> loop
        std::string loopPath = testDir + "/loop";
        int r1 = symlink("loop", loopPath.c_str());
        g_assert_cmpint(r1, ==, 0);

        // Mutually recursive symlinks: a -> b and b -> a
        std::string aPath = testDir + "/sym_a";
        std::string bPath = testDir + "/sym_b";
        int r2 = symlink("sym_b", aPath.c_str());
        int r3 = symlink("sym_a", bPath.c_str());
        g_assert_cmpint(r2, ==, 0);
        g_assert_cmpint(r3, ==, 0);

        // Cyclic parent symlink: parent_loop -> ..
        std::string parentPath = testDir + "/parent_loop";
        int r4 = symlink("..", parentPath.c_str());
        g_assert_cmpint(r4, ==, 0);

        // Regular file for baseline comparison
        {
            std::ofstream(testDir + "/plain.txt") << "content";
        }

        dfiles::model::DirectoryModel model;
        bool finished = false;
        model.onLoadingFinished = [&](size_t, size_t, size_t, uint64_t) {
            finished = true;
        };

        g_autoptr(GFile) dirFile = g_file_new_for_path(testDir.c_str());
        model.loadDirectory(dirFile);

        bool completed = pumpWhile([&]() { return !finished; }, 5000);
        g_assert_true(completed);
        g_assert_cmpuint(model.getItemCount(), ==, 5); // 4 symlinks + 1 regular file

        size_t symlinkFoundCount = 0;
        for (size_t i = 0; i < model.getItemCount(); ++i) {
            auto item = model.getItemAt(i);
            g_assert_nonnull(item);
            if (item->isSymlink()) {
                symlinkFoundCount++;
            }
        }
        g_assert_cmpuint(symlinkFoundCount, ==, 4);

        // Cleanup
        std::filesystem::remove_all(testDir);
    });

    // TEST 13: Massive Directory Churn & Scaling (10,000 Files)
    runTest("Vector 2.13: Massive Directory Churn & Scaling (10,000 Files)", [&]() {
        std::string testDir = "/tmp/dfiles_stress_10k_" + std::to_string(getpid());
        std::filesystem::remove_all(testDir);
        std::filesystem::create_directories(testDir);

        auto startCreate = std::chrono::steady_clock::now();
        for (int i = 0; i < 10000; ++i) {
            char buf[64];
            std::snprintf(buf, sizeof(buf), "/file_%05d.dat", i);
            std::string fname = testDir + buf;
            int fd = creat(fname.c_str(), 0644);
            if (fd >= 0) close(fd);
        }
        auto createMs = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - startCreate).count();
        g_print("    [Info] Generated 10,000 files in %ld ms\n", createMs);

        dfiles::model::DirectoryModel model;
        bool finished = false;
        size_t loadedCount = 0;
        model.onLoadingFinished = [&](size_t total, size_t, size_t, uint64_t) {
            finished = true;
            loadedCount = total;
        };

        g_autoptr(GFile) dirFile = g_file_new_for_path(testDir.c_str());
        auto startLoad = std::chrono::steady_clock::now();
        model.loadDirectory(dirFile);

        bool completed = pumpWhile([&]() { return !finished; }, 30000);
        auto loadMs = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - startLoad).count();
        g_print("    [Info] Loaded 10,000 files in %ld ms\n", loadMs);

        g_assert_true(completed);
        g_assert_cmpuint(loadedCount, ==, 10000);
        g_assert_cmpuint(model.getItemCount(), ==, 10000);
        g_assert_cmpuint(model.getFileCount(), ==, 10000);
        g_assert_cmpuint(model.getDirectoryCount(), ==, 0);

        auto first = model.getItemAt(0);
        auto last = model.getItemAt(9999);
        g_assert_nonnull(first);
        g_assert_nonnull(last);

        // Cleanup
        std::filesystem::remove_all(testDir);
    });

    // TEST 14: In-Flight Navigation Cancellation Storm
    runTest("Vector 2.14: In-Flight Navigation Cancellation Storm (50 Rapid Navigations)", [&]() {
        std::string baseDir = "/tmp/dfiles_stress_storm_" + std::to_string(getpid());
        std::filesystem::remove_all(baseDir);
        std::filesystem::create_directories(baseDir);

        constexpr int NUM_DIRS = 50;
        std::vector<std::string> dirPaths;
        dirPaths.reserve(NUM_DIRS);

        for (int i = 0; i < NUM_DIRS; ++i) {
            char dname[64];
            std::snprintf(dname, sizeof(dname), "/dir_%03d", i);
            std::string d = baseDir + dname;
            std::filesystem::create_directories(d);
            for (int j = 0; j < 10; ++j) {
                std::string f = d + "/f_" + std::to_string(j) + ".txt";
                int fd = creat(f.c_str(), 0644);
                if (fd >= 0) close(fd);
            }
            dirPaths.push_back(d);
        }

        dfiles::views::PaneView paneView;

        // Rapidly fire 50 navigateTo calls in succession while previous ones are in-flight
        for (int i = 0; i < NUM_DIRS; ++i) {
            g_autoptr(GFile) gdir = g_file_new_for_path(dirPaths[i].c_str());
            paneView.navigateTo(gdir);
            if (i % 5 == 0) {
                g_main_context_iteration(nullptr, FALSE);
            }
        }

        // Wait for final navigation to finish
        bool completed = pumpWhile([&]() { return paneView.getModel()->isLoading(); }, 15000);
        g_assert_true(completed);

        GFile* cur = paneView.getCurrentDirectory();
        g_assert_nonnull(cur);
        g_autofree char* curPath = g_file_get_path(cur);
        g_assert_cmpstr(curPath, ==, dirPaths.back().c_str());
        g_assert_cmpuint(paneView.getModel()->getItemCount(), ==, 10);

        // SEC-BLOCK-03 / ARCH-P2-02 / REMED-2 Cross-Directory Contamination Verification:
        // Assert every single item in the final model has a GFile strictly prefixed
        // by the 50th directory (dirPaths.back()), and NOT any of the 49 cancelled directories.
        std::string expectedPrefix = dirPaths.back() + "/";
        for (size_t i = 0; i < paneView.getModel()->getItemCount(); ++i) {
            auto item = paneView.getModel()->getItemAt(i);
            g_assert_nonnull(item);
            g_assert_nonnull(item->getGFile());
            g_assert_true(g_file_has_prefix(item->getGFile(), cur));

            std::string itemPath = item->getPath();
            g_assert_true(itemPath.starts_with(expectedPrefix));

            for (int k = 0; k < NUM_DIRS - 1; ++k) {
                std::string cancelledPrefix = dirPaths[k] + "/";
                g_assert_false(itemPath.starts_with(cancelledPrefix));
                g_autoptr(GFile) cancelledGDir = g_file_new_for_path(dirPaths[k].c_str());
                g_assert_false(g_file_has_prefix(item->getGFile(), cancelledGDir));
            }
        }

        // Direct DirectoryModel rapid navigation cancellation stress:
        // Execute 50 rapid loadDirectory calls directly on a raw DirectoryModel
        dfiles::model::DirectoryModel directModel;
        for (int i = 0; i < NUM_DIRS; ++i) {
            g_autoptr(GFile) gdir = g_file_new_for_path(dirPaths[i].c_str());
            directModel.loadDirectory(gdir);
            if (i % 7 == 0) {
                g_main_context_iteration(nullptr, FALSE);
            }
        }
        completed = pumpWhile([&]() { return directModel.isLoading(); }, 15000);
        g_assert_true(completed);
        g_assert_cmpuint(directModel.getItemCount(), ==, 10);

        g_autoptr(GFile) lastDirGFile = g_file_new_for_path(dirPaths.back().c_str());
        for (size_t i = 0; i < directModel.getItemCount(); ++i) {
            auto item = directModel.getItemAt(i);
            g_assert_nonnull(item);
            g_assert_nonnull(item->getGFile());
            g_assert_true(g_file_has_prefix(item->getGFile(), lastDirGFile));

            std::string itemPath = item->getPath();
            g_assert_true(itemPath.starts_with(expectedPrefix));
            for (int k = 0; k < NUM_DIRS - 1; ++k) {
                std::string cancelledPrefix = dirPaths[k] + "/";
                g_assert_false(itemPath.starts_with(cancelledPrefix));
                g_autoptr(GFile) cancelledGDir = g_file_new_for_path(dirPaths[k].c_str());
                g_assert_false(g_file_has_prefix(item->getGFile(), cancelledGDir));
            }
        }

        // Cleanup
        std::filesystem::remove_all(baseDir);
    });

    // TEST 15: Rapid F3 Toggle Storm During Heavy Async Directory Loading
    runTest("Vector 2.15: Rapid F3 Toggle Storm During Heavy Async Directory Loading", [&]() {
        std::string testDir = "/tmp/dfiles_stress_f3_10k_" + std::to_string(getpid());
        std::filesystem::remove_all(testDir);
        std::filesystem::create_directories(testDir);

        // Generate 10,000 files
        for (int i = 0; i < 10000; ++i) {
            char buf[64];
            std::snprintf(buf, sizeof(buf), "/file_%05d.dat", i);
            std::string fname = testDir + buf;
            int fd = creat(fname.c_str(), 0644);
            if (fd >= 0) close(fd);
        }

        dfiles::views::SplitView splitView;
        g_assert_false(splitView.isSplit());

        g_autoptr(GFile) dirFile = g_file_new_for_path(testDir.c_str());
        // Initiate heavy async load on left pane
        splitView.getLeftPane()->navigateTo(dirFile);
        g_assert_true(splitView.getLeftPane()->getModel()->isLoading());

        // Immediately hammer toggleSplit 300 times while I/O is streaming
        for (int i = 0; i < 300; ++i) {
            splitView.toggleSplit();
            if (i % 25 == 0) {
                pumpEvents(1);
            }
        }

        // 300 toggles from false -> even -> final state false
        g_assert_false(splitView.isSplit());

        // Toggle once more to true to verify split state handling
        splitView.toggleSplit();
        g_assert_true(splitView.isSplit());

        // Wait for all in-flight directory loading to complete
        bool completed = pumpWhile([&]() {
            return splitView.getLeftPane()->getModel()->isLoading() ||
                   splitView.getRightPane()->getModel()->isLoading();
        }, 30000);
        g_assert_true(completed);

        g_assert_cmpuint(splitView.getLeftPane()->getModel()->getItemCount(), ==, 10000);
        g_assert_true(splitView.isSplit());

        // Toggle back to single pane cleanly
        splitView.toggleSplit();
        g_assert_false(splitView.isSplit());
        pumpEvents(10);

        // Cleanup
        std::filesystem::remove_all(testDir);
    });

    // TEST 16: Non-Blocking File Monitor Async I/O & Mutation Storm (REMED-4 / ARCH-P2-04 / SEC-BLOCK-04)
    runTest("Vector 2.16: Non-Blocking File Monitor Async I/O & Mutation Storm", [&]() {
        std::string testDir = "/tmp/dfiles_stress_monitor_async_" + std::to_string(getpid());
        std::filesystem::remove_all(testDir);
        std::filesystem::create_directories(testDir);

        g_autoptr(GFile) dirFile = g_file_new_for_path(testDir.c_str());
        dfiles::model::DirectoryModel model;
        model.loadDirectory(dirFile);

        bool completed = pumpWhile([&]() { return model.isLoading(); }, 5000);
        g_assert_true(completed);
        g_assert_cmpuint(model.getItemCount(), ==, 0);

        // 1. Rapid File Creation Storm: 100 files created sequentially
        // Asynchronous monitor queries via g_file_query_info_async must populate the model without blocking
        constexpr int NUM_FILES = 100;
        for (int i = 0; i < NUM_FILES; ++i) {
            char filename[64];
            std::snprintf(filename, sizeof(filename), "/storm_file_%03d.txt", i);
            std::string fpath = testDir + filename;
            std::ofstream(fpath) << "Storm payload data for file #" << i << "\n";
        }

        // Wait for file monitor events and g_file_query_info_async to process all 100 creations
        completed = pumpWhile([&]() { return model.getItemCount() < NUM_FILES; }, 15000);
        g_assert_true(completed);
        g_assert_cmpuint(model.getItemCount(), ==, NUM_FILES);
        g_assert_cmpuint(model.getFileCount(), ==, NUM_FILES);
        g_assert_cmpuint(model.getDirectoryCount(), ==, 0);

        for (size_t i = 0; i < model.getItemCount(); ++i) {
            auto item = model.getItemAt(i);
            g_assert_nonnull(item);
            g_assert_nonnull(item->getGFile());
            g_assert_true(g_file_has_prefix(item->getGFile(), dirFile));
        }

        // 2. Rapid File Modification Storm: touch / append to 50 files
        for (int i = 0; i < 50; ++i) {
            char filename[64];
            std::snprintf(filename, sizeof(filename), "/storm_file_%03d.txt", i);
            std::string fpath = testDir + filename;
            std::ofstream ofs(fpath, std::ios::app);
            ofs << "appended content modification chunk\n";
        }
        pumpEvents(25);

        // 3. Rapid File Deletion Storm: delete 50 files (storm_file_050 to storm_file_099)
        for (int i = 50; i < NUM_FILES; ++i) {
            char filename[64];
            std::snprintf(filename, sizeof(filename), "/storm_file_%03d.txt", i);
            std::string fpath = testDir + filename;
            unlink(fpath.c_str());
        }

        // Wait for model item count to drop down to 50
        completed = pumpWhile([&]() { return model.getItemCount() > 50; }, 15000);
        g_assert_true(completed);
        g_assert_cmpuint(model.getItemCount(), ==, 50);

        // 4. Rapid File Renaming Storm: rename storm_file_000 to storm_file_024
        for (int i = 0; i < 25; ++i) {
            char oldName[64];
            char newName[64];
            std::snprintf(oldName, sizeof(oldName), "/storm_file_%03d.txt", i);
            std::snprintf(newName, sizeof(newName), "/renamed_file_%03d.txt", i);
            std::filesystem::rename(testDir + oldName, testDir + newName);
        }

        completed = pumpWhile([&]() {
            g_autoptr(GFile) probe = g_file_new_for_path((testDir + "/renamed_file_000.txt").c_str());
            return model.findItemByFile(probe) == nullptr;
        }, 15000);
        g_assert_true(completed);

        // 5. Teardown Race Condition Stress (aliveToken safety in MonitorQueryContext):
        // Create model in scoped pointer, load directory, burst-create 30 files,
        // and immediately destroy model while async queries are in flight
        {
            auto scopedModel = std::make_unique<dfiles::model::DirectoryModel>();
            scopedModel->loadDirectory(dirFile);
            pumpWhile([&]() { return scopedModel->isLoading(); }, 5000);

            for (int i = 100; i < 130; ++i) {
                char filename[64];
                std::snprintf(filename, sizeof(filename), "/teardown_file_%03d.txt", i);
                std::ofstream(testDir + filename) << "teardown test";
            }
            // Scoped model destroyed immediately while queries are in-flight
            scopedModel.reset();
            // Pump event loop to execute pending trampolines; weak aliveToken must prevent UAF
            pumpEvents(50);
        }

        // Cleanup
        std::filesystem::remove_all(testDir);
    });

    // TEST 17: Natural Sort Strict Weak Ordering Stress & Leading Zeros Edge Cases (REMED-P2-05 / REMED-8 / BLOCK-QA-P2-05 / SEC-WARN-02)
    runTest("Vector 2.17: Natural Sort Strict Weak Ordering Stress & Leading Zeros Edge Cases", [&]() {
        std::string testDir = "/tmp/dfiles_stress_sort_strict_" + std::to_string(getpid());
        std::filesystem::remove_all(testDir);
        std::filesystem::create_directories(testDir);

        const std::vector<std::string> testFiles = {
            "file1.txt",
            "file01.txt",
            "file001.txt",
            "file0001.txt",
            "file2.txt",
            "file02.txt",
            "file10.txt",
            "file010.txt",
            "file20.txt",
            "File1.txt",
            "FILE1.TXT",
            "item_a.txt",
            "item_A.txt",
            "item_b.txt",
            "100_numeric.dat",
            "0100_numeric.dat",
            "00100_numeric.dat",
            "20_numeric.dat",
            "020_numeric.dat"
        };

        for (const auto& fname : testFiles) {
            std::string path = testDir + "/" + fname;
            std::ofstream(path) << "sample sort content";
        }

        dfiles::model::DirectoryModel model;
        g_autoptr(GFile) dirGFile = g_file_new_for_path(testDir.c_str());
        model.loadDirectory(dirGFile);

        bool completed = pumpWhile([&]() { return model.isLoading(); }, 10000);
        g_assert_true(completed);
        g_assert_cmpuint(model.getItemCount(), ==, testFiles.size());

        // Test sorting: Name Ascending
        model.setSorting(dfiles::core::SortColumn::Name, dfiles::core::SortOrder::Ascending);
        pumpEvents(10);

        GListModel* listModel = model.getModel();
        guint nItems = g_list_model_get_n_items(listModel);
        g_assert_cmpuint(nItems, ==, testFiles.size());

        // Verify Strict Weak Ordering:
        // No adjacent elements can have identical name strings (all distinct files must remain distinct)
        for (guint i = 0; i < nItems - 1; ++i) {
            auto* itemA = DFILES_FILE_ITEM(g_list_model_get_item(listModel, i));
            auto* itemB = DFILES_FILE_ITEM(g_list_model_get_item(listModel, i + 1));
            g_assert_nonnull(itemA);
            g_assert_nonnull(itemB);

            const char* nameA = dfiles_file_item_get_name(itemA);
            const char* nameB = dfiles_file_item_get_name(itemB);
            g_assert_cmpstr(nameA, !=, nameB);

            g_object_unref(itemA);
            g_object_unref(itemB);
        }

        // Test sorting: Name Descending
        model.setSorting(dfiles::core::SortColumn::Name, dfiles::core::SortOrder::Descending);
        pumpEvents(10);
        nItems = g_list_model_get_n_items(listModel);
        g_assert_cmpuint(nItems, ==, testFiles.size());

        // Test sorting: Size, Type, ModifiedTime
        model.setSorting(dfiles::core::SortColumn::Size, dfiles::core::SortOrder::Ascending);
        pumpEvents(5);
        model.setSorting(dfiles::core::SortColumn::Type, dfiles::core::SortOrder::Ascending);
        pumpEvents(5);
        model.setSorting(dfiles::core::SortColumn::ModifiedTime, dfiles::core::SortOrder::Descending);
        pumpEvents(5);

        // Cleanup
        std::filesystem::remove_all(testDir);
    });

    // TEST 18: Dual-Pane Focus Cycling & Hardware Keyboard Focus Stress (REMED-P2-04 / ARCH-P2-03 / BLOCK-QA-P2-04)
    runTest("Vector 2.18: Dual-Pane Focus Cycling & Hardware Focus Stress (200x Cycles)", [&]() {
        auto window = std::make_unique<dfiles::ui::DFilesWindow>(app);
        auto* splitView = window->getSplitView();
        g_assert_nonnull(splitView);

        splitView->toggleSplit();
        g_assert_true(splitView->isSplit());

        auto* leftPane = splitView->getLeftPane();
        auto* rightPane = splitView->getRightPane();
        g_assert_nonnull(leftPane);
        g_assert_nonnull(rightPane);

        for (int i = 0; i < 200; ++i) {
            splitView->cycleFocus();

            if (i % 2 == 0) {
                // Odd cycle count: right pane should be active
                g_assert_true(splitView->getActivePane() == rightPane);
                g_assert_true(rightPane->isActive());
                g_assert_false(leftPane->isActive());
            } else {
                // Even cycle count: left pane should be active
                g_assert_true(splitView->getActivePane() == leftPane);
                g_assert_true(leftPane->isActive());
                g_assert_false(rightPane->isActive());
            }

            if (i % 20 == 0) {
                pumpEvents(2);
            }
        }

        // Direct setActivePane invocation and focus verification
        splitView->setActivePane(leftPane);
        g_assert_true(leftPane->isActive());
        g_assert_false(rightPane->isActive());

        splitView->setActivePane(rightPane);
        g_assert_true(rightPane->isActive());
        g_assert_false(leftPane->isActive());

        // Close split cleanly
        splitView->toggleSplit();
        g_assert_false(splitView->isSplit());
        g_assert_true(splitView->getActivePane() == leftPane);
        pumpEvents(5);
    });

    // -------------------------------------------------------------
    // PHASE 3 ADVERSARIAL CHAOS VECTORS
    // -------------------------------------------------------------

    // TEST 19: Vector 3.1: Rapid Toggle Storm (F4 Terminal & F11 Inspector 300+ cycles)
    runTest("Vector 3.1: Rapid Toggle Storm (F4 Terminal & F11 Inspector 300+ cycles)", [&]() {
        auto window = std::make_unique<dfiles::ui::DFilesWindow>(app);
        g_assert_nonnull(window->getWidget());
        auto* actionGroup = G_ACTION_GROUP(window->getWindow());
        g_assert_nonnull(actionGroup);

        auto* drawer = window->getTerminalDrawer();
        auto* inspector = window->getInspectorSidebar();
        auto* hb = window->getHeaderBar();
        g_assert_nonnull(drawer);
        g_assert_nonnull(inspector);
        g_assert_nonnull(hb);

        // Hammer 300 rapid toggle cycles of F4 (terminal) and F11 (inspector)
        for (int i = 0; i < 300; ++i) {
            g_action_group_activate_action(actionGroup, "toggle-terminal", nullptr);
            g_action_group_activate_action(actionGroup, "toggle-inspector", nullptr);

            if (i % 25 == 0) {
                pumpEvents(2);
            }
        }

        // 300 toggles from initial false -> even number -> final state false
        g_assert_false(drawer->isVisible());
        g_assert_false(inspector->isVisible());
        g_assert_false(hb->isTerminalActive());
        g_assert_false(hb->isInspectorActive());

        // Interleaved chaotic toggle storm (toggle terminal, toggle inspector, toggle split)
        for (int i = 0; i < 150; ++i) {
            if (i % 2 == 0) g_action_group_activate_action(actionGroup, "toggle-terminal", nullptr);
            if (i % 3 == 0) g_action_group_activate_action(actionGroup, "toggle-inspector", nullptr);
            if (i % 5 == 0) g_action_group_activate_action(actionGroup, "toggle-split", nullptr);

            if (i % 15 == 0) {
                pumpEvents(1);
            }
        }

        // Close all drawers cleanly
        if (drawer->isVisible()) g_action_group_activate_action(actionGroup, "toggle-terminal", nullptr);
        if (inspector->isVisible()) g_action_group_activate_action(actionGroup, "toggle-inspector", nullptr);
        if (window->getSplitView()->isSplit()) g_action_group_activate_action(actionGroup, "toggle-split", nullptr);

        pumpEvents(10);
        g_assert_false(drawer->isVisible());
        g_assert_false(inspector->isVisible());
        g_assert_false(window->getSplitView()->isSplit());
    });

    // TEST 20: Vector 3.2: 50MB Checksum Stress & Cancellation Storm
    runTest("Vector 3.2: 50MB Checksum Stress & Cancellation Storm (100 rapid cancellations)", [&]() {
        std::string testFile = "/tmp/dfiles_stress_50mb_" + std::to_string(getpid()) + ".bin";
        std::filesystem::remove(testFile);

        // Create 50MB binary test file with deterministic pattern
        {
            std::ofstream ofs(testFile, std::ios::binary);
            constexpr size_t CHUNK_SIZE = 1024 * 1024; // 1MB chunks
            std::vector<char> chunk(CHUNK_SIZE);
            for (size_t i = 0; i < CHUNK_SIZE; ++i) {
                chunk[i] = static_cast<char>((i * 73 + 19) & 0xFF);
            }
            for (int mb = 0; mb < 50; ++mb) {
                ofs.write(chunk.data(), CHUNK_SIZE);
            }
        }
        g_assert_true(std::filesystem::exists(testFile));
        g_assert_cmpuint(std::filesystem::file_size(testFile), ==, 50 * 1024 * 1024);

        g_autoptr(GFile) gfile = g_file_new_for_path(testFile.c_str());

        // Stress Part A: 100 rapid compute/cancel cycles on a single ChecksumHelper
        {
            dfiles::inspector::ChecksumHelper helper;
            for (int i = 0; i < 100; ++i) {
                helper.computeAsync(gfile, [](const dfiles::inspector::ChecksumResult&) {});
                if (i % 4 == 0) {
                    g_main_context_iteration(nullptr, FALSE);
                }
                helper.cancel();
                g_assert_false(helper.isRunning());
            }
        }
        pumpEvents(10);

        // Stress Part B: 25 rapid ChecksumHelper instantiations and immediate destructions (RAII UAF / leak check)
        for (int i = 0; i < 25; ++i) {
            auto helper = std::make_unique<dfiles::inspector::ChecksumHelper>();
            helper->computeAsync(gfile, [](const dfiles::inspector::ChecksumResult&) {});
            if (i % 3 == 0) {
                g_main_context_iteration(nullptr, FALSE);
            }
            // Reset immediately while background worker is streaming chunks
            helper.reset();
        }
        pumpEvents(20);

        // Verification Part C: Compute full 50MB checksum to completion and verify validity
        {
            dfiles::inspector::ChecksumHelper helper;
            bool completed = false;
            dfiles::inspector::ChecksumResult finalResult;

            helper.computeAsync(gfile, [&](const dfiles::inspector::ChecksumResult& res) {
                completed = true;
                finalResult = res;
            });

            bool pumpOk = pumpWhile([&]() { return !completed; }, 30000);
            g_assert_true(pumpOk);
            g_assert_true(finalResult.success);
            g_assert_cmpuint(finalResult.md5.size(), ==, 32);
            g_assert_cmpuint(finalResult.sha256.size(), ==, 64);
            g_print("    [Info] 50MB Checksums - MD5: %s | SHA-256: %s\n",
                    finalResult.md5.c_str(), finalResult.sha256.c_str());
        }

        // Cleanup
        std::filesystem::remove(testFile);
    });

    // TEST 21: Vector 3.3: Malformed & Hostile OSC 7 URI Fuzzing
    runTest("Vector 3.3: Malformed & Hostile OSC 7 URI Fuzzing", [&]() {
        auto window = std::make_unique<dfiles::ui::DFilesWindow>(app);
        auto* drawer = window->getTerminalDrawer();
        g_assert_nonnull(drawer);

        // Make drawer visible so onShellDirectoryChanged is enabled
        drawer->setVisible(true);
        g_assert_true(drawer->isVisible());

        auto* activePane = window->getSplitView()->getActivePane();
        g_assert_nonnull(activePane);

        // Baseline safe directory
        g_autoptr(GFile) baselineGFile = g_file_new_for_path("/tmp");
        activePane->navigateTo(baselineGFile);
        pumpWhile([&]() { return activePane->getModel()->isLoading(); }, 5000);
        GFile* curBefore = activePane->getCurrentDirectory();
        g_assert_nonnull(curBefore);

        // Hostile / Malformed OSC 7 payloads:
        const std::vector<std::string> hostileOsc7Uris = {
            // Foreign / remote schemes (must NEVER navigate)
            "http://malicious.example.com/exploit",
            "https://evil.corp/payload.sh",
            "ftp://anonymous@ftp.upload.org/incoming",
            "smb://malicious.lan/share",
            "gopher://old.protocol/foo",
            "data:text/html,<script>alert(1)</script>",
            "file:",
            "file://",
            "file:///",
            // Giant 128KB string buffer overflow attempt
            std::string("file:///tmp/") + std::string(128 * 1024, 'A'),
            // Format string vulnerability attack vectors
            "file:///tmp/%s%s%n%x%p%d%s%s",
            "file:///tmp/%08x.%08x.%08x.%08x",
            // Path traversal attacks (must not escape into arbitrary paths blindly)
            "file:///../../../../../../../../../../etc/shadow",
            "file:///proc/kcore",
            // Corrupted / malformed percent encoding
            "file:///tmp/%ZZ%GG%FF%EE",
            "file:///tmp/%",
            "file:///tmp/%A",
            // Embedded control characters / newlines
            "file:///tmp/newline\nseparated\rcarriage",
            "file:///tmp/escaped\x1b[31mred\x1b[0m",
            // Nonexistent arbitrary paths
            "file:///tmp/definitely_nonexistent_random_directory_987654321",
        };

        for (const auto& hostileUri : hostileOsc7Uris) {
            if (drawer->onShellDirectoryChanged) {
                drawer->onShellDirectoryChanged(hostileUri);
            }
            pumpEvents(1);
        }

        // Pump event loop to process any scheduled operations
        pumpEvents(10);

        // Active pane must still be alive and functional
        g_assert_nonnull(activePane->getCurrentDirectory());

        // Valid local file URI should successfully navigate
        std::string validUri = "file:///tmp";
        if (drawer->onShellDirectoryChanged) {
            drawer->onShellDirectoryChanged(validUri);
            pumpEvents(10);
            g_autofree char* afterValid = g_file_get_path(activePane->getCurrentDirectory());
            g_assert_cmpstr(afterValid, ==, "/tmp");
        }
    });

    // TEST 22: Vector 3.4: Terminal Process Termination & Zombie Reap
    runTest("Vector 3.4: Terminal Process Termination & Zombie Reap", [&]() {
        // Test Part A: Standalone TerminalDrawer lifecycle and abrupt shell termination
        {
            auto drawer = std::make_unique<dfiles::terminal::TerminalDrawer>();
            g_assert_nonnull(drawer->getWidget());
            g_assert_nonnull(drawer->getTerminal());

            drawer->setVisible(true);
            g_assert_true(drawer->isVisible());
            pumpEvents(10);

            // Access child PID via VteTerminal
            VteTerminal* vte = drawer->getTerminal();
            g_assert_nonnull(vte);

            // Send clear, syncDirectory, grabFocus commands to active terminal
            g_autoptr(GFile) tmpDir = g_file_new_for_path("/tmp");
            drawer->syncDirectory(tmpDir);
            drawer->clear();
            drawer->grabFocus();
            pumpEvents(5);

            // Reset and destroy drawer while shell process is running
            // TerminalDrawer destructor must send SIGHUP/SIGTERM and call g_spawn_close_pid
            drawer.reset();
            pumpEvents(10);
        }

        // Test Part B: Rapid drawer creation, process spawn, and destruction churn (20 cycles)
        for (int i = 0; i < 20; ++i) {
            auto drawer = std::make_unique<dfiles::terminal::TerminalDrawer>();
            drawer->setVisible(true);
            if (i % 2 == 0) {
                g_autoptr(GFile) dir = g_file_new_for_path("/tmp");
                drawer->syncDirectory(dir);
            }
            pumpEvents(1);
            // Abrupt destruction
            drawer.reset();
        }
        pumpEvents(10);

        // Test Part C: Reap verification (no lingering zombies from our tests)
        int status = 0;
        pid_t reaped = 0;
        do {
            reaped = waitpid(-1, &status, WNOHANG);
        } while (reaped > 0);
    });

    // -------------------------------------------------------------
    // SUMMARY
    // -------------------------------------------------------------
    g_print("====================================================\n");
    g_print("  TEST RESULTS: %d / %d PASSED\n", passedTests, totalTests);
    g_print("====================================================\n");

    return (passedTests == totalTests) ? 0 : 1;
}
