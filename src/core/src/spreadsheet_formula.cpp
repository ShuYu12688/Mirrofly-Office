#include <mirrorfly/spreadsheet.hpp>

#include <algorithm>
#include <cmath>
#include <functional>
#include <iomanip>
#include <locale>
#include <sstream>
#include <stdexcept>
#include <tuple>

namespace
{
    using namespace mirrorfly;
    using Key = std::tuple<std::size_t, std::uint32_t, std::uint32_t>;

    struct InvalidFormula
    {
    };

    SpreadsheetValue error(const char* text)
    {
        return {SpreadsheetValueKind::Error, text};
    }

    SpreadsheetValue number(double value)
    {
        if (!std::isfinite(value))
            return error("#NUM!");
        std::ostringstream output;
        output.imbue(std::locale::classic());
        output << std::setprecision(15) << (value == 0 ? 0 : value);
        return {SpreadsheetValueKind::Number, output.str()};
    }

    double numeric(const SpreadsheetValue& value)
    {
        if (value.kind == SpreadsheetValueKind::Empty)
            return 0;
        if (value.kind == SpreadsheetValueKind::Boolean)
            return value.text == "1" || value.text == "true" ? 1 : 0;
        if (value.kind != SpreadsheetValueKind::Number)
            throw value.kind == SpreadsheetValueKind::Error ? value : error("#VALUE!");
        std::istringstream input(value.text);
        input.imbue(std::locale::classic());
        double result = 0;
        input >> result;
        if (!input || !input.eof() || !std::isfinite(result))
            throw error("#NUM!");
        return result;
    }

    bool letter(char c)
    {
        return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
    }

    bool digit(char c)
    {
        return c >= '0' && c <= '9';
    }

    std::string upper(std::string value)
    {
        for (auto& c : value)
            if (c >= 'a' && c <= 'z')
                c -= 'a' - 'A';
        return value;
    }

    struct Reference
    {
        std::size_t start = 0;
        std::size_t end = 0;
        SpreadsheetAddress address;
        bool fixed_row = false;
        bool fixed_column = false;
    };

    struct Operand
    {
        SpreadsheetValue value;
        std::optional<SpreadsheetRange> range;
        std::string sheet;
    };

    struct ReferenceGroup
    {
        std::size_t start = 0;
        std::size_t end = 0;
        std::string sheet;
        Reference first;
        Reference last;
        bool range = false;
    };

    class Parser
    {
    public:
        Parser(const std::string& text,
            std::function<SpreadsheetValue(const std::string&, SpreadsheetAddress)> read = {})
            : text_(text), read_(std::move(read))
        {
        }

        SpreadsheetValue run()
        {
            if (text_.empty() || text_.size() > 8192)
                throw InvalidFormula{};
            auto value = scalar(expression());
            space();
            if (position_ != text_.size())
                throw InvalidFormula{};
            return value.kind == SpreadsheetValueKind::Empty ? number(0) : value;
        }

        std::vector<Reference> references;
        std::vector<ReferenceGroup> groups;

    private:
        const std::string& text_;
        std::size_t position_ = 0;
        std::size_t depth_ = 0;
        std::function<SpreadsheetValue(const std::string&, SpreadsheetAddress)> read_;

        void space()
        {
            while (position_ < text_.size() && (text_[position_] == ' ' || text_[position_] == '\t'))
                ++position_;
        }

        char peek()
        {
            space();
            return position_ < text_.size() ? text_[position_] : '\0';
        }

        bool take(char c)
        {
            if (peek() != c)
                return false;
            ++position_;
            return true;
        }

        SpreadsheetValue scalar(const Operand& value)
        {
            if (!read_)
                return number(0);
            if (!value.range)
                return value.value;
            if (value.range->first.row != value.range->last.row ||
                value.range->first.column != value.range->last.column)
                return error("#VALUE!");
            return read_(value.sheet, value.range->first);
        }

        Operand operation(Operand left, Operand right, char op)
        {
            if (!read_)
                return {number(0), {}, {}};
            try
            {
                auto a = numeric(scalar(left));
                auto b = numeric(scalar(right));
                if (op == '/' && b == 0)
                    return {error("#DIV/0!"), {}, {}};
                double result = 0;
                switch (op)
                {
                case '+':
                    result = a + b;
                    break;
                case '-':
                    result = a - b;
                    break;
                case '*':
                    result = a * b;
                    break;
                case '/':
                    result = a / b;
                    break;
                case '^':
                    result = std::pow(a, b);
                    break;
                }
                return {number(result), {}, {}};
            }
            catch (const SpreadsheetValue& failure)
            {
                return {failure, {}, {}};
            }
        }

