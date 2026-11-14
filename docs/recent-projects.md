# Recent projects ("Tiếp tục dự án")

When the app opens, the projects that changed since their last export slide up in a strip at the
bottom of the screen: a small poster, the name, when it was last saved and its length. One click or
tap opens a project. The strip slides away (never pops) when a project is opened, when editing
starts, or with ✕. "Mở dự án khác…" (desktop) and "Tất cả" (mobile) open the full list.

## How it stays instant

Listing never opens media or parses a large project. Every save writes two small files into the
package, and a successful export writes a third:

| File | Written by | Holds |
|---|---|---|
| `card.json` | save | name, save time, length, clip count, canvas size, content hash |
| `poster.rgba` | save | the current preview frame, box-downscaled to 256 px (only when the frame changed) |
| `exported.json` | export | content hash of the exported project |

A project is unfinished when its content hash differs from the exported one (save timestamps are
not part of the hash, so saving the same content again keeps it "done"). Packages saved before cards
existed are still listed, read once from `project.json`.

## Where projects come from

* **Desktop:** a recent-list file (`recent.json` in the app data folder), updated on open and save,
  newest first, at most 30 entries; missing packages drop out. Right click a card to remove it.
* **Mobile:** every project lives in one folder, which is scanned.

## Code

* Core: `core/include/vstudio/recents.hpp` (`writeCard`, `markExported`, `readCard`, `readPoster`,
  `RecentList`, `startCards`).
* C API: `vs_recent_projects_json`, `vs_recent_touch`, `vs_recent_remove`, `vs_project_poster`;
  `vs_save` writes the card and poster, a finished export marks the project exported.
* Desktop: `desktop/widgets/RecentStrip.*`, `EditorController::recentProjects`.
* Mobile: `mobile/lib/ui/recent_strip.dart`, `VStudio.recentProjects` / `VStudio.projectPoster`.
