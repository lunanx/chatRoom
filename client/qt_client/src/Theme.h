#pragma once

#include <QColor>
#include <QString>

// Go聊主题 B：紫灰沉浸。
// 颜色与尺寸对应 qt_chat_ui_theme_b_custom.html 中的 :root CSS 变量。
namespace Theme
{
    // 品牌色
    inline const QColor Purple{0x6A, 0x53, 0xF6};             // --purple: #6A53F6
    inline const QColor PurpleDeep{0x59, 0x41, 0xE7};         // --purple-deep: #5941E7
    inline const QColor PurpleSoft{0xEC, 0xE6, 0xFF};         // --purple-soft: #ECE6FF
    inline const QColor PurpleHalo{174, 159, 255, 59};       // --purple-halo, alpha .23
    inline const QColor PurpleHaloSoft{201, 190, 255, 26};    // --purple-halo-soft, alpha .10
    inline const QColor ChatGlow{179, 161, 255, 46};         // --chat-glow, alpha .18

    // 背景与表面
    inline const QColor Page{0xF5, 0xF5, 0xFA};               // --page
    inline const QColor Surface{0xFC, 0xFB, 0xFF};            // --surface
    inline const QColor SurfaceStrong{0xFF, 0xFF, 0xFF};     // --surface-strong
    inline const QColor Nav{0xF0, 0xED, 0xFA};                // --nav
    inline const QColor NavTop{0xF3, 0xF0, 0xFC};             // --nav-top
    inline const QColor NavBottom{0xEA, 0xE5, 0xF8};          // --nav-bottom
    inline const QColor Rail{0xFD, 0xFC, 0xFF};               // --rail
    inline const QColor ChatBackground{0xF5, 0xF2, 0xFB};     // --chat-bg
    inline const QColor White{0xFF, 0xFF, 0xFF};              // --white
    inline const QColor White90{255, 255, 255, 230};          // --white-90

    // 文字颜色
    inline const QColor Ink{0x30, 0x2A, 0x48};                // --ink
    inline const QColor InkSoft{0x77, 0x70, 0x8F};            // --ink-soft
    inline const QColor Muted{0xA3, 0x9B, 0xB9};              // --muted
    inline const QColor Placeholder{0xAA, 0xA3, 0xBD};        // --placeholder
    inline const QColor IncomingText{0x4C, 0x45, 0x66};       // --incoming-text

    // 线条、阴影与交互状态
    inline const QColor Line{88, 70, 130, 26};                // --line, alpha .10
    inline const QColor FrameLine{109, 88, 172, 41};          // --frame-line, alpha .16
    inline const QColor BorderSoft{109, 88, 172, 31};         // --border-soft, alpha .12
    inline const QColor PurpleLineSoft{106, 83, 246, 31};     // --purple-line-soft, alpha .12
    inline const QColor PurpleLineMedium{106, 83, 246, 51};   // --purple-line-medium, alpha .20
    inline const QColor PurpleBorderStrong{106, 83, 246, 71}; // --purple-border-strong, alpha .28
    inline const QColor PurpleFocus{106, 83, 246, 148};       // --purple-focus, alpha .58
    inline const QColor PurpleFocusRing{106, 83, 246, 23};    // --purple-focus-ring, alpha .09
    inline const QColor PurpleShadowStrong{106, 83, 246, 69}; // --purple-shadow-strong, alpha .27
    inline const QColor PurpleShadowMedium{106, 83, 246, 56}; // --purple-shadow-medium, alpha .22
    inline const QColor PurpleShadowSoft{106, 83, 246, 36};   // --purple-shadow-soft, alpha .14
    inline const QColor PurpleShadowTint{106, 83, 246, 26};   // --purple-shadow-tint, alpha .10
    inline const QColor PurpleShadowFaint{106, 83, 246, 9};   // --purple-shadow-faint, alpha .035
    inline const QColor PurpleShadowFainter{106, 83, 246, 6}; // --purple-shadow-fainter, alpha .025
    inline const QColor PurpleShadowTwenty{106, 83, 246, 51}; // --purple-shadow-twenty, alpha .20
    inline const QColor ShadowInkSoft{89, 68, 153, 36};       // --shadow-ink-soft, alpha .14
    inline const QColor ShadowActive{90, 69, 164, 28};        // --shadow-active, alpha .11
    inline const QColor ShadowConversation{103, 80, 186, 17}; // --shadow-conversation, alpha .065
    inline const QColor ShadowAvatar{62, 50, 112, 26};        // --shadow-avatar, alpha .10
    inline const QColor BubbleBorder{104, 84, 162, 9};        // --bubble-border, alpha .035
    inline const QColor InputShadow{93, 70, 150, 15};         // --input-shadow, alpha .06