        Operand expression()
        {
            auto left = product();
            while (peek() == '+' || peek() == '-')
            {
                const char op = text_[position_++];
                left = operation(std::move(left), product(), op);
            }
            return left;
        }

        Operand product()
        {
            auto left = power();
            while (peek() == '*' || peek() == '/')
            {
                const char op = text_[position_++];
                left = operation(std::move(left), power(), op);
            }
            return left;
        }

        Operand power()
        {
            if (++depth_ > 64)
                throw InvalidFormula{};
            auto left = primary();
            while (take('%'))
                left = operation(std::move(left), {number(100), {}, {}}, '/');
            if (take('^'))
                left = operation(std::move(left), power(), '^');
            --depth_;
            return left;
        }

        SpreadsheetAddress reference()
        {
            space();
            Reference ref;
            ref.start = position_;
            ref.fixed_column = take('$');
            auto start = position_;
            while (position_ < text_.size() && letter(text_[position_]))
                ++position_;
            const auto column = text_.substr(start, position_ - start);
            ref.fixed_row = take('$');
            start = position_;
            while (position_ < text_.size() && digit(text_[position_]))
                ++position_;
            auto address = parse_spreadsheet_address(column + text_.substr(start, position_ - start));
            if (!address)
                throw InvalidFormula{};
            ref.end = position_;
            ref.address = *address;
            references.push_back(ref);
            return *address;
        }

        Operand function(const std::string& name)
        {
            if (name != "SUM" && name != "AVERAGE" && name != "COUNT" && name != "MIN" && name != "MAX")
                throw InvalidFormula{};
            std::size_t count = 0;
            double total = 0, minimum = 0, maximum = 0;
            SpreadsheetValue failure;
            const auto collect = [&](const SpreadsheetValue& value, bool reference_value)
            {
                if (!read_)
                    return;
                if (value.kind == SpreadsheetValueKind::Error)
                {
                    if (name != "COUNT" && failure.kind != SpreadsheetValueKind::Error)
                        failure = value;
                    return;
                }
                if (reference_value && value.kind != SpreadsheetValueKind::Number)
                    return;
                try
                {
                    const auto n = numeric(value);
                    minimum = count ? std::min(minimum, n) : n;
                    maximum = count ? std::max(maximum, n) : n;
                    total += n;
                    ++count;
                }
                catch (const SpreadsheetValue& value_error)
                {
                    failure = value_error;
                }
            };
            if (!take(')'))
            {
                do
                {
                    auto argument = expression();
                    if (argument.range && read_)
                    {
                        const auto r = *argument.range;
                        const auto cells = std::uint64_t(r.last.row - r.first.row + 1) *
                            (r.last.column - r.first.column + 1);
                        if (cells > maximum_spreadsheet_cells)
                            failure = error("#NUM!");
                        else
                            for (auto row = r.first.row; row <= r.last.row; ++row)
                                for (auto column = r.first.column; column <= r.last.column; ++column)
                                    collect(read_(argument.sheet, {row, column}), true);
                    }
                    else
                        collect(scalar(argument), false);
                } while (take(','));
                if (!take(')'))
                    throw InvalidFormula{};
            }
            if (failure.kind == SpreadsheetValueKind::Error)
                return {failure, {}, {}};
            if (name == "AVERAGE" && !count && read_)
                return {error("#DIV/0!"), {}, {}};
            double result = total;
            if (name == "COUNT")
                result = static_cast<double>(count);
            if (name == "MIN")
                result = minimum;
            if (name == "MAX")
                result = maximum;
            if (name == "AVERAGE" && count)
                result = total / count;
            return {number(result), {}, {}};
        }

