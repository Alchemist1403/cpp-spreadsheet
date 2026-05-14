#include "FormulaAST.h"
#include "FormulaBaseListener.h"
#include "FormulaLexer.h"
#include "FormulaParser.h"
#include "common.h"
#include "cell.h"
#include <cassert>
#include <cmath>
#include <sstream>
#include <algorithm>
#include <limits>
#include <stack>

namespace ASTImpl {

enum ExprPrecedence {
    EP_ADD,
    EP_SUB,
    EP_MUL,
    EP_DIV,
    EP_UNARY,
    EP_ATOM,
    EP_END,
};

enum PrecedenceRule {
    PR_NONE = 0b00,
    PR_LEFT = 0b01,
    PR_RIGHT = 0b10,
    PR_BOTH = PR_LEFT | PR_RIGHT,
};

constexpr PrecedenceRule PRECEDENCE_RULES[EP_END][EP_END] = {
    /* EP_ADD   */ {PR_NONE, PR_NONE, PR_NONE, PR_NONE, PR_NONE, PR_NONE},
    /* EP_SUB   */ {PR_RIGHT, PR_RIGHT, PR_NONE, PR_NONE, PR_NONE, PR_NONE},
    /* EP_MUL   */ {PR_BOTH, PR_BOTH, PR_RIGHT, PR_RIGHT, PR_NONE, PR_NONE},
    /* EP_DIV   */ {PR_BOTH, PR_BOTH, PR_RIGHT, PR_RIGHT, PR_NONE, PR_NONE},
    /* EP_UNARY */ {PR_BOTH, PR_BOTH, PR_NONE, PR_NONE, PR_NONE, PR_NONE},
    /* EP_ATOM  */ {PR_NONE, PR_NONE, PR_NONE, PR_NONE, PR_NONE, PR_NONE},
};

class Expr {
public:
    virtual ~Expr() = default;
    virtual void Print(std::ostream& out) const = 0;
    virtual void DoPrintFormula(std::ostream& out, ExprPrecedence precedence) const = 0;
    virtual double Execute(const SheetInterface& sheet) const = 0;
    virtual ExprPrecedence GetPrecedence() const = 0;

    void PrintFormula(std::ostream& out, ExprPrecedence parent_precedence, bool right_child = false) const {
        auto precedence = GetPrecedence();
        auto mask = right_child ? PR_RIGHT : PR_LEFT;
        bool parens_needed = PRECEDENCE_RULES[parent_precedence][precedence] & mask;
        if (parens_needed) {
            out << '(';
        }
        DoPrintFormula(out, precedence);
        if (parens_needed) {
            out << ')';
        }
    }
};

namespace {

class BinaryOpExpr final : public Expr {
public:
    enum Type : char {
        Add = '+',
        Subtract = '-',
        Multiply = '*',
        Divide = '/',
    };

    explicit BinaryOpExpr(Type type, std::unique_ptr<Expr> lhs, std::unique_ptr<Expr> rhs):
        type_(type),
        lhs_(std::move(lhs)), 
        rhs_(std::move(rhs)) {}

    void Print(std::ostream& out) const override {
        out << '(' << static_cast<char>(type_) << ' ';
        lhs_->Print(out);
        out << ' ';
        rhs_->Print(out);
        out << ')';
    }

    void DoPrintFormula(std::ostream& out, ExprPrecedence precedence) const override {
        lhs_->PrintFormula(out, precedence);
        out << static_cast<char>(type_);
        rhs_->PrintFormula(out, precedence, true);
    }

    ExprPrecedence GetPrecedence() const override {
        switch (type_) {
            case Add: 
                return EP_ADD;
            case Subtract: 
                return EP_SUB;
            case Multiply: 
                return EP_MUL;
            case Divide: 
                return EP_DIV;
            default: 
                assert(false); 
                return EP_END;
        }
    }

