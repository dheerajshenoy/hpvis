#pragma once

#include <string_view>

// Every command the user can bind a key to. The enumerator is what the code
// refers to; where the command sits in the menus does not matter. Moving or
// relabelling a menu item never touches its key binding.
enum class Command
{
    None, // not bindable

    // File
    FileOpen,
    FileOpenRecent,
    FileReopenSession,
    FileHeader,
    FileSaveMap,
    FileSaveMapWithMask,
    FileExport,
    FileCloseTab,
    FileExit,

    // Edit
    EditCopyImage,

    // View
    ViewFileExplorer,
    ViewMinimap,
    ViewOrientationDefault,
    ViewOrientationGalactic,
    ViewOrientationEcliptic,
    ViewOrientationEquatorial,
    ViewPerspectiveInside,
    ViewPerspectiveOutside,
    ViewPixelOrderingNested,
    ViewPixelOrderingRing,
    ViewZoomIn,
    ViewZoomOut,
    ViewResetZoom,
    ViewZoomToRegion,
    ViewZoom10,
    ViewZoom25,
    ViewZoom50,
    ViewZoom100,
    ViewZoom200,
    ViewZoom400,
    ViewZoom800,
    ViewZoom1600,
    ViewZoom3200,
    ViewZoom6400,
    ViewZoom12800,
    ViewZoom25600,
    ViewZoom51200,
    ViewPanLeft,
    ViewPanRight,
    ViewPanUp,
    ViewPanDown,
    ViewSetRotation,
    ViewResetRotation,
    ViewCenterNorthPole,
    ViewCenterSouthPole,
    ViewCenterEquator,
    ViewRecenterRightClick,
    ViewLockRoll,
    ViewResetView,
    ViewBackgroundColor,
    ViewUnseenColor,
    ViewGraticuleShow,
    ViewPixelGridShow,
    ViewPixelGridNumbers,
    ViewRegionRectangle,
    ViewRegionEllipse,
    ViewRegionPolygon,
    ViewRegionLine,
    ViewRegionClear,

    // Colormap
    ColormapLoad,
    ColormapOpenFolder,
    ColormapReverse,
    ColorBarShow,
    ColorBarLinear,
    ColorBarLog,
    ColorBarAsinh,
    ColorBarRange,
    ColorBarResetRange,
    ColorBarPercentile,

    // Masks
    MaskAdd,
    MaskAddFromColumn,
    MaskClear,
    MaskDimStrength,

    // Tools
    ToolsOperate,
    ToolsResolution,
    ToolsSmooth,
    ToolsMultipoleFilter,
    ToolsHarmonic,
    ToolsUnits,
    ToolsTransform,
    ToolsRandomMap,
    ToolsPowerSpectrum,
    ToolsStatistics,
    ToolsMaskBuilder,
    ToolsSettings,

    // Help
    HelpUsage,
    HelpAbout,

    // Keys read by the viewer rather than by a menu item
    RegionFinish,
    RegionCancel,

    Count_ // not a command
};

