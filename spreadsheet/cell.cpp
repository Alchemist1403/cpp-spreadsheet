#include "cell.h"
#include <algorithm>
#include <sstream>
#include <limits>
#include <queue>

// Impl Implementations

std::string Cell::EmptyImpl::GetText() const {
    return "";
}

std::vector<Position> Cell::EmptyImpl::GetReferencedCells() const {
    return {};
}

bool Cell::EmptyImpl::IsFormula() const {
    return false;
}

Cell::TextImpl::TextImpl(std::string text) : text_(std::move(text)) {}

std::string Cell::TextImpl::GetText() const {
    return text_;
}

std::vector<Position> Cell::TextImpl::GetReferencedCells() const {
    return {};
}

bool Cell::TextImpl::IsFormula() const {
    return false;
}

Cell::FormulaImpl::FormulaImpl(std::string formula_text)
    : formula_text_(std::move(formula_text)) {
    formula_ = ParseFormula(formula_text_);
}

std::string Cell::FormulaImpl::GetText() const {
    return "=" + formula_->GetExpression();
}

std::vector<Position> Cell::FormulaImpl::GetReferencedCells() const {
    return formula_->GetReferencedCells();
}

bool Cell::FormulaImpl::IsFormula() const {
    return true;
}

// Cell Implementation

Cell::Cell() : impl_(std::make_unique<EmptyImpl>()) {}

Cell::~Cell() = default;

void Cell::Set(std::string text) {
    text_ = text;
    InvalidateCache();
    
    if (text.empty()) {
        impl_ = std::make_unique<EmptyImpl>();
        dependencies_.clear();
    } else if (text[0] == ESCAPE_SIGN) {
        impl_ = std::make_unique<TextImpl>(text);
        dependencies_.clear();
    } else if (text[0] == FORMULA_SIGN) {
        if (text.size() == 1) {
            impl_ = std::make_unique<TextImpl>(text);
            dependencies_.clear();
        } else {
            try {
                std::string formula_expr = text.substr(1);
                impl_ = std::make_unique<FormulaImpl>(formula_expr);
                auto* f_impl = dynamic_cast<FormulaImpl*>(impl_.get());
                if (f_impl) {
                    dependencies_ = f_impl->GetReferencedCells();
                }
            } catch (const FormulaException&) {
                throw;
            }
        }
    } else {
        impl_ = std::make_unique<TextImpl>(text);
        dependencies_.clear();
    }
}

void Cell::Clear() {
    text_.clear();
    impl_ = std::make_unique<EmptyImpl>();
    dependencies_.clear();
    InvalidateCache();
}

void Cell::Recalculate(const SheetInterface& sheet) {
    if (impl_->IsFormula()) {
        auto* f_impl = dynamic_cast<FormulaImpl*>(impl_.get());
        if (f_impl) {
            try {
                auto formula_val = f_impl->GetFormula().Evaluate(sheet);
                if (std::holds_alternative<double>(formula_val)) {
                    cached_value_ = std::get<double>(formula_val);
                } else {
                    cached_value_ = std::get<FormulaError>(formula_val);
                }
                is_cache_valid_ = true;
            } catch (const FormulaError& e) {
                cached_value_ = e;
                is_cache_valid_ = true;
            }
        }
    } else {
        Value v;
        if (text_.empty()) {
            v = "";
        } else if (text_[0] == ESCAPE_SIGN) {
            v = text_.substr(1);
        } else {
            v = text_;
        }
        cached_value_ = v;
        is_cache_valid_ = true;
    }
}


Cell::Value Cell::GetValue() const {
    if (is_cache_valid_) {
        return *cached_value_;
    }

    if (!impl_->IsFormula()) {
        Value v;
        if (text_.empty()) {
            v = "";
        } else if (text_[0] == ESCAPE_SIGN) {
            v = text_.substr(1);
        } else {
            v = text_;
        }
        const_cast<Cell*>(this)->cached_value_ = v;
        const_cast<Cell*>(this)->is_cache_valid_ = true;
        return v;
    }
    
    return FormulaError(FormulaError::Category::Value); 
}



std::string Cell::GetText() const {
    return impl_->GetText();
}

std::vector<Position> Cell::GetReferencedCells() const {
    return dependencies_;
}

const std::vector<Position>& Cell::GetDependencies() const {
    return dependencies_;
}

void Cell::AddDependent(Position pos) {
    dependents_.insert(pos);
}

void Cell::RemoveDependent(Position pos) {
    dependents_.erase(pos);
}

const std::unordered_set<Position, PositionHash>& Cell::GetDependents() const {
    return dependents_;
}

void Cell::InvalidateCache() {
    is_cache_valid_ = false;
    cached_value_.reset();
}

bool Cell::IsCacheValid() const {
    return is_cache_valid_;
}

bool Cell::HasCycle(const Position& current_pos, const Position& target_pos, 
                    std::unordered_set<Position, PositionHash>& visited,
                    std::function<const Cell*(Position)> get_cell_func) {
    if (current_pos == target_pos) {
        return true;
    }
    
    if (visited.count(current_pos)) {
        return false;
    }
    visited.insert(current_pos);
    
    const Cell* cell = get_cell_func(current_pos);
    if (!cell) {
        return false;
    }
    
    for (const Position& dep : cell->GetDependencies()) {
        if (HasCycle(dep, target_pos, visited, get_cell_func)) {
            return true;
        }
    }
    
    return false;
}

void Cell::InvalidateCacheAndDependents(std::function<Cell*(Position)> get_cell_func) {
    std::queue<Position> q;
    std::unordered_set<Position, PositionHash> v;

    InvalidateCache();
    
    for (const Position& dep : dependents_) {
        q.push(dep);
        v.insert(dep);
    }
    
    while (!q.empty()) {
        Position curr = q.front();
        q.pop();
        
        Cell* cell = get_cell_func(curr);
        if (cell) {
            cell->InvalidateCache();
            
            for (const Position& next_dep : cell->dependents_) {
                if (!v.count(next_dep)) {
                    v.insert(next_dep);
                    q.push(next_dep);
                }
            }
        }
    }
}