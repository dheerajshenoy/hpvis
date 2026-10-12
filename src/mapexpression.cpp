#include "mapexpression.hpp"

#include <algorithm>
#include <cmath>

// Recursive-descent parser producing the postfix program
class ExpressionParser
{
public:
    ExpressionParser(const QString &text, const QStringList &variables,
                     MapExpression &out)
        : m_text(text), m_out(out)
    {
        for (const QString &v : variables)
            m_vars << v.toUpper();
    }

    bool parse(QString *error)
    {
        m_out.m_code.clear();
        m_out.m_used.clear();
        try
        {
            skipSpace();
            if (atEnd())
                fail("the expression is empty");
            expression();
            skipSpace();
            if (!atEnd())
                fail(QString("unexpected '%1'").arg(m_text[m_pos]));
            if (m_depth != 1)
                fail("malformed expression");
        }
        catch (const QString &message)
        {
            if (error)
                *error = message;
            return false;
        }
        std::sort(m_out.m_used.begin(), m_out.m_used.end());
        m_out.m_used.erase(
            std::unique(m_out.m_used.begin(), m_out.m_used.end()),
            m_out.m_used.end());
        return true;
    }

private:
    using Op = MapExpression::Op;

    [[noreturn]] void fail(const QString &what) const
    {
        throw QString("%1 (at position %2)").arg(what).arg(m_pos + 1);
    }

    bool atEnd() const
    {
        return m_pos >= m_text.size();
    }
    void skipSpace()
    {
        while (!atEnd() && m_text[m_pos].isSpace())
            ++m_pos;
    }
    bool accept(QChar c)
    {
        skipSpace();
        if (!atEnd() && m_text[m_pos] == c)
        {
            ++m_pos;
            return true;
        }
        return false;
    }

    void emit_(Op op, int arg = 0, double value = 0.0, int pops = 0,
               int pushes = 0)
    {
        m_depth += pushes - pops;
        m_peak = std::max(m_peak, m_depth);
        if (m_peak > MapExpression::kMaxStack)
            fail("the expression is too deeply nested");
        m_out.m_code.push_back({op, arg, value});
    }
    void binary(Op op)
    {
        emit_(op, 0, 0.0, 2, 1);
    }
    void unary(Op op)
    {
        emit_(op, 0, 0.0, 1, 1);
    }

    // expression := term (('+' | '-') term)*
    void expression()
    {
        term();
        for (;;)
        {
            if (accept('+'))
            {
                term();
                binary(Op::Add);
            }
            else if (accept('-'))
            {
                term();
                binary(Op::Sub);
            }
            else
                return;
        }
    }

    // term := factor (('*' | '/') factor)*
    void term()
    {
        factor();
        for (;;)
        {
            if (accept('*'))
            {
                factor();
                binary(Op::Mul);
            }
            else if (accept('/'))
            {
                factor();
                binary(Op::Div);
            }
            else
                return;
        }
    }

    // factor := ('-' | '+') factor | power
    void factor()
    {
        if (accept('-'))
        {
            factor();
            unary(Op::Neg);
        }
        else if (accept('+'))
            factor();
        else
            power();
    }

    // power := primary ('^' factor)?   (right-associative, binds tighter than
    // a leading minus: -2^2 is -4)
    void power()
    {
        primary();
        if (accept('^'))
        {
            factor();
            binary(Op::Pow);
        }
    }

    void primary()
    {
        skipSpace();
        if (atEnd())
            fail("the expression ends unexpectedly");

        if (accept('('))
        {
            expression();
            if (!accept(')'))
                fail("missing ')'");
            return;
        }

        const QChar c = m_text[m_pos];
        if (c.isDigit() || c == '.')
        {
            number();
            return;
        }
        if (c.isLetter() || c == '_')
        {
            identifier();
            return;
        }
        fail(QString("unexpected '%1'").arg(c));
    }

    void number()
    {
        const int start = m_pos;
        while (!atEnd() && (m_text[m_pos].isDigit() || m_text[m_pos] == '.'))
            ++m_pos;
        // optional exponent: 1e-3, 2.5E+4
        if (!atEnd() && (m_text[m_pos] == 'e' || m_text[m_pos] == 'E'))
        {
            int p = m_pos + 1;
            if (p < m_text.size() && (m_text[p] == '+' || m_text[p] == '-'))
                ++p;
            if (p < m_text.size() && m_text[p].isDigit())
            {
                m_pos = p;
                while (!atEnd() && m_text[m_pos].isDigit())
                    ++m_pos;
            }
        }
        bool ok            = false;
        const double value = m_text.mid(start, m_pos - start).toDouble(&ok);
        if (!ok)
        {
            m_pos = start;
            fail("invalid number");
        }
        emit_(Op::Push, 0, value, 0, 1);
    }

