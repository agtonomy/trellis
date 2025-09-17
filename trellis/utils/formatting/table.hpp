/*
 * Copyright (C) 2025 Agtonomy
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

/**
 * @file table.hpp
 * @brief A templated table formatting utility for creating bordered tables with automatic column width calculation.
 * @details This file provides a Table class template that can format data into bordered tables with proper
 *          column alignment and padding. The table automatically calculates column widths based on content.
 */

#ifndef TRELLIS_UTILS_FORMATTING_TABLE_HPP_
#define TRELLIS_UTILS_FORMATTING_TABLE_HPP_

#include <algorithm>
#include <array>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <tuple>
#include <vector>

namespace trellis::utils::formatting {

/**
 * @brief A templated table class for formatting tabular data with borders and automatic column sizing.
 * @tparam Columns The types of the columns in the table (variadic template parameters).
 *
 * @details This class provides functionality to create formatted tables with:
 *          - Automatic column width calculation based on content
 *          - Bordered output with proper alignment
 *          - Support for mixed data types in columns
 *          - Header row with centered text
 *          - Data rows with left-aligned content
 *
 * Example usage:
 * @code
 * Table<std::string, int, double> table({"Name", "Age", "Score"});
 * table.AddRow("Alice", 25, 95.5);
 * table.AddRow("Bob", 30, 87.2);
 * table.Print(std::cout);
 * @endcode
 */
template <typename... Columns>
class Table {
 public:
  /// @brief Type alias for the array of column names
  using RowStringArray = std::array<std::string, sizeof...(Columns)>;
  /// @brief Type alias for a single row of data as a tuple
  using Row = std::tuple<Columns...>;
  /// @brief Type alias for a vector containing all rows
  using RowVector = std::vector<Row>;
  /// @brief Type alias for the array storing calculated column widths
  using ColumnWidths = std::array<size_t, sizeof...(Columns)>;

  /**
   * @brief Constructs a new Table object with specified column names.
   * @param column_names Array of strings containing the column header names.
   *
   * @details The number of column names must match the number of template parameters.
   *          Column names will be centered in the header row.
   */
  Table(RowStringArray column_names) : column_names_(column_names) {}

  /**
   * @brief Adds a new row of data to the table.
   * @param columns Variadic arguments representing the data for each column in the row.
   *
   * @details The types and number of arguments must match the template parameters.
   *          Data will be converted to strings when printed and left-aligned in cells.
   */
  void AddRow(Columns... columns) { data_.emplace_back(std::make_tuple(columns...)); }

  /**
   * @brief Prints the complete table with borders, headers, and all data rows.
   * @param os Output stream to write the formatted table to.
   *
   * @details Outputs a fully formatted table including:
   *          - Top border
   *          - Header row with column names (centered)
   *          - Separator border
   *          - All data rows (left-aligned)
   *          - Bottom border
   */
  void Print(std::ostream& os) const {
    const auto widths = CalculateColumnWidths();
    PrintBorder(os, widths);
    PrintHeader(os, widths);
    PrintBorder(os, widths);
    for (const auto& row : data_) {
      PrintRow(os, row, widths);
    }
    PrintBorder(os, widths);
  }

  /**
   * @brief Prints a single row of data without table borders or headers.
   * @param os Output stream to write the formatted row to.
   * @param row The row data as a tuple to be printed.
   *
   * @details Prints only the data row with proper column alignment and separators.
   *          Column widths are calculated based on all existing table data.
   *          Useful for printing individual rows or custom table formats.
   */
  void PrintRow(std::ostream& os, const Row& row) const {
    const auto widths = CalculateColumnWidths();
    PrintRow(os, row, widths);
  }

 private:
  /**
   * @brief Converts any value to a string representation for table display.
   * @tparam T The type of the value to convert.
   * @param value The value to convert to string.
   * @return String representation of the value.
   *
   * @details Uses std::ostringstream to handle type conversion automatically.
   *          This allows the table to work with any streamable type.
   */
  template <typename T>
  std::string ToString(const T& value) const {
    std::ostringstream oss;
    oss << value;
    return oss.str();
  }

  /**
   * @brief Recursively converts all elements in a row tuple to strings.
   * @tparam Index Current column index being processed (compile-time recursion).
   * @param row The row tuple containing the data.
   * @param strings Array to store the converted string representations.
   *
   * @details Template recursion processes each tuple element at compile time,
   *          converting each to a string and storing in the corresponding array position.
   */
  template <size_t Index>
  void GetRowStrings(const Row& row, RowStringArray& strings) const {
    if constexpr (Index < std::tuple_size_v<Row>) {
      strings[Index] = ToString(std::get<Index>(row));
      GetRowStrings<Index + 1>(row, strings);
    }
  }