    double Execute(const SheetInterface& sheet) const override {
        double left_val = lhs_->Execute(sheet);
        double right_val = rhs_->Execute(sheet);
        double result = 0.0;

        switch (type_) {
            case Add: 
                result = left_val + right_val;
                break;
            case Subtract: 
                result = left_val - right_val; 
                break;
            case Multiply: 
                result = left_val * right_val; 
                break;
            case Divide:
                if (right_val == 0.0) {
                    throw FormulaError(FormulaError::Category::Arithmetic);
                }
                result = left_val / right_val;
                break;
        }

        if (!std::isfinite(result)) {
            throw FormulaError(FormulaError::Category::Arithmetic);
        }
        return result;
    }

private:
    Type type_;
    std::unique_ptr<Expr> lhs_;
    std::unique_ptr<Expr> rhs_;
};

class UnaryOpExpr final : public Expr {
public:
    enum Type : char {
        UnaryPlus = '+',
        UnaryMinus = '-',
    };

    explicit UnaryOpExpr(Type type, std::unique_ptr<Expr> operand)
        : type_(type), 
        operand_(std::move(operand)) {}

    void Print(std::ostream& out) const override {
        out << '(' << static_cast<char>(type_) << ' ';
        operand_->Print(out);
        out << ')';
    }

    void DoPrintFormula(std::ostream& out, ExprPrecedence precedence) const override {
        out << static_cast<char>(type_);
        operand_->PrintFormula(out, precedence);
    }

    ExprPrecedence GetPrecedence() const override {
        return EP_UNARY;
    }

    double Execute(const SheetInterface& sheet) const override {
        double val = operand_->Execute(sheet);

        if (type_ == UnaryMinus) {
            val = -val;
        }

        if (!std::isfinite(val)) {
            throw FormulaError(FormulaError::Category::Arithmetic);
        }

        return val;
    }

private:
    Type type_;
    std::unique_ptr<Expr> operand_;
};

class NumberExpr final : public Expr {
public:
    explicit NumberExpr(double value) : value_(value) {}

    void Print(std::ostream& out) const override {
        out << value_;
    }

    void DoPrintFormula(std::ostream& out, ExprPrecedence) const override {
        out << value_;
    }

    ExprPrecedence GetPrecedence() const override {
        return EP_ATOM;
    }

    double Execute(const SheetInterface&) const override {
        return value_;
    }

private:
    double value_;
};

class CellRefExpr final : public Expr {
public:
    explicit CellRefExpr(Position pos) : pos_(pos) {}

    void Print(std::ostream& out) const override {
        out << pos_.ToString();
    }

    void DoPrintFormula(std::ostream& out, ExprPrecedence) const override {
        out << pos_.ToString();
    }

    ExprPrecedence GetPrecedence() const override {
        return EP_ATOM;
    }

    double Execute(const SheetInterface& sheet) const override {

        if (!pos_.IsValid()) {
            throw FormulaError(FormulaError::Category::Ref);
        }

        const CellInterface* cell = sheet.GetCell(pos_);

        if (!cell) {
            return 0.0;
        }

        auto value = cell->GetValue();
        if (std::holds_alternative<double>(value)) {
            return std::get<double>(value);
        } else if (std::holds_alternative<std::string>(value)) {
            const std::string& s = std::get<std::string>(value);
            try {
                size_t pos;
                double d = std::stod(s, &pos);
                if (pos != s.size()) {
                    throw FormulaError(FormulaError::Category::Value);
                }
                return d;
            } catch (...) {
                throw FormulaError(FormulaError::Category::Value);
            }
        } else if (std::holds_alternative<FormulaError>(value)) {
            throw std::get<FormulaError>(value);
        }
        throw FormulaError(FormulaError::Category::Value);
    }

private:
    Position pos_;
};

class ParseASTListener final : public FormulaBaseListener {
public:
    std::unique_ptr<Expr> MoveRoot() {
        assert(args_.size() == 1);
        auto root = std::move(args_.front());
        args_.clear();
        return root;
    }

    std::vector<Position> MoveReferencedCells() {
        return std::move(referenced_cells_);
    }

    void exitUnaryOp(FormulaParser::UnaryOpContext* ctx) override {
        assert(args_.size() >= 1);
        auto operand = std::move(args_.back());
        UnaryOpExpr::Type type = ctx->SUB() ? UnaryOpExpr::UnaryMinus : UnaryOpExpr::UnaryPlus;
        auto node = std::make_unique<UnaryOpExpr>(type, std::move(operand));
        args_.back() = std::move(node);
    }

