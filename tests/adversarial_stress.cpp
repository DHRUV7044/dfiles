#include "core/app.hpp"
#include "ui/window.hpp"
#include "ui/headerbar.hpp"
#include "ui/sidebar.hpp"

#include <adwaita.h>
#include <gtk/gtk.h>
#include <gdk/gdkkeysyms.h>

#include <cassert>
#include <chrono>
#include <iostream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>
#include <atomic>

namespace {

void pumpEvents(int iterations = 10) {
    for (int i = 0; i < iterations; ++i) {
        while (g_main_context_iteration(nullptr, FALSE)) {
            // Process all pending events
        }
    }
}



} // namespace

int main(int /*argc*/, char* /*argv*/[]) {
    g_print("====================================================\n");
    g_print("  DFILES PHASE 1: ADVERSARIAL STRESS TEST SUITE    \n");
    g_print("====================================================\n\n");

    // Initialize Libadwaita / GTK
    adw_init();

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
    // SUMMARY
    // -------------------------------------------------------------
    g_print("====================================================\n");
    g_print("  TEST RESULTS: %d / %d PASSED\n", passedTests, totalTests);
    g_print("====================================================\n");

    return (passedTests == totalTests) ? 0 : 1;
}
