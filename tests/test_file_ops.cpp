#include "operations/file_ops.hpp"

#include <adwaita.h>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

namespace fs = std::filesystem;

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

void pumpMainLoop(int iterations = 10) {
    for (int i = 0; i < iterations; ++i) {
        g_main_context_iteration(nullptr, FALSE);
    }
}

void pumpMainLoopUntil(std::function<bool()> condition, int timeoutMs = 5000) {
    auto start = std::chrono::steady_clock::now();
    while (!condition()) {
        g_main_context_iteration(nullptr, FALSE);
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - start).count();
        if (elapsed > timeoutMs) {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
}

std::string readFileContent(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    if (!in.is_open()) return "";
    return std::string((std::istreambuf_iterator<char>(in)),
                       std::istreambuf_iterator<char>());
}

void writeFileContent(const fs::path& p, const std::string& content) {
    std::ofstream out(p, std::ios::binary);
    out << content;
}

} // anonymous namespace

int main([[maybe_unused]] int argc, [[maybe_unused]] char* argv[]) {
    adw_init();

    fs::path tempBase = fs::temp_directory_path() / ("dfiles_test_ops_" + std::to_string(std::chrono::system_clock::now().time_since_epoch().count()));
    fs::create_directories(tempBase);

    std::cout << "=========================================================" << std::endl;
    std::cout << "  DFILES PHASE 4: ASYNC FILE OPERATIONS TEST SUITE       " << std::endl;
    std::cout << "  Temp Test Root: " << tempBase.string() << std::endl;
    std::cout << "=========================================================" << std::endl;

    // -------------------------------------------------------------
    // Test 1: Collision Resolution & Naming
    // -------------------------------------------------------------
    std::cout << "\n--- Test Suite 1: Collision Resolution ---" << std::endl;
    {
        fs::path collDir = tempBase / "collisions";
        fs::create_directories(collDir);
        g_autoptr(GFile) dirGFile = g_file_new_for_path(collDir.c_str());

        // File doesn't exist yet
        std::string n1 = dfiles::operations::FileOperations::getNonCollidingBasename(dirGFile, "photo.jpg");
        testAssert(n1 == "photo.jpg", "Non-existing file returns original name", n1);

        // Create photo.jpg
        writeFileContent(collDir / "photo.jpg", "img1");
        std::string n2 = dfiles::operations::FileOperations::getNonCollidingBasename(dirGFile, "photo.jpg");
        testAssert(n2 == "photo (copy 1).jpg", "Colliding photo.jpg produces photo (copy 1).jpg", n2);

        // Create photo (copy 1).jpg
        writeFileContent(collDir / "photo (copy 1).jpg", "img2");
        std::string n3 = dfiles::operations::FileOperations::getNonCollidingBasename(dirGFile, "photo.jpg");
        testAssert(n3 == "photo (copy 2).jpg", "Colliding photo.jpg produces photo (copy 2).jpg", n3);

        // Copying an existing "photo (copy 1).jpg" directly
        std::string n3b = dfiles::operations::FileOperations::getNonCollidingBasename(dirGFile, "photo (copy 1).jpg");
        testAssert(n3b == "photo (copy 2).jpg", "Colliding copy-pattern increments cleanly", n3b);

        // Double extension archive.tar.gz
        writeFileContent(collDir / "archive.tar.gz", "tar1");
        std::string n4 = dfiles::operations::FileOperations::getNonCollidingBasename(dirGFile, "archive.tar.gz");
        testAssert(n4 == "archive (copy 1).tar.gz", "Double extension .tar.gz handles copy correctly", n4);

        writeFileContent(collDir / "archive (copy 1).tar.gz", "tar2");
        std::string n4b = dfiles::operations::FileOperations::getNonCollidingBasename(dirGFile, "archive.tar.gz");
        testAssert(n4b == "archive (copy 2).tar.gz", "Double extension copy 2 handles copy correctly", n4b);
        std::string n4c = dfiles::operations::FileOperations::getNonCollidingBasename(dirGFile, "archive (copy 1).tar.gz");
        testAssert(n4c == "archive (copy 2).tar.gz", "Double extension existing copy pattern increments cleanly", n4c);

        // Dotfile .bashrc
        writeFileContent(collDir / ".bashrc", "alias ls='ls --color'");
        std::string n5 = dfiles::operations::FileOperations::getNonCollidingBasename(dirGFile, ".bashrc");
        testAssert(n5 == ".bashrc (copy 1)", "Dotfile .bashrc handles copy correctly", n5);

        // Extensionless Makefile
        writeFileContent(collDir / "Makefile", "all:\n\ttrue");
        std::string n6 = dfiles::operations::FileOperations::getNonCollidingBasename(dirGFile, "Makefile");
        testAssert(n6 == "Makefile (copy 1)", "Extensionless file handles copy correctly", n6);

        // Trailing slash
        std::string nSlash = dfiles::operations::FileOperations::getNonCollidingBasename(dirGFile, "photo.jpg/");
        testAssert(nSlash == "photo (copy 2).jpg", "Trailing slash in photo.jpg/ resolved cleanly", nSlash);

        // Defensive checks
        std::string nNull = dfiles::operations::FileOperations::getNonCollidingBasename(nullptr, "test.txt");
        testAssert(nNull == "test.txt", "Null directory returns original name");
        std::string nEmpty = dfiles::operations::FileOperations::getNonCollidingBasename(dirGFile, "");
        testAssert(nEmpty.empty(), "Empty original name returns empty string");
    }

    // -------------------------------------------------------------
    // Test 2: Directory Creation (createDirectoryAsync)
    // -------------------------------------------------------------
    std::cout << "\n--- Test Suite 2: Create Directory Async ---" << std::endl;
    {
        dfiles::operations::FileOperations ops;
        fs::path mkdirDir = tempBase / "mkdir_test";
        fs::create_directories(mkdirDir);
        g_autoptr(GFile) parentGFile = g_file_new_for_path(mkdirDir.c_str());

        bool done = false;
        bool okResult = false;
        std::string msgResult;

        ops.createDirectoryAsync(parentGFile, "new_folder", [&](bool ok, const std::string& msg) {
            done = true;
            okResult = ok;
            msgResult = msg;
        });

        pumpMainLoopUntil([&]() { return done; });
        testAssert(done && okResult, "createDirectoryAsync succeeded", msgResult);
        testAssert(fs::is_directory(mkdirDir / "new_folder"), "new_folder exists on filesystem");

        // Attempting to create same folder again should fail
        done = false;
        okResult = true;
        ops.createDirectoryAsync(parentGFile, "new_folder", [&](bool ok, const std::string& msg) {
            done = true;
            okResult = ok;
            msgResult = msg;
        });
        pumpMainLoopUntil([&]() { return done; });
        testAssert(done && !okResult, "createDirectoryAsync fails when directory already exists", msgResult);

        // Invalid inputs
        done = false;
        ops.createDirectoryAsync(nullptr, "bad", [&](bool ok, const std::string& msg) {
            done = true;
            okResult = ok;
            msgResult = msg;
        });
        pumpMainLoopUntil([&]() { return done; });
        testAssert(done && !okResult, "createDirectoryAsync fails on null parent directory");
    }

    // -------------------------------------------------------------
    // Test 3: Rename Async (renameAsync)
    // -------------------------------------------------------------
    std::cout << "\n--- Test Suite 3: Rename Async ---" << std::endl;
    {
        dfiles::operations::FileOperations ops;
        fs::path renameDir = tempBase / "rename_test";
        fs::create_directories(renameDir);
        fs::path fileOld = renameDir / "original.txt";
        writeFileContent(fileOld, "content for rename");

        g_autoptr(GFile) origGFile = g_file_new_for_path(fileOld.c_str());

        bool done = false;
        bool okResult = false;
        std::string msgResult;

        ops.renameAsync(origGFile, "renamed.txt", [&](bool ok, const std::string& msg) {
            done = true;
            okResult = ok;
            msgResult = msg;
        });

        pumpMainLoopUntil([&]() { return done; });
        testAssert(done && okResult, "renameAsync succeeded", msgResult);
        testAssert(!fs::exists(fileOld), "Original file no longer exists");
        testAssert(fs::exists(renameDir / "renamed.txt"), "Renamed file exists on filesystem");
        testAssert(readFileContent(renameDir / "renamed.txt") == "content for rename", "Renamed file preserved content");
    }

    // -------------------------------------------------------------
    // Test 4: Copy Async (Single File, Overwrite, Collisions & Progress)
    // -------------------------------------------------------------
    std::cout << "\n--- Test Suite 4: Copy Async (Single Files & Progress) ---" << std::endl;
    {
        dfiles::operations::FileOperations ops;
        fs::path copySrcDir = tempBase / "copy_src";
        fs::path copyDstDir = tempBase / "copy_dst";
        fs::create_directories(copySrcDir);
        fs::create_directories(copyDstDir);

        fs::path srcFile = copySrcDir / "document.txt";
        std::string expectedData = "This is a document for testing copyAsync with progress updates!";
        writeFileContent(srcFile, expectedData);

        g_autoptr(GFile) srcGFile = g_file_new_for_path(srcFile.c_str());
        g_autoptr(GFile) dstDirGFile = g_file_new_for_path(copyDstDir.c_str());

        bool done = false;
        bool okResult = false;
        std::string msgResult;
        dfiles::operations::OperationProgress lastProg;
        size_t progCallbacks = 0;

        ops.copyAsync(
            {srcGFile},
            dstDirGFile,
            false,
            [&](bool ok, const std::string& msg) {
                done = true;
                okResult = ok;
                msgResult = msg;
            },
            [&](const dfiles::operations::OperationProgress& p) {
                lastProg = p;
                progCallbacks++;
            }
        );

        pumpMainLoopUntil([&]() { return done; });
        testAssert(done && okResult, "copyAsync succeeded", msgResult);
        testAssert(fs::exists(copyDstDir / "document.txt"), "Copied document.txt exists");
        testAssert(readFileContent(copyDstDir / "document.txt") == expectedData, "Copied content matches byte-for-byte");
        testAssert(progCallbacks > 0, "Progress callbacks were invoked", std::to_string(progCallbacks));
        testAssert(lastProg.currentFiles == 1 && lastProg.totalFiles == 1, "Progress files count matches 1/1");
        testAssert(lastProg.currentBytes == expectedData.size(), "Progress bytes match total file size");

        // Copy again without overwrite -> creates "document (copy 1).txt"
        done = false;
        ops.copyAsync(
            {srcGFile},
            dstDirGFile,
            false,
            [&](bool ok, const std::string& msg) {
                done = true;
                okResult = ok;
                msgResult = msg;
            }
        );
        pumpMainLoopUntil([&]() { return done; });
        testAssert(done && okResult, "copyAsync without overwrite resolves collision", msgResult);
        testAssert(fs::exists(copyDstDir / "document (copy 1).txt"), "Collision copy 'document (copy 1).txt' exists");

        // Overwrite test
        writeFileContent(srcFile, "Updated content for overwrite");
        done = false;
        ops.copyAsync(
            {srcGFile},
            dstDirGFile,
            true, // overwrite = true
            [&](bool ok, const std::string& msg) {
                done = true;
                okResult = ok;
                msgResult = msg;
            }
        );
        pumpMainLoopUntil([&]() { return done; });
        testAssert(done && okResult, "copyAsync with overwrite succeeded", msgResult);
        testAssert(readFileContent(copyDstDir / "document.txt") == "Updated content for overwrite", "Overwritten file has new content");
    }

    // -------------------------------------------------------------
    // Test 5: Recursive Directory Copy
    // -------------------------------------------------------------
    std::cout << "\n--- Test Suite 5: Recursive Directory Copy ---" << std::endl;
    {
        dfiles::operations::FileOperations ops;
        fs::path treeSrc = tempBase / "tree_source";
        fs::create_directories(treeSrc / "nested1" / "nested2");
        writeFileContent(treeSrc / "root.txt", "root file");
        writeFileContent(treeSrc / "nested1" / "mid.txt", "mid file");
        writeFileContent(treeSrc / "nested1" / "nested2" / "deep.txt", "deep file");

        // Create a symlink
        fs::create_symlink("root.txt", treeSrc / "symlink_to_root");

        fs::path treeDst = tempBase / "tree_target";
        fs::create_directories(treeDst);

        g_autoptr(GFile) srcTreeGFile = g_file_new_for_path(treeSrc.c_str());
        g_autoptr(GFile) dstDirGFile = g_file_new_for_path(treeDst.c_str());

        bool done = false;
        bool okResult = false;
        std::string msgResult;

        ops.copyAsync(
            {srcTreeGFile},
            dstDirGFile,
            false,
            [&](bool ok, const std::string& msg) {
                done = true;
                okResult = ok;
                msgResult = msg;
            }
        );

        pumpMainLoopUntil([&]() { return done; });
        testAssert(done && okResult, "Recursive directory copy succeeded", msgResult);

        fs::path copiedTree = treeDst / "tree_source";
        testAssert(fs::is_directory(copiedTree), "tree_source copied as directory");
        testAssert(fs::exists(copiedTree / "root.txt"), "tree_source/root.txt exists");
        testAssert(fs::exists(copiedTree / "nested1" / "mid.txt"), "tree_source/nested1/mid.txt exists");
        testAssert(fs::exists(copiedTree / "nested1" / "nested2" / "deep.txt"), "tree_source/nested1/nested2/deep.txt exists");
        testAssert(fs::is_symlink(copiedTree / "symlink_to_root"), "tree_source/symlink_to_root preserved as symlink");

        // Test cyclic containment guard: copy directory into itself
        g_autoptr(GFile) selfDst = g_file_new_for_path((treeSrc / "nested1").c_str());
        done = false;
        ops.copyAsync(
            {srcTreeGFile},
            selfDst,
            false,
            [&](bool ok, const std::string& msg) {
                done = true;
                okResult = ok;
                msgResult = msg;
            }
        );
        pumpMainLoopUntil([&]() { return done; });
        testAssert(done && !okResult, "Copying directory into descendant is rejected", msgResult);
    }

    // -------------------------------------------------------------
    // Test 6: Move Async (Files and Directories)
    // -------------------------------------------------------------
    std::cout << "\n--- Test Suite 6: Move Async ---" << std::endl;
    {
        dfiles::operations::FileOperations ops;
        fs::path moveSrc = tempBase / "move_source";
        fs::path moveDst = tempBase / "move_dest";
        fs::create_directories(moveSrc);
        fs::create_directories(moveDst);

        fs::path mFile = moveSrc / "move_test.txt";
        writeFileContent(mFile, "moving content");

        g_autoptr(GFile) mFileG = g_file_new_for_path(mFile.c_str());
        g_autoptr(GFile) mDstG = g_file_new_for_path(moveDst.c_str());

        bool done = false;
        bool okResult = false;
        std::string msgResult;

        ops.moveAsync({mFileG}, mDstG, false, [&](bool ok, const std::string& msg) {
            done = true;
            okResult = ok;
            msgResult = msg;
        });

        pumpMainLoopUntil([&]() { return done; });
        testAssert(done && okResult, "moveAsync file succeeded", msgResult);
        testAssert(!fs::exists(mFile), "Source file removed after move");
        testAssert(fs::exists(moveDst / "move_test.txt"), "Destination file exists after move");
        testAssert(readFileContent(moveDst / "move_test.txt") == "moving content", "Destination content preserved");

        // Moving a whole directory
        fs::path dirToMove = moveSrc / "folder_to_move";
        fs::create_directories(dirToMove / "subdir");
        writeFileContent(dirToMove / "subdir" / "data.bin", "binary 0101");

        g_autoptr(GFile) dirToMoveG = g_file_new_for_path(dirToMove.c_str());
        done = false;
        ops.moveAsync({dirToMoveG}, mDstG, false, [&](bool ok, const std::string& msg) {
            done = true;
            okResult = ok;
            msgResult = msg;
        });

        pumpMainLoopUntil([&]() { return done; });
        testAssert(done && okResult, "moveAsync directory succeeded", msgResult);
        testAssert(!fs::exists(dirToMove), "Source directory removed after move");
        testAssert(fs::is_directory(moveDst / "folder_to_move"), "Destination directory exists after move");
        testAssert(fs::exists(moveDst / "folder_to_move" / "subdir" / "data.bin"), "Subdir and file intact in moved destination");
    }

    // -------------------------------------------------------------
    // Test 7: Permanent Delete Async (deleteAsync)
    // -------------------------------------------------------------
    std::cout << "\n--- Test Suite 7: Permanent Delete Async ---" << std::endl;
    {
        dfiles::operations::FileOperations ops;
        fs::path delDir = tempBase / "delete_target";
        fs::create_directories(delDir / "sub1" / "sub2");
        writeFileContent(delDir / "f1.txt", "f1");
        writeFileContent(delDir / "sub1" / "f2.txt", "f2");
        writeFileContent(delDir / "sub1" / "sub2" / "f3.txt", "f3");

        g_autoptr(GFile) delGFile = g_file_new_for_path(delDir.c_str());

        bool done = false;
        bool okResult = false;
        std::string msgResult;

        ops.deleteAsync({delGFile}, [&](bool ok, const std::string& msg) {
            done = true;
            okResult = ok;
            msgResult = msg;
        });

        pumpMainLoopUntil([&]() { return done; });
        testAssert(done && okResult, "deleteAsync directory succeeded", msgResult);
        testAssert(!fs::exists(delDir), "Deleted directory tree is completely gone");

        // Empty delete list returns cleanly
        done = false;
        ops.deleteAsync({}, [&](bool ok, const std::string& msg) {
            done = true;
            okResult = ok;
            msgResult = msg;
        });
        pumpMainLoopUntil([&]() { return done; });
        testAssert(done && okResult, "deleteAsync with empty list succeeds immediately");
    }

    // -------------------------------------------------------------
    // Test 8: Trash Async (trashAsync)
    // -------------------------------------------------------------
    std::cout << "\n--- Test Suite 8: Trash Async ---" << std::endl;
    {
        dfiles::operations::FileOperations ops;
        fs::path userCacheDir = fs::path(g_get_user_cache_dir()) / "dfiles_trash_test";
        fs::create_directories(userCacheDir);
        fs::path trashFile = userCacheDir / "trash_target.txt";
        writeFileContent(trashFile, "trashable data");

        g_autoptr(GFile) trashGFile = g_file_new_for_path(trashFile.c_str());

        bool done = false;
        bool okResult = false;
        std::string msgResult;

        ops.trashAsync({trashGFile}, [&](bool ok, const std::string& msg) {
            done = true;
            okResult = ok;
            msgResult = msg;
        });

        pumpMainLoopUntil([&]() { return done; });
        testAssert(done && okResult, "trashAsync succeeded", msgResult);
        testAssert(!fs::exists(trashFile), "Trashed file removed from original path");
        std::error_code trashEc;
        fs::remove_all(userCacheDir, trashEc);
    }

    // -------------------------------------------------------------
    // Test 9: Clipboard Integration (Copy, Cut, Paste, canPaste)
    // -------------------------------------------------------------
    std::cout << "\n--- Test Suite 9: Clipboard Integration ---" << std::endl;
    {
        dfiles::operations::FileOperations ops;
        fs::path clipSrcDir = tempBase / "clip_source";
        fs::path clipDstDir = tempBase / "clip_target";
        fs::create_directories(clipSrcDir);
        fs::create_directories(clipDstDir);

        fs::path f1 = clipSrcDir / "clip1.txt";
        fs::path f2 = clipSrcDir / "clip2.txt";
        writeFileContent(f1, "clip1 data");
        writeFileContent(f2, "clip2 data");

        g_autoptr(GFile) g1 = g_file_new_for_path(f1.c_str());
        g_autoptr(GFile) g2 = g_file_new_for_path(f2.c_str());
        g_autoptr(GFile) dstDirG = g_file_new_for_path(clipDstDir.c_str());

        // 1. Copy to clipboard
        testAssert(!ops.canPaste(), "canPaste() initially false");
        ops.copyToClipboard({g1, g2}, false);
        testAssert(ops.canPaste(), "canPaste() true after copyToClipboard");
        testAssert(!ops.isClipboardCut(), "isClipboardCut() is false for copy");

        // Paste copy
        bool done = false;
        bool okResult = false;
        std::string msgResult;

        ops.pasteFromClipboard(dstDirG, [&](bool ok, const std::string& msg) {
            done = true;
            okResult = ok;
            msgResult = msg;
        });

        pumpMainLoopUntil([&]() { return done; });
        testAssert(done && okResult, "pasteFromClipboard copy succeeded", msgResult);
        testAssert(fs::exists(clipDstDir / "clip1.txt"), "clip1.txt exists in target");
        testAssert(fs::exists(clipDstDir / "clip2.txt"), "clip2.txt exists in target");
        testAssert(fs::exists(f1) && fs::exists(f2), "Source files still exist after copy paste");

        // 2. Cut to clipboard
        fs::path cutDstDir = tempBase / "clip_cut_target";
        fs::create_directories(cutDstDir);
        g_autoptr(GFile) cutDstDirG = g_file_new_for_path(cutDstDir.c_str());

        ops.copyToClipboard({g1}, true); // Cut
        testAssert(ops.canPaste(), "canPaste() true after cut");
        testAssert(ops.isClipboardCut(), "isClipboardCut() is true for cut");

        done = false;
        ops.pasteFromClipboard(cutDstDirG, [&](bool ok, const std::string& msg) {
            done = true;
            okResult = ok;
            msgResult = msg;
        });

        pumpMainLoopUntil([&]() { return done; });
        testAssert(done && okResult, "pasteFromClipboard cut succeeded", msgResult);
        testAssert(fs::exists(cutDstDir / "clip1.txt"), "clip1.txt moved to cut target");
        testAssert(!fs::exists(f1), "Source clip1.txt removed after cut paste");
        testAssert(!ops.isClipboardCut(), "Clipboard cut status cleared after paste");
    }

    // -------------------------------------------------------------
    // Test 10: Cancellation & Lifecycle Safety
    // -------------------------------------------------------------
    std::cout << "\n--- Test Suite 10: Cancellation & Lifecycle Safety ---" << std::endl;
    {
        fs::path stormDir = tempBase / "cancel_storm";
        fs::create_directories(stormDir / "src");
        fs::create_directories(stormDir / "dst");

        // Create 20 files
        std::vector<GFile*> srcFiles;
        for (int i = 0; i < 20; ++i) {
            fs::path p = stormDir / "src" / ("file_" + std::to_string(i) + ".dat");
            writeFileContent(p, std::string(1024 * 16, 'x'));
            srcFiles.push_back(g_file_new_for_path(p.c_str()));
        }

        g_autoptr(GFile) dstG = g_file_new_for_path((stormDir / "dst").c_str());

        // Test cancelCurrent()
        {
            auto ops = std::make_unique<dfiles::operations::FileOperations>();
            bool completed = false;
            ops->copyAsync(srcFiles, dstG, false, [&](bool, const std::string&) {
                completed = true;
            });

            ops->cancelCurrent();
            testAssert(!ops->isRunning(), "FileOperations::isRunning() is false after cancelCurrent()");
            pumpMainLoop(5);
        }
        testAssert(true, "Destruction of FileOperations immediately after cancel completes safely");

        // Clean up GFile references
        for (auto* f : srcFiles) {
            g_object_unref(f);
        }
    }

    // Cleanup temp directory
    std::error_code ec;
    fs::remove_all(tempBase, ec);

    std::cout << "\n=========================================================" << std::endl;
    std::cout << "  FILE OPERATIONS TEST RESULTS SUMMARY                   " << std::endl;
    std::cout << "  TOTAL TESTS PASSED : " << g_passCount << std::endl;
    std::cout << "  TOTAL TESTS FAILED : " << g_failCount << std::endl;
    std::cout << "=========================================================" << std::endl;

    return (g_failCount == 0) ? 0 : 1;
}
