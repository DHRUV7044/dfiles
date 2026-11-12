#include "model/file_item.hpp"

#include <sys/stat.h>
#include <cstring>

struct _DfilesFileItem {
    GObject parent_instance;
    GFile* file;
    GFileInfo* info;
    char* name;
    char* display_name;
    char* path;
    char* uri;
    char* mime_type;
    char* formatted_size;
    char* formatted_time;
    char* permissions_str;
    GIcon* icon;
    uint64_t size;
    int64_t modified_time;
    uint32_t unix_mode;
    gboolean is_directory;
    gboolean is_hidden;
    gboolean is_symlink;
};

G_DEFINE_FINAL_TYPE(DfilesFileItem, dfiles_file_item, G_TYPE_OBJECT)

static void dfiles_file_item_dispose(GObject* object) {
    DfilesFileItem* self = DFILES_FILE_ITEM(object);

    g_clear_object(&self->file);
    g_clear_object(&self->info);
    g_clear_object(&self->icon);

    G_OBJECT_CLASS(dfiles_file_item_parent_class)->dispose(object);
}

static void dfiles_file_item_finalize(GObject* object) {
    DfilesFileItem* self = DFILES_FILE_ITEM(object);

    g_free(self->name);
    g_free(self->display_name);
    g_free(self->path);
    g_free(self->uri);
    g_free(self->mime_type);
    g_free(self->formatted_size);
    g_free(self->formatted_time);
    g_free(self->permissions_str);

    G_OBJECT_CLASS(dfiles_file_item_parent_class)->finalize(object);
}

static void dfiles_file_item_class_init(DfilesFileItemClass* klass) {
    GObjectClass* object_class = G_OBJECT_CLASS(klass);
    object_class->dispose = dfiles_file_item_dispose;
    object_class->finalize = dfiles_file_item_finalize;
}

static void dfiles_file_item_init(DfilesFileItem* self) {
    self->file = nullptr;
    self->info = nullptr;
    self->name = nullptr;
    self->display_name = nullptr;
    self->path = nullptr;
    self->uri = nullptr;
    self->mime_type = nullptr;
    self->formatted_size = nullptr;
    self->formatted_time = nullptr;
    self->permissions_str = nullptr;
    self->icon = nullptr;
    self->size = 0;
    self->modified_time = 0;
    self->unix_mode = 0;
    self->is_directory = FALSE;
    self->is_hidden = FALSE;
    self->is_symlink = FALSE;
}

static char* format_unix_permissions(uint32_t mode) {
    if (mode == 0) {
        return g_strdup("---------");
    }
    char p[10];
    p[0] = (mode & S_IRUSR) ? 'r' : '-';
    p[1] = (mode & S_IWUSR) ? 'w' : '-';
    p[2] = (mode & S_ISUID) ? ((mode & S_IXUSR) ? 's' : 'S') : ((mode & S_IXUSR) ? 'x' : '-');
    p[3] = (mode & S_IRGRP) ? 'r' : '-';
    p[4] = (mode & S_IWGRP) ? 'w' : '-';
    p[5] = (mode & S_ISGID) ? ((mode & S_IXGRP) ? 's' : 'S') : ((mode & S_IXGRP) ? 'x' : '-');
    p[6] = (mode & S_IROTH) ? 'r' : '-';
    p[7] = (mode & S_IWOTH) ? 'w' : '-';
    p[8] = (mode & S_ISVTX) ? ((mode & S_IXOTH) ? 't' : 'T') : ((mode & S_IXOTH) ? 'x' : '-');
    p[9] = '\0';
    return g_strdup(p);
}

