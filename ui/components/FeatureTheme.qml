import QtQuick

QtObject
{
    id: root

    property var base
    property string kind: "slides"
    readonly property var values:
    {
        if (!base)
        {
            return ({});
        }
        const result = Object.assign({}, base);
        const accent = base[kind + "Accent"];
        const surface = base[kind + "Surface"];
        result.accent = accent;
        result.accentHover = accent;
        result.accentSoft = surface;
        result.backgroundColor = base[kind + "Background"];
        result.gradientStart = accent;
        result.gradientMiddle = accent;
        result.gradientEnd = accent;
        result.gradientSoftStart = base.surfaceColor;
        result.gradientSoftMiddle = base.surfaceColor;
        result.gradientSoftEnd = surface;
        result.hoverColor = surface;
        return result;
    }
}
