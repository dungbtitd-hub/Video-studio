import 'package:flutter/material.dart';

/// Design tokens shared with the desktop app (design/tokens.json).
class VsColors {
  static const bg = Color(0xFF0F1115);
  static const panel = Color(0xFF171A20);
  static const panel2 = Color(0xFF1D2128);
  static const raised = Color(0xFF252A33);
  static const line = Color(0xFF2C323C);
  static const fg = Color(0xFFE7E9EE);
  static const fg2 = Color(0xFFA4ABB8);
  static const fg3 = Color(0xFF6E7685);
  static const accent = Color(0xFFF2A93B);
  static const accentInk = Color(0xFF1B1206);
  static const clipVideo = Color(0xFF4A6FA5);
  static const clipVideo2 = Color(0xFF36557F);
  static const clipAudio = Color(0xFF2F9C8F);
  static const clipAudio2 = Color(0xFF22766C);
  static const bad = Color(0xFFE2665E);
}

ThemeData buildTheme() {
  final base = ThemeData.dark(useMaterial3: true);
  return base.copyWith(
    scaffoldBackgroundColor: VsColors.bg,
    colorScheme: base.colorScheme.copyWith(
      primary: VsColors.accent,
      onPrimary: VsColors.accentInk,
      surface: VsColors.panel,
      onSurface: VsColors.fg,
      error: VsColors.bad,
    ),
    appBarTheme: const AppBarTheme(backgroundColor: VsColors.panel, foregroundColor: VsColors.fg, elevation: 0),
  );
}