DfilesFileItem* dfiles_file_item_new(GFile* file, GFileInfo* info) {
    g_return_val_if_fail(G_IS_FILE(file), nullptr);
    g_return_val_if_fail(G_IS_FILE_INFO(info), nullptr);

    auto* self = DFILES_FILE_ITEM(g_object_new(DFILES_TYPE_FILE_ITEM, nullptr));
    self->file = G_FILE(g_object_ref(file));
    self->info = G_FILE_INFO(g_object_ref(info));

    // Name & Display Name
    const char* raw_name = g_file_info_get_name(info);
    self->name = raw_name ? g_strdup(raw_name) : g_strdup("");

    const char* raw_disp = g_file_info_get_display_name(info);
    self->display_name = raw_disp ? g_strdup(raw_disp) : (raw_name ? g_strdup(raw_name) : g_strdup(""));

    // Path & URI
    char* raw_path = g_file_get_path(file);
    self->path = raw_path ? raw_path : g_file_get_uri(file);
    self->uri = g_file_get_uri(file);

    // MIME Content Type
    const char* raw_mime = g_file_info_get_content_type(info);
    self->mime_type = raw_mime ? g_strdup(raw_mime) : g_strdup("application/octet-stream");

    // File Type flags
    self->is_directory = (g_file_info_get_file_type(info) == G_FILE_TYPE_DIRECTORY);
    self->is_hidden = g_file_info_get_is_hidden(info);
    self->is_symlink = g_file_info_get_is_symlink(info);

    // Size
    self->size = g_file_info_get_size(info);
    if (self->is_directory) {
        self->formatted_size = g_strdup("--");
    } else {
        self->formatted_size = g_format_size(self->size);
    }

    // Modification Time
    g_autoptr(GDateTime) dt = g_file_info_get_modification_date_time(info);
    if (dt) {
        self->modified_time = g_date_time_to_unix(dt);
        self->formatted_time = g_date_time_format(dt, "%Y-%m-%d %H:%M");
    } else {
        self->modified_time = 0;
        self->formatted_time = g_strdup("--");
    }

    // UNIX Permissions Mode
    self->unix_mode = g_file_info_get_attribute_uint32(info, G_FILE_ATTRIBUTE_UNIX_MODE);
    self->permissions_str = format_unix_permissions(self->unix_mode);

    // Icon
    GIcon* raw_icon = g_file_info_get_icon(info);
    if (raw_icon) {
        self->icon = G_ICON(g_object_ref(raw_icon));
    } else {
        if (self->is_directory) {
            self->icon = g_themed_icon_new("folder");
        } else {
            self->icon = g_themed_icon_new("text-x-generic");
        }
    }

    return self;
}

const char* dfiles_file_item_get_name(DfilesFileItem* item) {
    g_return_val_if_fail(DFILES_IS_FILE_ITEM(item), "");
    return item->name ? item->name : "";
}

const char* dfiles_file_item_get_display_name(DfilesFileItem* item) {
    g_return_val_if_fail(DFILES_IS_FILE_ITEM(item), "");
    return item->display_name ? item->display_name : "";
}

const char* dfiles_file_item_get_path(DfilesFileItem* item) {
    g_return_val_if_fail(DFILES_IS_FILE_ITEM(item), "");
    return item->path ? item->path : "";
}

const char* dfiles_file_item_get_uri(DfilesFileItem* item) {
    g_return_val_if_fail(DFILES_IS_FILE_ITEM(item), "");
    return item->uri ? item->uri : "";
}

const char* dfiles_file_item_get_mime_type(DfilesFileItem* item) {
    g_return_val_if_fail(DFILES_IS_FILE_ITEM(item), "");
    return item->mime_type ? item->mime_type : "";
}

uint64_t dfiles_file_item_get_size(DfilesFileItem* item) {
    g_return_val_if_fail(DFILES_IS_FILE_ITEM(item), 0);
    return item->size;
}

const char* dfiles_file_item_get_formatted_size(DfilesFileItem* item) {
    g_return_val_if_fail(DFILES_IS_FILE_ITEM(item), "");
    return item->formatted_size ? item->formatted_size : "";
}

int64_t dfiles_file_item_get_modified_time(DfilesFileItem* item) {
    g_return_val_if_fail(DFILES_IS_FILE_ITEM(item), 0);
    return item->modified_time;
}

const char* dfiles_file_item_get_formatted_time(DfilesFileItem* item) {
    g_return_val_if_fail(DFILES_IS_FILE_ITEM(item), "");
    return item->formatted_time ? item->formatted_time : "";
}

uint32_t dfiles_file_item_get_unix_mode(DfilesFileItem* item) {
    g_return_val_if_fail(DFILES_IS_FILE_ITEM(item), 0);
    return item->unix_mode;
}

const char* dfiles_file_item_get_permissions_string(DfilesFileItem* item) {
    g_return_val_if_fail(DFILES_IS_FILE_ITEM(item), "");
    return item->permissions_str ? item->permissions_str : "";
}

gboolean dfiles_file_item_is_directory(DfilesFileItem* item) {
    g_return_val_if_fail(DFILES_IS_FILE_ITEM(item), FALSE);
    return item->is_directory;
}

gboolean dfiles_file_item_is_hidden(DfilesFileItem* item) {
    g_return_val_if_fail(DFILES_IS_FILE_ITEM(item), FALSE);
    return item->is_hidden;
}

