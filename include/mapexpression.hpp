#pragma once

#include <QString>
#include <QStringList>
#include <cstdint>
#include <vector>

// Pixel-wise arithmetic on maps: compiles an expression such as
// "(A - B) / sqrt(C)" once, then evaluates it for one pixel at a time.
//
// Operators: + - * / ^ and unary -, with parentheses. Functions: abs, sqrt,
// log, log10, exp, sin, cos, tan, min(a, b), max(a, b), pow(a, b). The
// constant pi. Variable names are the operands' names, case-insensitively.
class MapExpression
{
public:
    static constexpr int kMaxStack = 64;

    // False with `error` set when the text is not a valid expression
    bool compile(const QString &text, const QStringList &variables,
                 QString *error);

    // Indices (into `variables`) of the operands the expression reads
    const std::vector<int> &usedVariables() const
    {
        return m_used;
    }

    // vars[i] is variable i for this pixel. False when the result is not a
    // finite number (division by zero, sqrt of a negative, ...).
    bool evaluate(const double *vars, double &result) const;

private:
    enum class Op : uint8_t
    {
        Push,
        Load,
        Add,
        Sub,
        Mul,
        Div,
        Pow,
        Neg,
        Abs,
        Sqrt,
        Log,
        Log10,
        Exp,
        Sin,
        Cos,
        Tan,
        Min,
        Max
    };
    struct Instr
    {
        Op op;
        int arg      = 0;   // variable index for Load
        double value = 0.0; // constant for Push
    };

    std::vector<Instr> m_code;
    std::vector<int> m_used;

    friend class ExpressionParser;
};
