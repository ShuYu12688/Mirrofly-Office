#pragma once

#include <QStringList>
#include <QVector>
#include <mirrorfly/spreadsheet.hpp>

namespace mirrorfly
{
    bool spreadsheet_input_value(
        const QString& text, const QString& kind, SpreadsheetValue& value, QString& error);
    bool spreadsheet_paste_rows(const QString& text, QVector<QStringList>& rows, QString& error);
    QString spreadsheet_quote_cell(QString text);
}