gboolean dfiles_file_item_is_symlink(DfilesFileItem* item) {
    g_return_val_if_fail(DFILES_IS_FILE_ITEM(item), FALSE);
    return item->is_symlink;
}

GIcon* dfiles_file_item_get_icon(DfilesFileItem* item) {
    g_return_val_if_fail(DFILES_IS_FILE_ITEM(item), nullptr);
    return item->icon;
}

GFile* dfiles_file_item_get_gfile(DfilesFileItem* item) {
    g_return_val_if_fail(DFILES_IS_FILE_ITEM(item), nullptr);
    return item->file;
}

GFileInfo* dfiles_file_item_get_file_info(DfilesFileItem* item) {
    g_return_val_if_fail(DFILES_IS_FILE_ITEM(item), nullptr);
    return item->info;
}

namespace dfiles::model {

FileItem::FileItem(GFile* file, GFileInfo* info)
    : m_item(dfiles_file_item_new(file, info)) {}

FileItem::FileItem(DfilesFileItem* item, bool takeRef) {
    if (item) {
        m_item = takeRef ? item : DFILES_FILE_ITEM(g_object_ref(item));
    }
}

FileItem::~FileItem() {
    if (m_item) {
        g_clear_object(&m_item);
    }
}

FileItem::FileItem(const FileItem& other)
    : m_item(other.m_item ? DFILES_FILE_ITEM(g_object_ref(other.m_item)) : nullptr) {}

FileItem& FileItem::operator=(const FileItem& other) {
    if (this != &other) {
        if (m_item) {
            g_clear_object(&m_item);
        }
        if (other.m_item) {
            m_item = DFILES_FILE_ITEM(g_object_ref(other.m_item));
        }
    }
    return *this;
}

FileItem::FileItem(FileItem&& other) noexcept
    : m_item(other.m_item) {
    other.m_item = nullptr;
}

FileItem& FileItem::operator=(FileItem&& other) noexcept {
    if (this != &other) {
        if (m_item) {
            g_clear_object(&m_item);
        }
        m_item = other.m_item;
        other.m_item = nullptr;
    }
    return *this;
}

std::string FileItem::getName() const {
    return m_item ? dfiles_file_item_get_name(m_item) : "";
}

std::string FileItem::getDisplayName() const {
    return m_item ? dfiles_file_item_get_display_name(m_item) : "";
}

std::string FileItem::getPath() const {
    return m_item ? dfiles_file_item_get_path(m_item) : "";
}

std::string FileItem::getUri() const {
    return m_item ? dfiles_file_item_get_uri(m_item) : "";
}

std::string FileItem::getMimeType() const {
    return m_item ? dfiles_file_item_get_mime_type(m_item) : "";
}

uint64_t FileItem::getSize() const {
    return m_item ? dfiles_file_item_get_size(m_item) : 0;
}

std::string FileItem::getFormattedSize() const {
    return m_item ? dfiles_file_item_get_formatted_size(m_item) : "";
}

int64_t FileItem::getModifiedTime() const {
    return m_item ? dfiles_file_item_get_modified_time(m_item) : 0;
}

std::string FileItem::getFormattedTime() const {
    return m_item ? dfiles_file_item_get_formatted_time(m_item) : "";
}

uint32_t FileItem::getUnixMode() const {
    return m_item ? dfiles_file_item_get_unix_mode(m_item) : 0;
}

std::string FileItem::getPermissionsString() const {
    return m_item ? dfiles_file_item_get_permissions_string(m_item) : "";
}

bool FileItem::isDirectory() const {
    return m_item && dfiles_file_item_is_directory(m_item);
}

bool FileItem::isHidden() const {
    return m_item && dfiles_file_item_is_hidden(m_item);
}

bool FileItem::isSymlink() const {
    return m_item && dfiles_file_item_is_symlink(m_item);
}

GIcon* FileItem::getIcon() const {
    return m_item ? dfiles_file_item_get_icon(m_item) : nullptr;
}

GFile* FileItem::getGFile() const {
    return m_item ? dfiles_file_item_get_gfile(m_item) : nullptr;
}

GFileInfo* FileItem::getGFileInfo() const {
    return m_item ? dfiles_file_item_get_file_info(m_item) : nullptr;
}

DfilesFileItem* FileItem::getGObject() const noexcept {
    return m_item;
}

} // namespace dfiles::model