    // 气泡、状态与输入控件
    inline const QColor Incoming{0xFF, 0xFF, 0xFF};            // --incoming
    inline const QColor Outgoing{0x6A, 0x53, 0xF6};            // --outgoing
    inline const QColor HeaderBackground{255, 255, 255, 199}; // --header-bg, alpha .78
    inline const QColor ComposerBackground{255, 255, 255, 209}; // --composer-bg, alpha .82
    inline const QColor SearchBackground{0xF5, 0xF2, 0xFB};   // --search-bg
    inline const QColor FilterActive{0xEC, 0xE6, 0xFF};        // --filter-active
    inline const QColor ConversationActive{0xF0, 0xEB, 0xFF}; // --convo-active
    inline const QColor DayBackground{0xEB, 0xE5, 0xF8};      // --day-bg
    inline const QColor ControlBackground{106, 83, 246, 20};  // --control-bg, alpha .075
    inline const QColor ControlHover{106, 83, 246, 36};       // --control-hover, alpha .14
    inline const QColor Green{0x50, 0xB9, 0x94};              // --green
    inline const QColor GreenSoft{0xE1, 0xF6, 0xEE};          // --green-soft
    inline const QColor CloseHover{0xE9, 0x69, 0x83};         // --close-hover

    // HTML 渐变的端点色（Qt 端使用 QLinearGradient / QRadialGradient 还原）
    inline const QColor BrandGradientStart{0x80, 0x6B, 0xFF}; // --brand-gradient 0%
    inline const QColor BrandGradientMiddle{0x6A, 0x53, 0xF6}; // --brand-gradient 52%
    inline const QColor BrandGradientEnd{0x92, 0x7B, 0xFF};   // --brand-gradient 100%
    inline const QColor AuthGradientStart{0xF1, 0xED, 0xFF};  // --auth-brand-gradient
    inline const QColor AuthGradientMiddle{0xE9, 0xE3, 0xFB};
    inline const QColor AuthGradientEnd{0xF8, 0xF5, 0xFF};
    inline const QColor PeachStart{0xFF, 0xD0, 0xBB};         // --peach-start
    inline const QColor PeachEnd{0xEF, 0x9A, 0x9F};           // --peach-end
    inline const QColor MintStart{0xA9, 0xE6, 0xD5};          // --mint-start
    inline const QColor MintEnd{0x5D, 0xBB, 0xA9};            // --mint-end
    inline const QColor BlueStart{0xB4, 0xD9, 0xFF};          // --blue-start
    inline const QColor BlueEnd{0x77, 0x9D, 0xE8};            // --blue-end
    inline const QColor GoldStart{0xF9, 0xD6, 0x8A};          // --gold-start
    inline const QColor GoldEnd{0xE8, 0xA3, 0x5D};            // --gold-end
    inline const QColor DarkStart{0xA3, 0x9B, 0xEF};          // --dark-start
    inline const QColor DarkEnd{0x55, 0x50, 0xA6};            // --dark-end

    // CSS 阴影的颜色；Qt 绘制阴影时可与 QGraphicsDropShadowEffect 配合使用。
    inline const QColor WindowShadowColor{89, 68, 153, 36};   // window-shadow alpha .14
    inline const QColor PanelShadowColor{89, 68, 153, 31};    // panel-shadow alpha .12
    inline const QColor BubbleShadowColor{93, 70, 150, 19};   // bubble-shadow alpha .075
    inline constexpr int WindowShadowOffsetY = 24;
    inline constexpr int WindowShadowBlur = 70;
    inline constexpr int PanelShadowOffsetY = 18;
    inline constexpr int PanelShadowBlur = 56;
    inline constexpr int BubbleShadowOffsetY = 7;
    inline constexpr int BubbleShadowBlur = 22;

    // 布局尺寸（CSS 像素对应 Qt 逻辑像素）
    inline constexpr int NavigationWidth = 56;
    inline constexpr int ConversationWidth = 240;
    inline constexpr int ChatHeaderHeight = 48;
    inline constexpr int AvatarSize = 40;
    inline constexpr int MainWindowRadius = 26;
    inline constexpr int AuthCardRadius = 32;
    inline constexpr int BrandMarkRadius = 10;
    inline constexpr int ButtonRadius = 15;
    inline constexpr int InputRadius = 15;
    inline constexpr int BubbleRadius = 29;
    inline constexpr int PanelSpacing = 12;
    inline constexpr int PagePadding = 28;
    inline constexpr int WindowMinimumWidth = 900;
    inline constexpr int WindowMinimumHeight = 600;

    inline const QString FontFamily = QStringLiteral("Inter, SF Pro Display, Segoe UI, Microsoft YaHei, sans-serif");
}
