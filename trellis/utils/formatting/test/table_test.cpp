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

#include "trellis/utils/formatting/table.hpp"

#include <gtest/gtest.h>

#include <sstream>
#include <string>

namespace trellis {
namespace utils {
namespace formatting {

TEST(TableTest, EmptyTable) {
  using TableType = Table<std::string, int>;
  TableType table({"Name", "Age"});

  std::stringstream ss;
  table.Print(ss);

  const std::string expected =
      "-----------------\n"
      "|  Name  |  Age  |\n"
      "-----------------\n"
      "-----------------\n";
  EXPECT_EQ(ss.str(), expected);
}

TEST(TableTest, SingleRowStringColumns) {
  using TableType = Table<std::string, std::string>;
  TableType table({"First", "Last"});

  table.AddRow("John", "Doe");

  std::stringstream ss;
  table.Print(ss);

  const std::string expected =
      "-------------------\n"
      "|  First  |  Last  |\n"
      "-------------------\n"
      "| John    | Doe    |\n"
      "-------------------\n";
  EXPECT_EQ(ss.str(), expected);
}

TEST(TableTest, MultipleRowsMixedTypes) {
  using TableType = Table<std::string, int, double>;
  TableType table({"Name", "Age", "Score"});

  table.AddRow("Alice", 25, 95.5);
  table.AddRow("Bob", 30, 87.2);
  table.AddRow("Charlie", 35, 92.8);

  std::stringstream ss;
  table.Print(ss);

  const std::string expected =
      "------------------------------\n"
      "|   Name    |  Age  |  Score  |\n"
      "------------------------------\n"
      "| Alice     | 25    | 95.5    |\n"
      "| Bob       | 30    | 87.2    |\n"
      "| Charlie   | 35    | 92.8    |\n"
      "------------------------------\n";
  EXPECT_EQ(ss.str(), expected);
}

TEST(TableTest, SingleColumn) {
  using TableType = Table<int>;
  TableType table({"ID"});

  table.AddRow(1);
  table.AddRow(2);
  table.AddRow(3);

  std::stringstream ss;
  table.Print(ss);

  const std::string expected =
      "-------\n"
      "|  ID  |\n"
      "-------\n"
      "| 1    |\n"
      "| 2    |\n"
      "| 3    |\n"
      "-------\n";
  EXPECT_EQ(ss.str(), expected);
}

TEST(TableTest, PrintRowIndividually) {
  using TableType = Table<std::string, int>;
  TableType table({"Product", "Quantity"});

  table.AddRow("Widget", 10);
  table.AddRow("Gadget", 5);

  std::stringstream ss;

  // Print header manually
  ss << "Product Quantity " << std::endl;

  // Print individual rows
  table.PrintRow(ss, std::make_tuple(std::string("Special"), 99));

  const std::string expected =
      "Product Quantity \n"
      "| Special   | 99         |\n";
  EXPECT_EQ(ss.str(), expected);
}

TEST(TableTest, LargeTable) {
  using TableType = Table<int, std::string, double, bool>;
  TableType table({"ID", "Name", "Value", "Active"});

  // Add multiple rows
  for (int i = 0; i < 5; ++i) {
    table.AddRow(i, "Item" + std::to_string(i), i * 1.5, i % 2 == 0);
  }

  std::stringstream ss;
  table.Print(ss);

  const std::string expected =
      "--------------------------------------\n"
      "|  ID  |  Name   |  Value  |  Active  |\n"
      "--------------------------------------\n"
      "| 0    | Item0   | 0       | 1        |\n"
      "| 1    | Item1   | 1.5     | 0        |\n"
      "| 2    | Item2   | 3       | 1        |\n"
      "| 3    | Item3   | 4.5     | 0        |\n"
      "| 4    | Item4   | 6       | 1        |\n"
      "--------------------------------------\n";
  EXPECT_EQ(ss.str(), expected);
}

TEST(TableTest, StringWithSpaces) {
  using TableType = Table<std::string, std::string>;
  TableType table({"Description", "Status"});

  table.AddRow("Task with spaces", "In Progress");
  table.AddRow("Another task", "Completed");

  std::stringstream ss;
  table.Print(ss);

  const std::string expected =
      "-------------------------------------\n"
      "|    Description     |    Status     |\n"
      "-------------------------------------\n"
      "| Task with spaces   | In Progress   |\n"
      "| Another task       | Completed     |\n"
      "-------------------------------------\n";
  EXPECT_EQ(ss.str(), expected);
}

TEST(TableTest, NegativeNumbers) {
  using TableType = Table<int, double>;
  TableType table({"Integer", "Float"});

  table.AddRow(-10, -5.5);
  table.AddRow(0, 0.0);
  table.AddRow(15, 3.14159);

  std::stringstream ss;
  table.Print(ss);

  const std::string expected =
      "------------------------\n"
      "|  Integer  |   Float   |\n"
      "------------------------\n"
      "| -10       | -5.5      |\n"
      "| 0         | 0         |\n"
      "| 15        | 3.14159   |\n"
      "------------------------\n";
  EXPECT_EQ(ss.str(), expected);
}

}  // namespace formatting
}  // namespace utils
}  // namespace trellis