  /**
   * @brief Recursively updates column width calculations for a single row.
   * @tparam Index Current column index being processed (compile-time recursion).
   * @param row The row data to measure.
   * @param widths Array of current maximum widths, updated in-place.
   *
   * @details Converts each column value to string and updates the maximum width
   *          for that column if the current value is longer.
   */
  template <size_t Index>
  void UpdateColumnWidths(const Row& row, ColumnWidths& widths) const {
    if constexpr (Index < std::tuple_size_v<Row>) {
      const std::string str = ToString(std::get<Index>(row));
      widths[Index] = std::max(widths[Index], str.length());
      UpdateColumnWidths<Index + 1>(row, widths);
    }
  }

  /**
   * @brief Calculates the optimal width for each column based on content and headers.
   * @return Array containing the calculated width for each column.
   *
   * @details Algorithm:
   *          1. Initialize widths with header text lengths
   *          2. Scan all data rows to find maximum content width per column
   *          3. Add padding (4 characters: 2 spaces on each side)
   *          This ensures all content fits with proper visual spacing.
   */
  ColumnWidths CalculateColumnWidths() const {
    ColumnWidths widths{};

    // Initialize with header widths
    for (size_t i = 0; i < column_names_.size(); ++i) {
      widths[i] = column_names_[i].length();
    }

    // Update with data widths
    for (const auto& row : data_) {
      UpdateColumnWidths<0>(row, widths);
    }

    // Add minimum padding (2 spaces on each side)
    for (size_t i = 0; i < widths.size(); ++i) {
      widths[i] += 4;
    }

    return widths;
  }

  /**
   * @brief Prints a horizontal border line across all columns.
   * @param os Output stream to write the border to.
   * @param widths Array of column widths to determine border length.
   *
   * @details Creates a continuous line of dashes spanning the width of each column
   *          plus one additional character to account for column separators.
   *          Used for top, middle, and bottom borders of the table.
   */
  void PrintBorder(std::ostream& os, const ColumnWidths& widths) const {
    for (size_t i = 0; i < widths.size(); ++i) {
      for (size_t j = 0; j < widths[i] + 1; ++j) {  // +1 for the | character
        os << "-";
      }
    }
    os << std::endl;
  }

  /**
   * @brief Prints the table header row with column names centered in each cell.
   * @param os Output stream to write the header to.
   * @param widths Array of column widths for proper alignment.
   *
   * @details Centers each column name within its calculated width by distributing
   *          padding equally on both sides. If padding is odd, the extra space
   *          goes to the right side.
   */
  void PrintHeader(std::ostream& os, const ColumnWidths& widths) const {
    os << "|";
    for (size_t i = 0; i < column_names_.size(); ++i) {
      const std::string content = column_names_[i];
      const size_t padding = widths[i] - content.length();
      const size_t left_pad = padding / 2;
      const size_t right_pad = padding - left_pad;

      for (size_t j = 0; j < left_pad; ++j) os << " ";
      os << content;
      for (size_t j = 0; j < right_pad; ++j) os << " ";
      os << "|";
    }
    os << std::endl;
  }

  /**
   * @brief Prints a single data row with left-aligned content in each cell.
   * @param os Output stream to write the row to.
   * @param row The row data as a tuple.
   * @param widths Array of column widths for proper alignment.
   *
   * @details Left-aligns content within each cell and pads with spaces to fill
   *          the calculated column width. This is the internal implementation
   *          used by both public Print methods.
   */
  void PrintRow(std::ostream& os, const Row& row, const ColumnWidths& widths) const {
    RowStringArray strings{};
    GetRowStrings<0>(row, strings);

    os << "|";
    for (size_t i = 0; i < strings.size(); ++i) {
      const std::string content = strings[i];
      const size_t padding = widths[i] - content.length();

      os << " " << content;
      for (size_t j = 0; j < padding - 1; ++j) os << " ";
      os << "|";
    }
    os << std::endl;
  }

 private:
  /// @brief Array storing the column header names for the table
  RowStringArray column_names_;
  /// @brief Vector containing all the data rows added to the table
  RowVector data_;
};

}  // namespace trellis::utils::formatting

#endif  // TRELLIS_UTILS_FORMATTING_TABLE_HPP_
