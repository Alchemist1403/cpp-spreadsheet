#pragma once

#include "common.h"
#include "formula.h"

#include <memory>
#include <vector>
#include <unordered_set>
#include <optional>
#include <string>
#include <functional>
#include <queue>


struct PositionHash {
    size_t operator()(const Position& pos) const {
        size_t h1 = std::hash<int>{}(pos.row);
        size_t h2 = std::hash<int>{}(pos.col);
        h1 ^= h2 + 0x9e3779b9 + (h1 << 6) + (h1 >> 2);
        return h1;
    }
};

class SheetInterface;

class Cell : public CellInterface {
public:
    Cell();
    ~Cell();

    Value GetValue() const override;
    std::string GetText() const override;
    std::vector<Position> GetReferencedCells() const override;

    void Set(std::string text);
    void Clear();
    void Recalculate(const SheetInterface& sheet);

    const std::vector<Position>& GetDependencies() const;
    void AddDependent(Position pos);
    void RemoveDependent(Position pos);
    const std::unordered_set<Position, PositionHash>& GetDependents() const;

    void InvalidateCache();
    bool IsCacheValid() const;

    static bool HasCycle(const Position& current_pos, const Position& target_pos, 
                        std::unordered_set<Position, PositionHash>& visited,
                        std::function<const Cell*(Position)> get_cell_func);

    void InvalidateCacheAndDependents(std::function<Cell*(Position)> get_cell_func);

private:
    class Impl {
    public:
        virtual ~Impl() = default;
        virtual std::string GetText() const = 0;
        virtual std::vector<Position> GetReferencedCells() const = 0;
        virtual bool IsFormula() const = 0;
    };

    class EmptyImpl : public Impl {
    public:
        std::string GetText() const override;
        std::vector<Position> GetReferencedCells() const override;
        bool IsFormula() const override;
    };

    class TextImpl : public Impl {
    public:
        explicit TextImpl(std::string text);
        std::string GetText() const override;
        std::vector<Position> GetReferencedCells() const override;
        bool IsFormula() const override;
    private:
        std::string text_;
    };

    class FormulaImpl : public Impl {
    public:
        explicit FormulaImpl(std::string formula_text);
        std::string GetText() const override;
        std::vector<Position> GetReferencedCells() const override;
        bool IsFormula() const override;
        const FormulaInterface& GetFormula() const { return *formula_; }
    private:
        std::unique_ptr<FormulaInterface> formula_;
        std::string formula_text_;
    };

    std::unique_ptr<Impl> impl_;
    
    mutable std::optional<Value> cached_value_;
    mutable bool is_cache_valid_ = false;

    std::vector<Position> dependencies_;
    std::unordered_set<Position, PositionHash> dependents_;
    std::string text_;
};