    void identifier()
    {
        const int start = m_pos;
        while (!atEnd()
               && (m_text[m_pos].isLetterOrNumber() || m_text[m_pos] == '_'))
            ++m_pos;
        const QString name = m_text.mid(start, m_pos - start);
        const QString up   = name.toUpper();

        skipSpace();
        if (!atEnd() && m_text[m_pos] == '(')
        {
            function(name.toLower(), start);
            return;
        }

        if (up == "PI")
        {
            emit_(Op::Push, 0, M_PI, 0, 1);
            return;
        }
        const int index = m_vars.indexOf(up);
        if (index < 0)
        {
            m_pos = start;
            fail(QString("unknown name '%1'").arg(name));
        }
        m_out.m_used.push_back(index);
        emit_(Op::Load, index, 0.0, 0, 1);
    }

    void function(const QString &name, int start)
    {
        struct Fn
        {
            const char *name;
            int arity;
            Op op;
        };
        static const Fn fns[] = {
            {"abs", 1, Op::Abs}, {"sqrt", 1, Op::Sqrt},   {"log", 1, Op::Log},
            {"ln", 1, Op::Log},  {"log10", 1, Op::Log10}, {"exp", 1, Op::Exp},
            {"sin", 1, Op::Sin}, {"cos", 1, Op::Cos},     {"tan", 1, Op::Tan},
            {"min", 2, Op::Min}, {"max", 2, Op::Max},     {"pow", 2, Op::Pow},
        };
        const Fn *fn = nullptr;
        for (const Fn &f : fns)
            if (name == f.name)
                fn = &f;
        if (!fn)
        {
            m_pos = start;
            fail(QString("unknown function '%1'").arg(name));
        }

        accept('(');
        for (int i = 0; i < fn->arity; ++i)
        {
            if (i > 0 && !accept(','))
                fail(QString("%1() needs %2 arguments")
                         .arg(name)
                         .arg(fn->arity));
            expression();
        }
        if (!accept(')'))
            fail(QString("%1() takes %2 argument(s); missing ')'")
                     .arg(name)
                     .arg(fn->arity));
        if (fn->arity == 1)
            unary(fn->op);
        else
            binary(fn->op);
    }

private:
    QString m_text;
    QStringList m_vars;
    MapExpression &m_out;
    int m_pos   = 0;
    int m_depth = 0;
    int m_peak  = 0;
};

bool
MapExpression::compile(const QString &text, const QStringList &variables,
                       QString *error)
{
    ExpressionParser parser(text, variables, *this);
    return parser.parse(error);
}

bool
MapExpression::evaluate(const double *vars, double &result) const
{
    double st[kMaxStack];
    int sp = 0;

    for (const Instr &in : m_code)
    {
        switch (in.op)
        {
            case Op::Push:
                st[sp++] = in.value;
                break;
            case Op::Load:
                st[sp++] = vars[in.arg];
                break;
            case Op::Add:
                --sp;
                st[sp - 1] += st[sp];
                break;
            case Op::Sub:
                --sp;
                st[sp - 1] -= st[sp];
                break;
            case Op::Mul:
                --sp;
                st[sp - 1] *= st[sp];
                break;
            case Op::Div:
                --sp;
                if (st[sp] == 0.0)
                    return false;
                st[sp - 1] /= st[sp];
                break;
            case Op::Pow:
                --sp;
                st[sp - 1] = std::pow(st[sp - 1], st[sp]);
                if (!std::isfinite(st[sp - 1]))
                    return false;
                break;
            case Op::Min:
                --sp;
                st[sp - 1] = std::min(st[sp - 1], st[sp]);
                break;
            case Op::Max:
                --sp;
                st[sp - 1] = std::max(st[sp - 1], st[sp]);
                break;
            case Op::Neg:
                st[sp - 1] = -st[sp - 1];
                break;
            case Op::Abs:
                st[sp - 1] = std::abs(st[sp - 1]);
                break;
            case Op::Sqrt:
                st[sp - 1] = std::sqrt(st[sp - 1]);
                if (!std::isfinite(st[sp - 1]))
                    return false;
                break;
            case Op::Log:
                st[sp - 1] = std::log(st[sp - 1]);
                if (!std::isfinite(st[sp - 1]))
                    return false;
                break;
            case Op::Log10:
                st[sp - 1] = std::log10(st[sp - 1]);
                if (!std::isfinite(st[sp - 1]))
                    return false;
                break;
            case Op::Exp:
                st[sp - 1] = std::exp(st[sp - 1]);
                if (!std::isfinite(st[sp - 1]))
                    return false;
                break;
            case Op::Sin:
                st[sp - 1] = std::sin(st[sp - 1]);
                break;
            case Op::Cos:
                st[sp - 1] = std::cos(st[sp - 1]);
                break;
            case Op::Tan:
                st[sp - 1] = std::tan(st[sp - 1]);
                break;
        }
    }
    result = st[0];
    return std::isfinite(result);
}
