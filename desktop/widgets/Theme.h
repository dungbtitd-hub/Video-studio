// Design tokens (design/tokens.json) for the Widgets prototype.
#pragma once

#include <QColor>
#include <QString>

namespace Theme {
inline QColor bg() { return QColor("#0f1115"); }
inline QColor panel() { return QColor("#171a20"); }
inline QColor panel2() { return QColor("#1d2128"); }
inline QColor raised() { return QColor("#252a33"); }
inline QColor line() { return QColor("#2c323c"); }
inline QColor lineSoft() { return QColor("#232830"); }
inline QColor fg() { return QColor("#e7e9ee"); }
inline QColor fg2() { return QColor("#a4abb8"); }
inline QColor fg3() { return QColor("#6e7685"); }
inline QColor accent() { return QColor("#f2a93b"); }
inline QColor accentInk() { return QColor("#1b1206"); }
inline QColor clipVideo() { return QColor("#4a6fa5"); }
inline QColor clipVideo2() { return QColor("#36557f"); }
inline QColor clipAudio() { return QColor("#2f9c8f"); }
inline QColor clipAudio2() { return QColor("#22766c"); }

inline QString styleSheet() {
    return QStringLiteral(R"(
      QMainWindow, QWidget { background: #0f1115; color: #e7e9ee; font-size: 13px; }
      QDockWidget { color: #6e7685; font-size: 11px; }
      QDockWidget::title { background: #171a20; padding: 6px 10px; text-transform: uppercase; }
      QToolBar { background: #171a20; border: none; spacing: 6px; padding: 6px 10px; }
      QToolButton, QPushButton { background: transparent; color: #e7e9ee; border: 1px solid #2c323c;
                                 border-radius: 6px; padding: 5px 10px; }
      QToolButton:hover, QPushButton:hover { background: #252a33; }
      QToolButton:disabled, QPushButton:disabled { color: #6e7685; }
      QToolButton#primary, QPushButton#primary { background: #f2a93b; color: #1b1206; border-color: #f2a93b; }
      QListWidget { background: #171a20; border: none; }
      QListWidget::item { background: #1d2128; border: 1px solid #232830; border-radius: 6px; margin: 3px; padding: 8px; }
      QListWidget::item:hover { background: #252a33; }
      QStatusBar { background: #171a20; color: #6e7685; font-size: 11px; }
      QScrollBar:horizontal { background: #171a20; height: 10px; }
      QScrollBar::handle:horizontal { background: #2c323c; border-radius: 4px; min-width: 30px; }
      QScrollBar::add-line, QScrollBar::sub-line { width: 0; }
      QMainWindow::separator { background: #0f1115; width: 3px; height: 3px; }
      QMainWindow::separator:hover { background: #f2a93b; }
    )");
}
}  // namespace Theme
