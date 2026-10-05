import QtQuick

QtObject
{
    id: root
    property var theme
    readonly property var options:
    {
        const academic = {ink: theme.templateInk, paper: theme.templatePaper, card: theme.templateCard,
            muted: theme.templateMuted, accent: theme.templateAcademicAccent, soft: theme.templateAcademicSoft};
        const work = Object.assign({}, academic, {accent: theme.templateWorkAccent, soft: theme.templateWorkSoft});
        return {researchStudio: {layout: "researchStudio", palette: academic},
            evidenceBoard: {layout: "evidenceBoard", palette: academic},
            projectDashboard: {layout: "projectDashboard", palette: work},
            deliveryRoadmap: {layout: "deliveryRoadmap", palette: work}};
    }
}
