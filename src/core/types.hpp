#pragma once

#include <cstdint>
#include <string_view>

namespace dfiles::core {

/**
 * @brief View layout mode for file browser panes.
 */
enum class ViewMode {
    List,
    Grid
};

/**
 * @brief Directory entry sorting column criteria.
 */
enum class SortColumn {
    Name,
    Size,
    ModifiedTime,
    Type
};

/**
 * @brief Direction of directory sorting.
 */
enum class SortOrder {
    Ascending,
    Descending
};

/**
 * @brief Convert ViewMode enum to string representation.
 */
[[nodiscard]] constexpr std::string_view viewModeToString(ViewMode mode) noexcept {
    switch (mode) {
        case ViewMode::List:
            return "List";
        case ViewMode::Grid:
            return "Grid";
    }
    return "Unknown";
}

/**
 * @brief Convert SortColumn enum to string representation.
 */
[[nodiscard]] constexpr std::string_view sortColumnToString(SortColumn col) noexcept {
    switch (col) {
        case SortColumn::Name:
            return "Name";
        case SortColumn::Size:
            return "Size";
        case SortColumn::ModifiedTime:
            return "ModifiedTime";
        case SortColumn::Type:
            return "Type";
    }
    return "Unknown";
}

/**
 * @brief Convert SortOrder enum to string representation.
 */
[[nodiscard]] constexpr std::string_view sortOrderToString(SortOrder order) noexcept {
    switch (order) {
        case SortOrder::Ascending:
            return "Ascending";
        case SortOrder::Descending:
            return "Descending";
    }
    return "Unknown";
}

} // namespace dfiles::core
