#pragma once

#include <gio/gio.h>
#include <glib-object.h>

#include <memory>
#include <string>

G_BEGIN_DECLS

#define DFILES_TYPE_FILE_ITEM (dfiles_file_item_get_type())
G_DECLARE_FINAL_TYPE(DfilesFileItem, dfiles_file_item, DFILES, FILE_ITEM, GObject)

/**
 * @brief Constructs a new DfilesFileItem GObject.
 * @param file GFile instance representing the file path.
 * @param info GFileInfo instance containing file attributes.
 * @return New DfilesFileItem with initial reference count of 1.
 */
DfilesFileItem* dfiles_file_item_new(GFile* file, GFileInfo* info);

// C API Accessors
const char* dfiles_file_item_get_name(DfilesFileItem* item);
const char* dfiles_file_item_get_display_name(DfilesFileItem* item);
const char* dfiles_file_item_get_path(DfilesFileItem* item);
const char* dfiles_file_item_get_uri(DfilesFileItem* item);
const char* dfiles_file_item_get_mime_type(DfilesFileItem* item);
uint64_t dfiles_file_item_get_size(DfilesFileItem* item);
const char* dfiles_file_item_get_formatted_size(DfilesFileItem* item);
int64_t dfiles_file_item_get_modified_time(DfilesFileItem* item);
const char* dfiles_file_item_get_formatted_time(DfilesFileItem* item);
uint32_t dfiles_file_item_get_unix_mode(DfilesFileItem* item);
const char* dfiles_file_item_get_permissions_string(DfilesFileItem* item);
gboolean dfiles_file_item_is_directory(DfilesFileItem* item);
gboolean dfiles_file_item_is_hidden(DfilesFileItem* item);
gboolean dfiles_file_item_is_symlink(DfilesFileItem* item);
GIcon* dfiles_file_item_get_icon(DfilesFileItem* item);
GFile* dfiles_file_item_get_gfile(DfilesFileItem* item);
GFileInfo* dfiles_file_item_get_file_info(DfilesFileItem* item);

G_END_DECLS

namespace dfiles::model {

/**
 * @brief C++ RAII wrapper class representing a directory entry.
 *
 * Wraps the underlying DfilesFileItem GObject, providing high-level C++20 accessors,
 * formatted metadata helpers, and seamless compatibility with GTK4 list models.
 */
class FileItem {
public:
    explicit FileItem(GFile* file, GFileInfo* info);
    explicit FileItem(DfilesFileItem* item, bool takeRef = false);
    ~FileItem();

    FileItem(const FileItem& other);
    FileItem& operator=(const FileItem& other);
    FileItem(FileItem&& other) noexcept;
    FileItem& operator=(FileItem&& other) noexcept;

    [[nodiscard]] std::string getName() const;
    [[nodiscard]] std::string getDisplayName() const;
    [[nodiscard]] std::string getPath() const;
    [[nodiscard]] std::string getUri() const;
    [[nodiscard]] std::string getMimeType() const;
    [[nodiscard]] uint64_t getSize() const;
    [[nodiscard]] std::string getFormattedSize() const;
    [[nodiscard]] int64_t getModifiedTime() const;
    [[nodiscard]] std::string getFormattedTime() const;
    [[nodiscard]] uint32_t getUnixMode() const;
    [[nodiscard]] std::string getPermissionsString() const;
    [[nodiscard]] bool isDirectory() const;
    [[nodiscard]] bool isHidden() const;
    [[nodiscard]] bool isSymlink() const;
    [[nodiscard]] GIcon* getIcon() const;
    [[nodiscard]] GFile* getGFile() const;
    [[nodiscard]] GFileInfo* getGFileInfo() const;
    [[nodiscard]] DfilesFileItem* getGObject() const noexcept;

private:
    DfilesFileItem* m_item{nullptr};
};

} // namespace dfiles::model
