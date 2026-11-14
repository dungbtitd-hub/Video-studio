// Where the preview and the editing controls go on phones, tablets and foldables
// (FR-027 tablet mode; Galaxy Z Fold 6/7/8, foldable iPhone). Pure geometry, so it is
// unit-tested without a device.
import 'dart:ui' show DisplayFeature, DisplayFeatureType, Rect, Size;

enum WorkspaceKind {
  /// Narrow screen: preview on top, controls and timeline below.
  phone,

  /// Wide screen or a vertical fold: preview left ([WorkspaceLayout.split] wide), controls right.
  sideBySide,

  /// Half-folded with a horizontal fold (on a table): preview above the fold, controls below.
  tabletop,
}

class WorkspaceLayout {
  const WorkspaceLayout(this.kind, {this.split = 0, this.gap = 0});
  final WorkspaceKind kind;

  /// Width (side by side) or height (tabletop) of the preview pane.
  final double split;

  /// Space left empty over the hinge.
  final double gap;
}

/// Wide enough for two panes: 840 dp is Material's "expanded" width class.
const double kWideWorkspace = 840;

/// [size] is the body's size; [features] the screen's display features (in global
/// coordinates); [topInset] the height above the body (status bar + app bar), to bring the
/// hinge into body coordinates.
WorkspaceLayout workspaceLayoutFor(Size size, List<DisplayFeature> features, double topInset) {
  for (final f in features) {
    if (f.type != DisplayFeatureType.hinge && f.type != DisplayFeatureType.fold) continue;
    final Rect b = f.bounds;
    final vertical = b.height >= b.width;
    if (vertical && b.left > size.width * 0.2 && b.right < size.width * 0.8) {
      // Book posture or unfolded flat: one pane per side of the fold.
      return WorkspaceLayout(WorkspaceKind.sideBySide, split: b.left, gap: b.width);
    }
    final top = b.top - topInset;
    if (!vertical && top > size.height * 0.25 && top < size.height * 0.75) {
      return WorkspaceLayout(WorkspaceKind.tabletop, split: top, gap: b.height);
    }
  }
  if (size.width >= kWideWorkspace && size.width > size.height) {
    // Tablet in landscape (or an unfolded foldable without a reported hinge): preview keeps 58 %.
    return WorkspaceLayout(WorkspaceKind.sideBySide, split: (size.width * 0.58).roundToDouble());
  }
  if (size.width >= 600 && size.height < 520) {
    // Phone in landscape: side by side too, or the preview would be a thin strip.
    return WorkspaceLayout(WorkspaceKind.sideBySide, split: (size.width * 0.5).roundToDouble());
  }
  return const WorkspaceLayout(WorkspaceKind.phone);
}
