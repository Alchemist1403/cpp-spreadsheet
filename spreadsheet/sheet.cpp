#include "sheet.h"
#include "cell.h"
#include "common.h"
#include <algorithm>
#include <iostream>
#include <limits>
#include <sstream>
#include <queue>

using namespace std::literals;

Cell* Sheet::GetConcreteCell(Position pos) {
    auto it = cells_.find(pos);
    if (it == cells_.end()) {
        return nullptr;
    }
    return it->second.get();
}

const Cell* Sheet::GetConcreteCell(Position pos) const {
    auto it = cells_.find(pos);
    if (it == cells_.end()) {
        return nullptr;
    }
    return it->second.get();
}

void Sheet::UpdatePrintableArea(const Position& pos, bool is_cleared) {
    if (is_cleared) {
        if (max_row_ != -1) {
             max_row_ = -1;
             max_col_ = -1;
             for (const auto& [p, cell] : cells_) {
                 if (!cell->GetText().empty()) {
                     max_row_ = std::max(max_row_, p.row);
                     max_col_ = std::max(max_col_, p.col);
                 }
             }
        }
    } else {
        if (cells_[pos]->GetText().empty()) {
            return;
        }
        if (max_row_ == -1) {
            max_row_ = pos.row;
            max_col_ = pos.col;
        } else {
            max_row_ = std::max(max_row_, pos.row);
            max_col_ = std::max(max_col_, pos.col);
        }
    }
}

void Sheet::ValidatePosition(const Position& pos) const {
    if (!pos.IsValid()) {
        throw InvalidPositionException("Invalid position");
    }
}

void Sheet::SetCell(Position pos, std::string text) {

    ValidatePosition(pos);
    std::vector<Position> new_deps;

    bool is_formula = (!text.empty() && text[0] == FORMULA_SIGN && text.size() > 1);
    
    if (is_formula) {
        try {
            auto formula = ParseFormula(text.substr(1));
            new_deps = formula->GetReferencedCells();
        } catch (const FormulaException&) {
            throw;
        }
    }

    if (is_formula) {
        std::unordered_set<Position, PositionHash> visited;
        auto get_cell_func = [this](Position p) -> const Cell* {
            return this->GetConcreteCell(p);
        };
        for (const Position& dep : new_deps) {
            if (Cell::HasCycle(dep, pos, visited, get_cell_func)) {
                throw CircularDependencyException("Circular dependency detected");
            }
        }
    }

    std::vector<Position> old_deps;
    Cell* old_cell = GetConcreteCell(pos);
    if (old_cell) {
        old_deps = old_cell->GetDependencies();
    }

    for (const Position& dep_pos : old_deps) {
        Cell* dep_cell = GetConcreteCell(dep_pos);
        if (dep_cell) {
            dep_cell->RemoveDependent(pos);
        }
    }

    if (!cells_[pos]) {
        cells_[pos] = std::make_unique<Cell>();
    }
    cells_[pos]->Set(std::move(text));

    for (const Position& dep_pos : new_deps) {
        if (cells_.find(dep_pos) == cells_.end()) {
            cells_[dep_pos] = std::make_unique<Cell>();
        }
        cells_[dep_pos]->AddDependent(pos);
    }

    auto get_mutable_cell_func = [this](Position p) -> Cell* {
        return this->GetConcreteCell(p);
    };
    cells_[pos]->InvalidateCacheAndDependents(get_mutable_cell_func);
    UpdatePrintableArea(pos, false);
    Cell* current_cell = GetConcreteCell(pos);
    if (current_cell && is_formula) {
        current_cell->Recalculate(*this);
    }
}

const CellInterface* Sheet::GetCell(Position pos) const {
    ValidatePosition(pos);
    auto it = cells_.find(pos);
    if (it == cells_.end()) {
        return nullptr;
    }
    if (it->second->GetText().empty()) {
        return nullptr;
    }
    return it->second.get();
}

CellInterface* Sheet::GetCell(Position pos) {
    ValidatePosition(pos);
    auto it = cells_.find(pos);
    if (it == cells_.end()) {
        return nullptr;
    }
    if (it->second->GetText().empty()) {
        return nullptr;
    }
    return it->second.get();
}

void Sheet::ClearCell(Position pos) {
    ValidatePosition(pos);
    auto it = cells_.find(pos);
    if (it == cells_.end()) {
        return;
    }

    std::vector<Position> deps = it->second->GetDependencies();
    for (const Position& dep_pos : deps) {
        Cell* dep_cell = GetConcreteCell(dep_pos);
        if (dep_cell) {
            dep_cell->RemoveDependent(pos);
        }
    }

    auto get_mutable_cell_func = [this](Position p) -> Cell* {
        return this->GetConcreteCell(p);
    };
    it->second->InvalidateCacheAndDependents(get_mutable_cell_func);
    
    cells_.erase(it);
    UpdatePrintableArea(pos, true);
}

Size Sheet::GetPrintableSize() const {
    if (max_row_ == -1) {
        return {0, 0};
    }
    return {max_row_ + 1, max_col_ + 1};
}

void Sheet::PrintValues(std::ostream& output) const {
    Size size = GetPrintableSize();
    if (size.rows == 0 || size.cols == 0) {
        return;
    }
    for (int row = 0; row < size.rows; ++row) {
        for (int col = 0; col < size.cols; ++col) {
            if (col > 0) {
                output << '\t';
            }
            Position pos{min_row_ + row, min_col_ + col};
            auto it = cells_.find(pos);
            if (it != cells_.end() && !it->second->GetText().empty()) {
                Cell* mutable_cell = it->second.get();
                if (!mutable_cell->IsCacheValid()) {
                    mutable_cell->Recalculate(*this);
                }
                auto value = it->second->GetValue();
                std::visit([&output](const auto& v) {
                    output << v;
                }, value);
            }
        }
        output << '\n';
    }
}

void Sheet::PrintTexts(std::ostream& output) const {
    Size size = GetPrintableSize();
    if (size.rows == 0 || size.cols == 0) {
        return;
    }
    for (int row = 0; row < size.rows; ++row) {
        for (int col = 0; col < size.cols; ++col) {
            if (col > 0) {
                output << '\t';
            }
            Position pos{min_row_ + row, min_col_ + col};
            auto it = cells_.find(pos);
            if (it != cells_.end() && !it->second->GetText().empty()) {
                output << it->second->GetText();
            }
        }
        output << '\n';
    }
}

std::unique_ptr<SheetInterface> CreateSheet() {
    return std::make_unique<Sheet>();
}