pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls

SpinBox
{
    id: root
    property real number: 0
    property real minimum: 0
    property real maximum: 144
    property real increment: 0.5
    signal numberModified(real number)

    from: Math.round(minimum * 100)
    to: Math.round(maximum * 100)
    stepSize: Math.round(increment * 100)
    value: Math.max(from, Math.min(to, Math.round(number * 100)))
    editable: true
    validator: DoubleValidator { bottom: root.minimum; top: root.maximum; decimals: 2 }
    textFromValue: function(value, locale) { return Number(value / 100).toLocaleString(locale, "f", 2); }
    valueFromText: function(text, locale)
    {
        const number = Number.fromLocaleString(locale, text);
        return isNaN(number) ? root.value : Math.round(number * 100);
    }
    onValueModified: numberModified(value / 100)
}