        Operand primary()
        {
            const auto c = peek();
            if (c == '+' || c == '-')
            {
                ++position_;
                if (++depth_ > 64)
                    throw InvalidFormula{};
                auto value = primary();
                --depth_;
                return operation({number(0), {}, {}}, std::move(value), c);
            }
            if (take('('))
            {
                auto value = expression();
                if (!take(')'))
                    throw InvalidFormula{};
                return value;
            }
            if (text_.compare(position_, 5, "#REF!") == 0)
            {
                position_ += 5;
                return {error("#REF!"), {}, {}};
            }
            const auto start = position_;
            std::string sheet;
            if (take('\''))
            {
                bool closed = false;
                while (position_ < text_.size())
                {
                    auto character = text_[position_++];
                    if (character == '\'')
                    {
                        if (position_ < text_.size() && text_[position_] == '\'')
                            ++position_;
                        else
                        {
                            closed = true;
                            break;
                        }
                    }
                    sheet += character;
                }
                if (!closed || sheet.empty() || !take('!'))
                    throw InvalidFormula{};
            }
            else
            {
                while (position_ < text_.size() &&
                    (letter(text_[position_]) || digit(text_[position_]) || text_[position_] == '_' ||
                        static_cast<unsigned char>(text_[position_]) >= 128))
                    ++position_;
                auto name = text_.substr(start, position_ - start);
                if (!name.empty() && take('!'))
                    sheet = name;
                else if (!name.empty() && letter(name.front()) && take('('))
                    return function(upper(name));
                else
                    position_ = start;
            }
            if (sheet.empty() && (digit(peek()) || peek() == '.'))
            {
                while (position_ < text_.size() && (digit(text_[position_]) || text_[position_] == '.'))
                    ++position_;
                if (position_ < text_.size() && (text_[position_] == 'e' || text_[position_] == 'E'))
                {
                    ++position_;
                    if (position_ < text_.size() && (text_[position_] == '+' || text_[position_] == '-'))
                        ++position_;
                    while (position_ < text_.size() && digit(text_[position_]))
                        ++position_;
                }
                SpreadsheetValue value{SpreadsheetValueKind::Number, text_.substr(start, position_ - start)};
                try
                {
                    numeric(value);
                }
                catch (const SpreadsheetValue&)
                {
                    throw InvalidFormula{};
                }
                return {value, {}, {}};
            }
            const auto first = reference();
            const auto first_reference = references.back();
            const bool is_range = take(':');
            const auto last = is_range ? reference() : first;
            groups.push_back({start, position_, sheet, first_reference, references.back(), is_range});
            SpreadsheetRange r{{std::min(first.row, last.row), std::min(first.column, last.column)},
                {std::max(first.row, last.row), std::max(first.column, last.column)}};
            return {{}, r, sheet};
        }
    };

    struct Calculation
    {
        const SpreadsheetDocument& document;
        std::map<Key, SpreadsheetValue> cache;
        std::set<Key> active;
        std::size_t work = 0;
        std::size_t root_work = 0;
        std::size_t work_limit = 200000;
        bool exhausted = false;
        bool root_exhausted = false;

        void consume(std::size_t amount)
        {
            if (amount > work_limit - work)
            {
                exhausted = true;
                throw error("#NUM!");
            }
            work += amount;
            if (work - root_work > 200000)
            {
                root_exhausted = true;
                throw error("#NUM!");
            }
        }

        SpreadsheetValue cell(std::size_t sheet, SpreadsheetAddress address)
        {
            consume(1);
            if (active.size() >= 64)
            {
                root_exhausted = true;
                throw error("#NUM!");
            }
            const Key key{sheet, address.row, address.column};
            if (auto found = cache.find(key); found != cache.end())
                return found->second;
            if (active.count(key))
                return error("#REF!");
            const auto source = spreadsheet_source_value(document, sheet, address);
            if (source.kind != SpreadsheetValueKind::Formula)
                return source;
            const auto patches = document.edits.find(sheet);
            const auto original = document.sheets[sheet].cells.find(address);
            if ((patches == document.edits.end() || !patches->second.count(address)) &&
                original != document.sheets[sheet].cells.end() && !original->second.formula_supported)
                return error("#N/A");
            active.insert(key);
            SpreadsheetValue value;
            try
            {
                consume(source.text.size());
                value = Parser(source.text, [&](const std::string& name, SpreadsheetAddress target)
                {
                    auto index = sheet;
                    if (!name.empty())
                    {
                        auto found =
                            std::find_if(document.sheets.begin(), document.sheets.end(), [&](const auto& item)
                        {
                            return upper(item.name) == upper(name);
                        });
                        if (found == document.sheets.end())
                            return error("#REF!");
                        index = static_cast<std::size_t>(found - document.sheets.begin());
                    }
                    return cell(index, target);
                }).run();
            }
            catch (const InvalidFormula&)
            {
                value = error("#N/A");
            }
            catch (const SpreadsheetValue& failure)
            {
                value = failure;
            }
            active.erase(key);
            if (!root_exhausted && !exhausted)
                cache.emplace(key, value);
            return value;
        }
    };
}

