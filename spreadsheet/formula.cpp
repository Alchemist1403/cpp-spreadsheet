#include "formula.h"
#include "FormulaAST.h"
#include <sstream>

class Formula final : public FormulaInterface {
public:
    explicit Formula(std::string expression)
        : expression_(std::move(expression))
        , ast_(ParseFormulaAST(expression_)) {
    }

    Value Evaluate(const SheetInterface& sheet) const override {
        try {
            double result = ast_.Execute(sheet);
            return result;
        } catch (const FormulaError& e) {
            return e;
        }
    }

    std::string GetExpression() const override {
        std::ostringstream oss;
        ast_.PrintFormula(oss);
        return oss.str();
    }

    std::vector<Position> GetReferencedCells() const override {
        return ast_.GetReferencedCells();
    }

private:
    std::string expression_;
    FormulaAST ast_;
};

std::unique_ptr<FormulaInterface> ParseFormula(std::string expression) {
    return std::make_unique<Formula>(std::move(expression));
}