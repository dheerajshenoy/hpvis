import QtQuick
import QtGraphs

GraphsView {
    id: view

    // plotEll / plotDl are set by the dialog before this file is loaded
    property real xMax: 1
    property real yMax: 1

    axisX: ValueAxis {
        min: 2
        max: view.xMax
        titleText: "l"
    }

    axisY: ValueAxis {
        min: 0
        max: view.yMax
        titleText: "l(l+1)C_l / 2\u03c0"
    }

    LineSeries {
        id: series
    }

    Component.onCompleted: {
        for (let i = 0; i < plotEll.length; ++i) {
            series.append(plotEll[i], plotDl[i])
            xMax = Math.max(xMax, plotEll[i])
            yMax = Math.max(yMax, plotDl[i])
        }
        yMax *= 1.05
    }
}