namespace mirrorfly
{
    SpreadsheetValue spreadsheet_source_value(
        const SpreadsheetDocument& document, std::size_t sheet, SpreadsheetAddress address)
    {
        if (sheet >= document.sheets.size() || address.row >= maximum_spreadsheet_rows ||
            address.column >= maximum_spreadsheet_columns)
            return error("#REF!");
        auto edits = document.edits.find(sheet);
        if (edits != document.edits.end())
            if (auto edit = edits->second.find(address); edit != edits->second.end())
                return edit->second;
        auto found = document.sheets[sheet].cells.find(address);
        if (found == document.sheets[sheet].cells.end())
            return {};
        if (found->second.formula_cell)
            return {SpreadsheetValueKind::Formula, found->second.formula};
        return found->second.value;
    }

    bool spreadsheet_formula_supported(const std::string& formula)
    {
        try
        {
            Parser(formula).run();
            return true;
        }
        catch (const InvalidFormula&)
        {
            return false;
        }
    }

    SpreadsheetValue spreadsheet_calculate(
        const SpreadsheetDocument& document, std::size_t sheet, SpreadsheetAddress address)
    {
        try
        {
            return Calculation{document, {}, {}, 0}.cell(sheet, address);
        }
        catch (const SpreadsheetValue& failure)
        {
            return failure;
        }
    }

    SpreadsheetCalculation spreadsheet_calculate_all(const SpreadsheetDocument& document)
    {
        SpreadsheetCalculation result;
        Calculation calculation{document};
        calculation.work_limit = 2000000;
        for (std::size_t sheet = 0; sheet < document.sheets.size(); ++sheet)
        {
            std::set<SpreadsheetAddress> addresses;
            for (const auto& [address, cell] : document.sheets[sheet].cells)
                if (cell.formula_supported)
                    addresses.insert(address);
            const auto edits = document.edits.find(sheet);
            if (edits != document.edits.end())
                for (const auto& [address, value] : edits->second)
                    if (value.kind == SpreadsheetValueKind::Formula)
                        addresses.insert(address);
            for (const auto& address : addresses)
            {
                if (spreadsheet_source_value(document, sheet, address).kind != SpreadsheetValueKind::Formula)
                    continue;
                calculation.root_work = calculation.work;
                calculation.root_exhausted = false;
                SpreadsheetValue value;
                try
                {
                    value = calculation.cell(sheet, address);
                }
                catch (const SpreadsheetValue& failure)
                {
                    value = failure;
                }
                if (calculation.exhausted)
                {
                    result.complete = false;
                    result.work = calculation.work;
                    return result;
                }
                result.values[sheet][address] = std::move(value);
            }
        }
        result.work = calculation.work;
        return result;
    }

    std::optional<std::string> spreadsheet_translate_formula(
        const std::string& formula, int row_offset, int column_offset)
    {
        try
        {
            Parser parser(formula);
            parser.run();
            auto result = formula;
            for (auto item = parser.references.rbegin(); item != parser.references.rend(); ++item)
            {
                auto row = std::int64_t(item->address.row) + (item->fixed_row ? 0 : row_offset);
                auto column = std::int64_t(item->address.column) + (item->fixed_column ? 0 : column_offset);
                std::string replacement = "#REF!";
                if (row >= 0 && row < maximum_spreadsheet_rows && column >= 0 &&
                    column < maximum_spreadsheet_columns)
                {
                    replacement = spreadsheet_address(
                        {static_cast<std::uint32_t>(row), static_cast<std::uint32_t>(column)});
                    if (item->fixed_row)
                        replacement.insert(replacement.find_first_of("0123456789"), "$");
                    if (item->fixed_column)
                        replacement.insert(0, "$");
                }
                result.replace(item->start, item->end - item->start, replacement);
            }
            // A range with an invalid end cannot be safely copied as a new supported formula.
            if (!spreadsheet_formula_supported(result))
                return std::string("#REF!");
            return result;
        }
        catch (const InvalidFormula&)
        {
            return std::nullopt;
        }
    }