// The name a binding is stored under in the settings file. These must never
// change once released, whatever happens to the enumerators or the menus
// (the names are the menu paths the commands had when keys were first
// stored). No default: the compiler warns when a command has no name.
constexpr std::string_view
storedName(Command c)
{
    switch (c)
    {
    case Command::None:
    case Command::Count_: return {};

    case Command::FileOpen: return "file/open";
    case Command::FileOpenRecent: return "file/open-recent";
    case Command::FileReopenSession: return "file/reopen-last-session";
    case Command::FileHeader: return "file/fits-header";
    case Command::FileSaveMap: return "file/save-map-as";
    case Command::FileSaveMapWithMask: return "file/save-map-with-mask";
    case Command::FileExport: return "file/export";
    case Command::FileCloseTab: return "file/close-tab";
    case Command::FileExit: return "file/exit";

    case Command::EditCopyImage: return "edit/copy-image";

    case Command::ViewFileExplorer: return "view/file-explorer";
    case Command::ViewMinimap: return "view/minimap";
    case Command::ViewOrientationDefault: return "view/orientation/default";
    case Command::ViewOrientationGalactic: return "view/orientation/galactic";
    case Command::ViewOrientationEcliptic: return "view/orientation/ecliptic";
    case Command::ViewOrientationEquatorial:
        return "view/orientation/equatorial";
    case Command::ViewPerspectiveInside: return "view/perspective/inside-sky";
    case Command::ViewPerspectiveOutside:
        return "view/perspective/outside-globe";
    case Command::ViewPixelOrderingNested: return "view/pixel-ordering/nested";
    case Command::ViewPixelOrderingRing: return "view/pixel-ordering/ring";
    case Command::ViewZoomIn: return "view/zoom/zoom-in";
    case Command::ViewZoomOut: return "view/zoom/zoom-out";
    case Command::ViewResetZoom: return "view/zoom/reset-zoom";
    case Command::ViewZoomToRegion: return "view/zoom/zoom-to-region";
    case Command::ViewZoom10: return "view/zoom/10x";
    case Command::ViewZoom25: return "view/zoom/25x";
    case Command::ViewZoom50: return "view/zoom/50x";
    case Command::ViewZoom100: return "view/zoom/100x";
    case Command::ViewZoom200: return "view/zoom/200x";
    case Command::ViewZoom400: return "view/zoom/400x";
    case Command::ViewZoom800: return "view/zoom/800x";
    case Command::ViewZoom1600: return "view/zoom/1600x";
    case Command::ViewZoom3200: return "view/zoom/3200x";
    case Command::ViewZoom6400: return "view/zoom/6400x";
    case Command::ViewZoom12800: return "view/zoom/12800x";
    case Command::ViewZoom25600: return "view/zoom/25600x";
    case Command::ViewZoom51200: return "view/zoom/51200x";
    case Command::ViewPanLeft: return "view/pan/left";
    case Command::ViewPanRight: return "view/pan/right";
    case Command::ViewPanUp: return "view/pan/up";
    case Command::ViewPanDown: return "view/pan/down";
    case Command::ViewSetRotation: return "view/set-rotation";
    case Command::ViewResetRotation: return "view/reset-rotation";
    case Command::ViewCenterNorthPole:
        return "view/rotation/center-on/north-pole";
    case Command::ViewCenterSouthPole:
        return "view/rotation/center-on/south-pole";
    case Command::ViewCenterEquator:
        return "view/rotation/center-on/equator";
    case Command::ViewRecenterRightClick:
        return "view/rotation/recenter-on-right-click";
    case Command::ViewLockRoll: return "view/rotation/lock-roll-when-recentering";
    case Command::ViewResetView: return "view/reset-view";
    case Command::ViewBackgroundColor: return "view/background-color";
    case Command::ViewUnseenColor: return "view/unseen-pixel-color";
    case Command::ViewGraticuleShow: return "view/graticule/show-graticule";
    case Command::ViewPixelGridShow: return "view/pixel-grid/show-pixel-grid";
    case Command::ViewPixelGridNumbers:
        return "view/pixel-grid/show-pixel-numbers";
    case Command::ViewRegionRectangle:
        return "view/select-statistics-region/rectangle";
    case Command::ViewRegionEllipse:
        return "view/select-statistics-region/ellipse";
    case Command::ViewRegionPolygon:
        return "view/select-statistics-region/"
               "polygon-double-click-or-enter-to-close";
    case Command::ViewRegionLine:
        return "view/select-statistics-region/line-great-circle";
    case Command::ViewRegionClear: return "view/clear-statistics-region";

    case Command::ColormapLoad: return "view/custom-colormaps/load-colormap";
    case Command::ColormapOpenFolder:
        return "view/custom-colormaps/open-colormaps-folder";
    case Command::ColormapReverse: return "view/reverse-colormap";
    case Command::ColorBarShow: return "view/color-bar/show-color-bar";
    case Command::ColorBarLinear: return "view/color-bar/linear";
    case Command::ColorBarLog: return "view/color-bar/logarithmic";
    case Command::ColorBarAsinh: return "view/color-bar/asinh";
    case Command::ColorBarRange: return "view/color-bar/range";
    case Command::ColorBarResetRange: return "view/color-bar/reset-range";
    case Command::ColorBarPercentile:
        return "view/color-bar/percentile-range";

    case Command::MaskAdd: return "masks/add-mask";
    case Command::MaskAddFromColumn: return "masks/add-mask-from-map-column";
    case Command::MaskClear: return "masks/clear-all";
    case Command::MaskDimStrength: return "masks/dim-strength";

    case Command::ToolsOperate: return "tools/operate-on-maps";
    case Command::ToolsResolution: return "tools/change-resolution";
    case Command::ToolsSmooth: return "tools/beam-smoothing";
    case Command::ToolsMultipoleFilter: return "tools/multipole-filter";
    case Command::ToolsHarmonic: return "tools/derivatives-and-e-b-maps";
    case Command::ToolsUnits: return "tools/maps/convert-units";
    case Command::ToolsTransform: return "tools/maps/data-transform";
    case Command::ToolsRandomMap: return "tools/maps/generate-random-map";
    case Command::ToolsPowerSpectrum: return "tools/analysis/power-spectrum";
    case Command::ToolsStatistics: return "tools/analysis/statistics";
    case Command::ToolsMaskBuilder: return "tools/analysis/mask-builder";
    case Command::ToolsSettings: return "tools/settings";

    case Command::HelpUsage: return "help/usage";
    case Command::HelpAbout: return "help/about";

    case Command::RegionFinish: return "region/finish-polygon";
    case Command::RegionCancel: return "region/cancel-selection";
    }
    return {};
}

namespace command_detail
{
// Every command has a name and no two share one (checked when compiling)
constexpr bool
namesValid()
{
    constexpr int n = int(Command::Count_);
    for (int i = 1; i < n; ++i)
    {
        if (storedName(Command(i)).empty())
            return false;
        for (int j = i + 1; j < n; ++j)
            if (storedName(Command(i)) == storedName(Command(j)))
                return false;
    }
    return true;
}
static_assert(namesValid(),
              "every Command needs a unique name in storedName()");
} // namespace command_detail