    void exitLiteral(FormulaParser::LiteralContext* ctx) override {
        double value = 0;
        auto valueStr = ctx->NUMBER()->getSymbol()->getText();
        std::istringstream in(valueStr);
        in >> value;

        if (!in) {
            throw ParsingError("Invalid number: " + valueStr);
        }

        auto node = std::make_unique<NumberExpr>(value);
        args_.push_back(std::move(node));
    }

    void exitBinaryOp(FormulaParser::BinaryOpContext* ctx) override {
        assert(args_.size() >= 2);
        auto rhs = std::move(args_.back());
        args_.pop_back();
        auto lhs = std::move(args_.back());

        BinaryOpExpr::Type type;
        if (ctx->ADD()) {
            type = BinaryOpExpr::Add;
        } else if (ctx->SUB()) {
            type = BinaryOpExpr::Subtract;
        } else if (ctx->MUL()) {
            type = BinaryOpExpr::Multiply;
        } else {
            type = BinaryOpExpr::Divide;
        }

        auto node = std::make_unique<BinaryOpExpr>(type, std::move(lhs), std::move(rhs));
        args_.back() = std::move(node);
    }

    void exitCell(FormulaParser::CellContext* ctx) override {
        std::string ref = ctx->CELL()->getSymbol()->getText();
        Position pos = Position::FromString(ref);

        if (!pos.IsValid()) {
            throw ParsingError("Invalid cell reference: " + ref);
        }

        referenced_cells_.push_back(pos);
        auto node = std::make_unique<CellRefExpr>(pos);
        args_.push_back(std::move(node));
    }

    void visitErrorNode(antlr4::tree::ErrorNode* node) override {
        throw ParsingError("Error when parsing: " + node->getSymbol()->getText());
    }

private:
    std::vector<std::unique_ptr<Expr>> args_;
    std::vector<Position> referenced_cells_;
};

class BailErrorListener : public antlr4::BaseErrorListener {
public:
    void syntaxError(antlr4::Recognizer*, antlr4::Token*, size_t, size_t, const std::string& msg, std::exception_ptr) override {
        throw ParsingError("Error when lexing: " + msg);
    }
};

} // namespace
} // namespace ASTImpl

FormulaAST::FormulaAST(std::unique_ptr<ASTImpl::Expr> root_expr, std::vector<Position> referenced_cells)
    : root_expr_(std::move(root_expr)), referenced_cells_(std::move(referenced_cells)) {
    std::sort(referenced_cells_.begin(), referenced_cells_.end());
    referenced_cells_.erase(std::unique(referenced_cells_.begin(), referenced_cells_.end()), referenced_cells_.end());
}

FormulaAST::~FormulaAST() = default;

double FormulaAST::Execute(const SheetInterface& sheet) const {
    return root_expr_->Execute(sheet);
}

void FormulaAST::Print(std::ostream& out) const {
    root_expr_->Print(out);
}

void FormulaAST::PrintFormula(std::ostream& out) const {
    root_expr_->PrintFormula(out, ASTImpl::EP_ATOM);
}

const std::vector<Position>& FormulaAST::GetReferencedCells() const {
    return referenced_cells_;
}

FormulaAST ParseFormulaAST(std::istream& in) {
    using namespace antlr4;
    ANTLRInputStream input(in);
    FormulaLexer lexer(&input);
    ASTImpl::BailErrorListener error_listener;
    lexer.removeErrorListeners();
    lexer.addErrorListener(&error_listener);

    CommonTokenStream tokens(&lexer);
    FormulaParser parser(&tokens);
    auto error_handler = std::make_shared<BailErrorStrategy>();
    parser.setErrorHandler(error_handler);
    parser.removeErrorListeners();

    tree::ParseTree* tree = parser.main();
    ASTImpl::ParseASTListener listener;
    tree::ParseTreeWalker::DEFAULT.walk(&listener, tree);

    return FormulaAST(listener.MoveRoot(), listener.MoveReferencedCells());
}

FormulaAST ParseFormulaAST(const std::string& in_str) {
    std::istringstream in(in_str);
    try {
        return ParseFormulaAST(in);
    } catch (const std::exception& exc) {
        std::throw_with_nested(FormulaException(exc.what()));
    }
}