    std::optional<std::string> spreadsheet_rewrite_formula(const std::string& formula,
        const std::string& source_sheet, const std::string& result_sheet,
        const SpreadsheetReferenceChange& change)
    {
        if (source_sheet.empty() || result_sheet.empty() || change.sheet.empty())
            return {};
        if (change.action != SpreadsheetReferenceAction::Insert &&
            change.action != SpreadsheetReferenceAction::Erase &&
            change.action != SpreadsheetReferenceAction::RenameSheet &&
            change.action != SpreadsheetReferenceAction::DeleteSheet &&
            change.action != SpreadsheetReferenceAction::Move)
            return {};
        if ((change.action == SpreadsheetReferenceAction::Insert ||
                change.action == SpreadsheetReferenceAction::Erase) &&
            (!change.count ||
                std::uint64_t(change.index) + change.count >
                    (change.column ? maximum_spreadsheet_columns : maximum_spreadsheet_rows)))
            return {};
        if ((change.action == SpreadsheetReferenceAction::RenameSheet ||
                change.action == SpreadsheetReferenceAction::Move) &&
            change.target_sheet.empty())
            return {};
        if (change.action == SpreadsheetReferenceAction::Move &&
            (change.range.first.row > change.range.last.row ||
                change.range.first.column > change.range.last.column ||
                change.range.last.row >= maximum_spreadsheet_rows ||
                change.range.last.column >= maximum_spreadsheet_columns ||
                std::uint64_t(change.destination.row) + change.range.last.row - change.range.first.row >=
                    maximum_spreadsheet_rows ||
                std::uint64_t(change.destination.column) + change.range.last.column -
                        change.range.first.column >=
                    maximum_spreadsheet_columns))
            return {};
        try
        {
            Parser parser(formula);
            parser.run();
            auto result = formula;
            for (auto item = parser.groups.rbegin(); item != parser.groups.rend(); ++item)
            {
                auto first = item->first.address;
                auto last = item->last.address;
                auto target_sheet = item->sheet.empty() ? source_sheet : item->sheet;
                bool valid = true;
                if (upper(target_sheet) == upper(change.sheet))
                {
                    switch (change.action)
                    {
                    case SpreadsheetReferenceAction::DeleteSheet:
                        valid = false;
                        break;
                    case SpreadsheetReferenceAction::RenameSheet:
                        target_sheet = change.target_sheet;
                        break;
                    case SpreadsheetReferenceAction::Insert:
                    case SpreadsheetReferenceAction::Erase:
                    {
                        auto& a = change.column ? first.column : first.row;
                        auto& b = change.column ? last.column : last.row;
                        auto low = std::int64_t(std::min(a, b));
                        auto high = std::int64_t(std::max(a, b));
                        if (change.action == SpreadsheetReferenceAction::Insert)
                        {
                            if (low >= change.index)
                                low += change.count;
                            if (high >= change.index)
                                high += change.count;
                        }
                        else
                        {
                            const auto end = std::int64_t(change.index) + change.count - 1;
                            if (low >= change.index && low <= end)
                                low = end + 1;
                            if (high >= change.index && high <= end)
                                high = std::int64_t(change.index) - 1;
                            if (low > high)
                                valid = false;
                            if (low > end)
                                low -= change.count;
                            if (high > end)
                                high -= change.count;
                        }
                        const auto limit =
                            change.column ? maximum_spreadsheet_columns : maximum_spreadsheet_rows;
                        valid = valid && low >= 0 && high < limit;
                        if (valid)
                        {
                            const bool forward = a <= b;
                            a = static_cast<std::uint32_t>(forward ? low : high);
                            b = static_cast<std::uint32_t>(forward ? high : low);
                        }
                        break;
                    }
                    case SpreadsheetReferenceAction::Move:
                    {
                        const auto& r = change.range;
                        const bool inside = std::min(first.row, last.row) >= r.first.row &&
                            std::max(first.row, last.row) <= r.last.row &&
                            std::min(first.column, last.column) >= r.first.column &&
                            std::max(first.column, last.column) <= r.last.column;
                        if (inside)
                        {
                            first.row = first.row - r.first.row + change.destination.row;
                            last.row = last.row - r.first.row + change.destination.row;
                            first.column = first.column - r.first.column + change.destination.column;
                            last.column = last.column - r.first.column + change.destination.column;
                            target_sheet = change.target_sheet;
                        }
                        break;
                    }
                    default:
                        return {};
                    }
                }
                const auto reference_text = [](SpreadsheetAddress address, const Reference& original)
                {
                    auto text = spreadsheet_address(address);
                    if (original.fixed_row)
                        text.insert(text.find_first_of("0123456789"), "$");
                    if (original.fixed_column)
                        text.insert(0, "$");
                    return text;
                };
                std::string replacement = "#REF!";
                if (valid)
                {
                    replacement = reference_text(first, item->first);
                    if (item->range)
                        replacement += ':' + reference_text(last, item->last);
                    if (!item->sheet.empty() || upper(target_sheet) != upper(result_sheet))
                    {
                        std::string quoted = "'";
                        for (auto c : target_sheet)
                        {
                            quoted += c;
                            if (c == '\'')
                                quoted += c;
                        }
                        replacement = quoted + "'!" + replacement;
                    }
                }
                result.replace(item->start, item->end - item->start, replacement);
            }
            if (!spreadsheet_formula_supported(result))
                return {};
            return result;
        }
        catch (const InvalidFormula&)
        {
            return {};
        }
    }
}
