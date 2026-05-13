#pragma once

#include "cell.h"
#include "common.h"

#include <functional>
#include <memory>
#include <unordered_map>
#include <vector>
#include <ostream>
#include <unordered_set>

class Sheet : public SheetInterface {
public:
    ~Sheet() override = default;

    void SetCell(Position pos, std::string text) override;
    const CellInterface* GetCell(Position pos) const override;
    CellInterface* GetCell(Position pos) override;
    void ClearCell(Position pos) override;
    Size GetPrintableSize() const override;
    void PrintValues(std::ostream& output) const override;
    void PrintTexts(std::ostream& output) const override;

private:
    std::unordered_map<Position, std::unique_ptr<Cell>, PositionHash> cells_;
    
    int min_row_ = 0;
    int max_row_ = -1;
    int min_col_ = 0;
    int max_col_ = -1;

    void UpdatePrintableArea(const Position& pos, bool is_cleared);
    Cell* GetConcreteCell(Position pos);
    const Cell* GetConcreteCell(Position pos) const;
};

std::unique_ptr<SheetInterface> CreateSheet();