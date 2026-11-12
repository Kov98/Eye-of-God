# Mapa token -> controle -> estado (VS 2026, tema escuro)

Varredura recursiva dos 7671 XAML do corpus; tokens cruzados com os 867 da paleta.

| metrica | valor |
|---|---|
| tokens distintos referenciados | **394** |
| com hex resolvido | **338** (85.8%) |
| sem hex na paleta | 56 |
| controles distintos | **172** |
| referencias totais | 4730 |

## Controles e estados (top 20 por uso)

### ?

| estado | token | hex |
|---|---|---|
| IsSelected=True | TextFillPrimaryBrushKey | FFFFFFFF |
| IsSelected=True | CommandBarMouseDownBackgroundBeginBrushKey | FF2E2E2E |
| IsSelected=True | CommandBarTextMouseDownBrushKey | FFFAFAFA |
| default | TextFillSecondaryBrushKey | C8FFFFFF |
| default | ToolWindowTextBrushKey | FFFAFAFA |
| default | TextFillPrimaryBrushKey | FFFFFFFF |
| IsHighlighted=true | ComboBoxMouseOverBackgroundGradientBrushKey | - |
| IsHighlighted=true | ComboBoxMouseOverBorderBrushKey | FF808080 |
| IsHighlighted=true | CommandBarTextHoverBrushKey | FFFAFAFA |
| IsEnabled=false | CommandBarTextInactiveBrushKey | FF5C5C5C |
| IsEnabled=false | CommandBarCheckBoxDisabledBrushKey | FF808080 |
| IsEnabled=false | ComboBoxDisabledGlyphBrushKey | FF808080 |
| IsHighlighted=True | ComboBoxMouseOverBackgroundGradientBrushKey | - |
| IsHighlighted=True | ComboBoxMouseOverBorderBrushKey | FF808080 |
| IsHighlighted=True | CommandBarTextHoverBrushKey | FFFAFAFA |
| IsEnabled=False | TextFillDisabledBrushKey | 5DFFFFFF |
| IsEnabled=False | CommandBarTextInactiveBrushKey | FF5C5C5C |
| IsEnabled=False | ComboBoxDisabledGlyphBrushKey | FF808080 |
| IsEnabled=True | ComboBoxButtonMouseOverSeparatorBrushKey | 00000000 |
| IsEnabled=True | ComboBoxButtonMouseOverBackgroundBrushKey | 00000000 |
| IsEnabled=True | ComboBoxMouseOverGlyphBrushKey | FFD6D6D6 |

### ComboBox

| estado | token | hex |
|---|---|---|
| default | DropDownTextBrushKey | FFFAFAFA |
| default | DropDownBorderBrushKey | FF424242 |
| default | DropDownBackgroundBrushKey | FF383838 |
| IsKeyboardFocusWithin=True | DropDownMouseOverTextBrushKey | FFFAFAFA |
| IsKeyboardFocusWithin=True | DropDownButtonMouseOverSeparatorBrushKey | 00000000 |
| IsKeyboardFocusWithin=True | DropDownButtonMouseOverBackgroundBrushKey | FF3D3D3D |
| IsMouseOver=True | DropDownMouseOverTextBrushKey | FFFAFAFA |
| IsMouseOver=True | DropDownButtonMouseOverSeparatorBrushKey | 00000000 |
| IsMouseOver=True | DropDownButtonMouseOverBackgroundBrushKey | FF3D3D3D |
| IsDropDownOpen=True | DropDownMouseDownBackgroundBrushKey | FF3D3D3D |
| IsDropDownOpen=True | DropDownMouseDownBorderBrushKey | FF707070 |
| IsDropDownOpen=True | DropDownMouseDownTextBrushKey | FFFAFAFA |
| IsEnabled=False | DropDownDisabledTextBrushKey | FF5C5C5C |
| IsEnabled=False | DropDownDisabledBorderBrushKey | FF3D3D3D |
| IsEnabled=False | DropDownDisabledBackgroundBrushKey | FF333333 |
| IsEditable=True | TextFillPrimaryBrushKey | FFFFFFFF |
| IsEditable=True | ComboBoxTextBrushKey | FFFAFAFA |
| IsKeyboardFocusWithin=true | DropDownButtonMouseOverSeparatorBrushKey | 00000000 |
| IsKeyboardFocusWithin=true | DropDownButtonMouseOverBackgroundBrushKey | FF3D3D3D |
| IsKeyboardFocusWithin=true | DropDownMouseOverGlyphBrushKey | FFD6D6D6 |

### Button

| estado | token | hex |
|---|---|---|
| default | SubtleFillTertiaryBrushKey | 0BFFFFFF |
| default | AccentFillDisabledBrushKey | 28FFFFFF |
| default | AccentFillSecondaryBrushKey | E59184EE |
| IsMouseOver=True | CommandBarMenuItemMouseOverBrushKey | FFFAFAFA |
| IsMouseOver=True | CommandBarBorderBrushKey | FF707070 |
| IsMouseOver=True | CommandBarTextHoverBrushKey | FFFAFAFA |
| IsPressed=True | SubtleFillTertiaryBrushKey | 0BFFFFFF |
| IsPressed=True | CommandBarBorderBrushKey | FF707070 |
| IsPressed=True | CommandBarMouseDownBackgroundGradientBrushKey | - |
| IsEnabled=False | CommandBarTextInactiveBrushKey | FF5C5C5C |
| IsEnabled=False | PanelHyperlinkDisabledBrushKey | FF808080 |
| IsEnabled=False | ControlFillDisabledBrushKey | 0BFFFFFF |
| Kind=Accent | AccentFillDefaultBrushKey | FF9184EE |
| Kind=Accent | TextOnAccentFillPrimaryBrushKey | FF000000 |
| Kind=Accent | AccentFillDefaultColorKey | FF9184EE |
| Kind=Subtle | ControlFillTransparentBrushKey | 00FFFFFF |
| Kind=Subtle | ControlStrokeTransparentBrushKey | 00FFFFFF |
| IsKeyboardFocusWithin=true | CommandBarMouseOverBackgroundGradientBrushKey | - |
| IsKeyboardFocusWithin=true | CommandBarHoverOverSelectedIconBorderBrushKey | FF707070 |
| IsKeyboardFocusWithin=true | CommandBarTextHoverBrushKey | FFFAFAFA |

### ToggleButton

| estado | token | hex |
|---|---|---|
| default | SubtleFillTertiaryBrushKey | 0BFFFFFF |
| default | AccentFillDisabledBrushKey | 28FFFFFF |
| default | SubtleFillSecondaryBrushKey | 0FFFFFFF |
| IsMouseOver=True | ComboBoxButtonMouseOverSeparatorBrushKey | 00000000 |
| IsMouseOver=True | ControlStrokeSecondaryBrushKey | 12FFFFFF |
| IsMouseOver=True | ComboBoxMouseOverTextBrushKey | FFFAFAFA |
| IsChecked=true | CommandBarOptionsMouseDownBackgroundVerticalGradientBrushKey | - |
| IsChecked=true | CommandBarOptionsMouseDownGlyphBrushKey | FFD6D6D6 |
| IsKeyboardFocused=True | CommandBarOptionsMouseOverBackgroundVerticalGradientBrushKey | - |
| IsKeyboardFocused=True | CommandBarOptionsMouseOverGlyphBrushKey | FFD6D6D6 |
| IsChecked=True | DropDownButtonMouseDownBackgroundBrushKey | FF3D3D3D |
| IsChecked=True | DropDownMouseDownGlyphBrushKey | FFD6D6D6 |
| IsChecked=True | DropDownButtonMouseDownSeparatorBrushKey | 00000000 |
| IsPressed=True | DropDownButtonMouseDownBackgroundBrushKey | FF3D3D3D |
| IsPressed=True | DropDownButtonMouseDownSeparatorBrushKey | 00000000 |
| IsPressed=True | DropDownMouseDownGlyphBrushKey | FFD6D6D6 |
| Kind=Subtle | ControlFillTransparentBrushKey | 00FFFFFF |
| Kind=Subtle | ControlStrokeTransparentBrushKey | 00FFFFFF |

### CheckBox

| estado | token | hex |
|---|---|---|
| default | AccentFillDisabledBrushKey | 28FFFFFF |
| default | AccentFillSecondaryBrushKey | E59184EE |
| default | AccentFillTertiaryBrushKey | CC9184EE |
| IsFocused=true | CommandBarCheckBoxMouseOverBrushKey | FFD6D6D6 |
| IsFocused=true | CommandBarHoverOverSelectedIconBrushKey | FF333333 |
| IsMouseOver=true | CommandBarCheckBoxMouseOverBrushKey | FFD6D6D6 |
| IsMouseOver=true | CommandBarHoverOverSelectedIconBrushKey | FF333333 |
| IsEnabled=False | TextOnAccentFillDisabledBrushKey | 80000000 |
| IsEnabled=False | ControlAltFillDisabledBrushKey | 00FFFFFF |
| IsEnabled=False | ControlStrongStrokeDisabledBrushKey | 28FFFFFF |
| IsChecked=True | AccentFillDefaultBrushKey | FF9184EE |
| IsChecked=True | TextOnAccentFillPrimaryBrushKey | FF000000 |
| IsChecked={x:Null} | AccentFillDefaultBrushKey | FF9184EE |
| IsChecked={x:Null} | TextOnAccentFillPrimaryBrushKey | FF000000 |
| IsPressed=True | TextOnAccentFillSecondaryBrushKey | 80000000 |
| IsPressed=True | ControlAltFillQuaternaryBrushKey | 12FFFFFF |
| IsPressed=True | ScrollBarArrowGlyphPressedBrushKey | FF1F1F1F |

### RadioButton

| estado | token | hex |
|---|---|---|
| default | ControlFillTransparentBrushKey | 00FFFFFF |
| default | AccentFillDisabledBrushKey | 28FFFFFF |
| default | AccentFillSecondaryBrushKey | E59184EE |
| IsMouseOver=True | SubtleFillSecondaryBrushKey | 0FFFFFFF |
| IsMouseOver=True | CommandBarMouseOverBackgroundGradientBrushKey | - |
| IsPressed=True | SubtleFillTertiaryBrushKey | 0BFFFFFF |
| IsPressed=True | TextFillSecondaryBrushKey | C8FFFFFF |
| IsPressed=True | TextOnAccentFillSecondaryBrushKey | 80000000 |
| IsEnabled=False | TextOnAccentFillDisabledBrushKey | 80000000 |
| IsEnabled=False | ControlAltFillDisabledBrushKey | 00FFFFFF |
| IsEnabled=False | ControlStrongStrokeDisabledBrushKey | 28FFFFFF |
| IsChecked=True | AccentFillDefaultBrushKey | FF9184EE |
| IsChecked=True | TextOnAccentFillPrimaryBrushKey | FF000000 |
| IsChecked=True | SystemHighlightBrushKey | - |
| IsKeyboardFocused=True | SystemHighlightBrushKey | - |
| IsKeyboardFocused=True | CommandBarMouseOverBackgroundGradientBrushKey | - |

### MenuItem

| estado | token | hex |
|---|---|---|
| default | CommandBarTextActiveBrushKey | FFD6D6D6 |
| default | CommandBarCheckBoxBrushKey | FFD6D6D6 |
| default | TextFillPrimaryBrushKey | FFFFFFFF |
| IsHighlighted=true | CommandBarMenuItemMouseOverBrushKey | FFFAFAFA |
| IsHighlighted=true | CommandBarMenuItemMouseOverBorderBrushKey | FF707070 |
| IsHighlighted=true | CommandBarMenuItemMouseOverTextBrushKey | - |
| IsEnabled=false | CommandBarTextInactiveBrushKey | FF5C5C5C |
| IsEnabled=false | CommandBarSelectedIconDisabledBrushKey | FF2E2E2E |
| IsEnabled=false | CommandBarSelectedIconDisabledBorderBrushKey | FF3D3D3D |
| IsHighlighted=True | SubtleFillSecondaryBrushKey | 0FFFFFFF |
| IsHighlighted=True | ControlStrokeSecondaryBrushKey | 12FFFFFF |
| IsHighlighted=True | CommandBarMouseOverBackgroundGradientBrushKey | - |
| IsEnabled=False | TextFillDisabledBrushKey | 5DFFFFFF |
| IsEnabled=False | CommandBarTextInactiveBrushKey | FF5C5C5C |
| IsEnabled=False | CommandBarMenuBackgroundGradientBeginBrushKey | FF2E2E2E |
| IsMouseOver=True | SubtleFillSecondaryBrushKey | 0FFFFFFF |
| IsMouseOver=True | ControlStrokeSecondaryBrushKey | 12FFFFFF |
| IsMouseOver=True | CommandBarMenuItemMouseOverBrushKey | FFFAFAFA |
| IsPressed=True | SubtleFillTertiaryBrushKey | 0BFFFFFF |
| IsPressed=True | TextFillSecondaryBrushKey | C8FFFFFF |

### Hyperlink

| estado | token | hex |
|---|---|---|
| default | PanelHyperlinkBrushKey | FF83BEEB |
| default | PanelHyperlinkHoverBrushKey | FFFAFAFA |
| default | HyperlinkFillPrimaryBrushKey | FF99EBFF |
| IsMouseOver=True | PanelHyperlinkHoverBrushKey | FFFAFAFA |
| IsMouseOver=True | HyperlinkFillSecondaryBrushKey | FF60CDFF |
| IsMouseOver=True | ControlLinkTextHoverBrushKey | FFFAFAFA |
| IsEnabled=True | PanelHyperlinkBrushKey | FF83BEEB |
| IsEnabled=True | HyperlinkFillPrimaryBrushKey | FF99EBFF |
| IsEnabled=False | HyperlinkFillDisabledBrushKey | 5DFFFFFF |
| IsEnabled=False | PanelHyperlinkDisabledBrushKey | FF808080 |
| IsKeyboardFocused=True | PanelHyperlinkHoverBrushKey | FFFAFAFA |
| IsEnabled=false | PanelHyperlinkDisabledBrushKey | FF808080 |

### ToggleSwitch

| estado | token | hex |
|---|---|---|
| default | ControlStrongStrokeDefaultBrushKey | 9AFFFFFF |
| default | AccentFillSecondaryBrushKey | E59184EE |
| default | AccentFillTertiaryBrushKey | CC9184EE |
| IsChecked=True | AccentFillDefaultBrushKey | FF9184EE |
| IsChecked=True | TextOnAccentFillPrimaryBrushKey | FF000000 |
| IsEnabled=False | AccentFillDisabledBrushKey | 28FFFFFF |
| IsEnabled=False | ControlAltFillDisabledBrushKey | 00FFFFFF |
| IsEnabled=False | ControlStrongStrokeDisabledBrushKey | 28FFFFFF |
| IsMouseOver=True | SubtleFillSecondaryBrushKey | 0FFFFFFF |
| IsPressed=True | ControlAltFillQuaternaryBrushKey | 12FFFFFF |

### ButtonBase

| estado | token | hex |
|---|---|---|
| default | ControlFillDefaultBrushKey | 0FFFFFFF |
| default | ControlStrokeDefaultBrushKey | 12FFFFFF |
| default | TextFillPrimaryBrushKey | FFFFFFFF |
| IsEnabled=False | TextFillDisabledBrushKey | 5DFFFFFF |
| IsEnabled=False | CommandBarTextInactiveBrushKey | FF5C5C5C |
| IsEnabled=False | ComboBoxDisabledBackgroundBrushKey | FF333333 |
| IsKeyboardFocused=True | CommandBarBorderBrushKey | FF707070 |
| IsKeyboardFocused=True | CommandBarMouseOverBackgroundGradientBrushKey | - |
| IsKeyboardFocused=True | CommandBarTextHoverBrushKey | FFFAFAFA |
| IsMouseOver=True | ControlFillSecondaryBrushKey | 15FFFFFF |
| IsMouseOver=True | ControlStrokeSecondaryBrushKey | 12FFFFFF |
| IsMouseOver=True | CommandBarBorderBrushKey | FF707070 |
| IsPressed=True | ControlFillTertiaryBrushKey | 08FFFFFF |
| IsPressed=True | ControlStrokeDefaultBrushKey | 12FFFFFF |
| IsPressed=True | TextFillSecondaryBrushKey | C8FFFFFF |

### ListBoxItem

| estado | token | hex |
|---|---|---|
| default | TextFillPrimaryBrushKey | FFFFFFFF |
| default | ControlFillTransparentBrushKey | 00FFFFFF |
| default | ControlStrokeTransparentBrushKey | 00FFFFFF |
| IsEnabled=False | TextFillDisabledBrushKey | 5DFFFFFF |
| IsMouseOver=True | SubtleFillSecondaryBrushKey | 0FFFFFFF |
| IsMouseOver=True | ControlStrokeSecondaryBrushKey | 12FFFFFF |
| IsMouseOver=True | CommandBarBorderBrushKey | FF707070 |
| IsSelected=True | SubtleFillSecondaryBrushKey | 0FFFFFFF |
| IsSelected=True | ControlStrokeSecondaryBrushKey | 12FFFFFF |
| IsSelected=True | ToolWindowTabMouseOverBackgroundGradientBrushKey | - |
| IsFocused=True | ComboBoxItemMouseOverBackgroundBrushKey | FF3D3D3D |
| IsFocused=True | ComboBoxItemMouseOverTextBrushKey | FFFAFAFA |
| IsFocused=True | ComboBoxItemMouseOverBorderBrushKey | FF707070 |
| IsKeyboardFocusWithin=True | SubtleFillSecondaryBrushKey | 0FFFFFFF |
| IsKeyboardFocusWithin=True | ControlStrokeSecondaryBrushKey | 12FFFFFF |

### TextBox

| estado | token | hex |
|---|---|---|
| default | AccentFillSelectedTextBackgroundBrushKey | FF005FB7 |
| default | TextFillSecondaryBrushKey | C8FFFFFF |
| default | ControlFillDefaultBrushKey | 0FFFFFFF |
| IsEnabled=False | TextFillDisabledBrushKey | 5DFFFFFF |
| IsKeyboardFocusWithin=True | AccentFillDefaultBrushKey | FF9184EE |
| IsKeyboardFocusWithin=True | ControlFillActiveInputBrushKey | B21E1E1E |
| IsMouseOver=True | ControlFillSecondaryBrushKey | 15FFFFFF |
| IsKeyboardFocusWithin=False | ControlFillTransparentBrushKey | 00FFFFFF |
| HasError=True | SystemFillCriticalBrushKey | FFFF99A4 |

### SplitButton

| estado | token | hex |
|---|---|---|
| default | AccentFillSecondaryBrushKey | E59184EE |
| default | ControlStrokeSecondaryBrushKey | 12FFFFFF |
| default | ControlFillSecondaryBrushKey | 15FFFFFF |
| Kind=Accent | TextOnAccentFillSecondaryBrushKey | 80000000 |
| Kind=Subtle | ControlStrokeDefaultBrushKey | 12FFFFFF |
| IsEnabled=False | TextFillDisabledBrushKey | 5DFFFFFF |

### HyperlinkButton

| estado | token | hex |
|---|---|---|
| default | ControlFillTransparentBrushKey | 00FFFFFF |
| default | ControlStrokeTransparentBrushKey | 00FFFFFF |
| default | HyperlinkFillPrimaryBrushKey | FF99EBFF |
| IsEnabled=False | HyperlinkFillDisabledBrushKey | 5DFFFFFF |
| IsEnabled=False | PanelHyperlinkDisabledBrushKey | FF808080 |
| IsMouseOver=True | SubtleFillSecondaryBrushKey | 0FFFFFFF |
| IsMouseOver=True | ControlStrokeSecondaryBrushKey | 12FFFFFF |
| IsMouseOver=True | HyperlinkFillSecondaryBrushKey | FF60CDFF |
| IsPressed=True | SubtleFillTertiaryBrushKey | 0BFFFFFF |
| IsPressed=True | HyperlinkFillTertiaryBrushKey | E660CDFF |
| IsEnabled=True | PanelHyperlinkBrushKey | FF83BEEB |

### BreadcrumbItem

| estado | token | hex |
|---|---|---|
| default | ControlFillTransparentBrushKey | 00FFFFFF |
| default | ControlStrokeTransparentBrushKey | 00FFFFFF |
| default | TextFillPrimaryBrushKey | FFFFFFFF |
| IsPressed=True | TextFillPrimaryBrushKey | FFFFFFFF |
| IsPressed=True | SubtleFillTertiaryBrushKey | 0BFFFFFF |
| IsPressed=True | TextFillSecondaryBrushKey | C8FFFFFF |
| IsEnabled=False | TextFillDisabledBrushKey | 5DFFFFFF |
| IsMouseOver=True | SubtleFillSecondaryBrushKey | 0FFFFFFF |
| IsMouseOver=True | ControlStrokeSecondaryBrushKey | 12FFFFFF |

### Thumb

| estado | token | hex |
|---|---|---|
| default | Instance | - |
| default | ControlStrongFillDefaultBrushKey | 8BFFFFFF |
| default | ControlSolidFillDefaultBrushKey | FF454545 |
| IsMouseOver=True | FileTabScrollBarThumbHoverBrushKey | FF999999 |
| IsMouseOver=True | ScrollBarThumbMouseOverBackgroundBrushKey | FF999999 |
| IsMouseOver=True | AccentFillDefaultBrushKey | FF9184EE |
| IsDragging=True | FileTabScrollBarThumbPressedBrushKey | FF999999 |
| IsDragging=True | ScrollBarThumbPressedBackgroundBrushKey | FF999999 |
| IsEnabled=False | ControlStrongFillDisabledBrushKey | 3FFFFFFF |
| IsEnabled=False | ScrollBarThumbDisabledBrushKey | FF3D3D3D |

### TextBlock

| estado | token | hex |
|---|---|---|
| default | TextFillPrimaryBrushKey | FFFFFFFF |
| default | ToolWindowTextBrushKey | FFFAFAFA |
| default | SystemFillCriticalBrushKey | FFFF99A4 |
| IsEnabled=False | TextFillDisabledBrushKey | 5DFFFFFF |
| IsEnabled=False | CommandBarTextInactiveBrushKey | FF5C5C5C |

### ThemedToggleButton

| estado | token | hex |
|---|---|---|
| default | CommandBarHoverOverSelectedIconBorderBrushKey | FF707070 |
| default | CommandBarHoverOverSelectedIconBrushKey | FF333333 |
| default | CommandBarTextHoverOverSelectedBrushKey | FFFAFAFA |
| IsKeyboardFocused=true | CommandBarBorderBrushKey | FF707070 |
| IsKeyboardFocused=true | CommandBarMouseOverBackgroundGradientBrushKey | - |
| IsKeyboardFocused=true | CommandBarTextHoverBrushKey | FFFAFAFA |
| IsMouseOver=true | CommandBarBorderBrushKey | FF707070 |
| IsMouseOver=true | CommandBarMouseOverBackgroundGradientBrushKey | - |
| IsMouseOver=true | CommandBarTextHoverBrushKey | FFFAFAFA |
| IsPressed=true | CommandBarBorderBrushKey | FF707070 |
| IsPressed=true | CommandBarMouseDownBackgroundGradientBrushKey | - |
| IsPressed=true | CommandBarTextMouseDownBrushKey | FFFAFAFA |

### TabItem

| estado | token | hex |
|---|---|---|
| default | TextFillPrimaryBrushKey | FFFFFFFF |
| default | ControlFillTransparentBrushKey | 00FFFFFF |
| default | ControlStrokeTransparentBrushKey | 00FFFFFF |
| IsMouseOver=True | SubtleFillSecondaryBrushKey | 0FFFFFFF |
| IsMouseOver=True | ControlStrokeSecondaryBrushKey | 12FFFFFF |
| IsSelected=True | SolidBackgroundFillTertiaryBrushKey | FF282828 |
| IsSelected=True | CommandBarTextActiveBrushKey | FFD6D6D6 |
| IsEnabled=False | TextFillDisabledBrushKey | 5DFFFFFF |

### RepeatButton

| estado | token | hex |
|---|---|---|
| default | ControlFillTransparentBrushKey | 00FFFFFF |
| default | ControlStrongFillDefaultBrushKey | 8BFFFFFF |
| default | ControlStrokeTransparentBrushKey | 00FFFFFF |
| IsMouseOver=True | SubtleFillSecondaryBrushKey | 0FFFFFFF |
| IsMouseOver=True | FileTabScrollBarArrowGlyphHoverBrushKey | FF999999 |
| IsMouseOver=True | ScrollBarArrowMouseOverBackgroundBrushKey | FF999999 |
| IsPressed=True | FileTabScrollBarArrowGlyphPressedBrushKey | FF999999 |
| IsPressed=True | ScrollBarArrowPressedBackgroundBrushKey | FF999999 |
| IsPressed=True | ScrollBarArrowGlyphPressedBrushKey | FF1F1F1F |
| IsEnabled=False | ScrollBarArrowGlyphDisabledBrushKey | FF808080 |
| IsEnabled=False | ScrollBarArrowDisabledBackgroundBrushKey | FF333333 |

## Top 40 tokens

| token | hex | refs | arquivos | controles |
|---|---|---|---|---|
| TextFillPrimaryBrushKey | FFFFFFFF | 304 | 214 | ?, BreadcrumbItem |
| ControlFillTransparentBrushKey | 00FFFFFF | 211 | 193 | RepeatButton, RadioButton |
| SubtleFillSecondaryBrushKey | 0FFFFFFF | 207 | 135 | RadioButton, ? |
| TextFillSecondaryBrushKey | C8FFFFFF | 171 | 95 | ?, Button |
| ControlStrokeSecondaryBrushKey | 12FFFFFF | 151 | 100 | SplitButton, ListBoxItem |
| TextFillDisabledBrushKey | 5DFFFFFF | 148 | 136 | ?, TextBox |
| ControlStrokeTransparentBrushKey | 00FFFFFF | 137 | 136 | Button, ToggleButton |
| AccentFillDefaultBrushKey | FF9184EE | 134 | 89 | CheckBox, Button |
| ToolWindowTextBrushKey | FFFAFAFA | 129 | 56 | ?, TextBlock |
| Instance | - | 95 | 37 | ScrollViewer, ScrollBar |
| ControlStrokeDefaultBrushKey | 12FFFFFF | 92 | 63 | DataGrid, ? |
| SubtleFillTertiaryBrushKey | 0BFFFFFF | 87 | 45 | Button, ToggleButton |
| AccentFillSecondaryBrushKey | E59184EE | 85 | 33 | CheckBox, SplitButton |
| AccentFillDisabledBrushKey | 28FFFFFF | 65 | 32 | CheckBox, Button |
| AccentFillTertiaryBrushKey | CC9184EE | 55 | 23 | CheckBox, Button |
| CommandBarTextActiveBrushKey | FFD6D6D6 | 54 | 35 | MenuItem, ComboBoxItem |
| PanelHyperlinkBrushKey | FF83BEEB | 49 | 28 | Hyperlink, HyperlinkButton |
| ControlFillDefaultBrushKey | 0FFFFFFF | 49 | 47 | TextBox, ButtonBase |
| ControlFillSecondaryBrushKey | 15FFFFFF | 48 | 39 | SplitButton, TextBox |
| TextOnAccentFillPrimaryBrushKey | FF000000 | 46 | 31 | CheckBox, RadioButton |
| CommandBarBorderBrushKey | FF707070 | 45 | 21 | GlyphButton, Button |
| CommandBarTextHoverBrushKey | FFFAFAFA | 45 | 30 | ?, Button |
| ToolWindowBackgroundBrushKey | FF1F1F1F | 37 | 28 | ?, TextBlock |
| PanelHyperlinkHoverBrushKey | FFFAFAFA | 34 | 25 | Hyperlink, Button |
| SolidBackgroundFillTertiaryBrushKey | FF282828 | 33 | 22 | ?, Window |
| FocusStrokeOuterBrushKey | FFFFFFFF | 33 | 28 | , Control |
| CommandBarMouseOverBackgroundGradientBrushKey | - | 31 | 18 | Button, ButtonBase |
| ComboBoxButtonMouseOverSeparatorBrushKey | 00000000 | 30 | 7 | ToggleButton, ? |
| ComboBoxMouseOverTextBrushKey | FFFAFAFA | 29 | 8 | ?, ToggleButton |
| CommandBarTextInactiveBrushKey | FF5C5C5C | 28 | 23 | ?, MenuItem |
| SystemFillCriticalBrushKey | FFFF99A4 | 28 | 24 | TextBox, ComboBox |
| DropDownMouseOverGlyphBrushKey | FFD6D6D6 | 27 | 14 | ComboBox, ? |
| SurfaceStrokeFlyoutBrushKey | 33000000 | 27 | 22 | ?, CalloutSurface |
| ComboBoxMouseOverBorderBrushKey | FF808080 | 27 | 10 | ?, ToggleButton |
| ComboBoxMouseOverBackgroundGradientBrushKey | - | 27 | 10 | ?, ToggleButton |
| DropDownMouseOverTextBrushKey | FFFAFAFA | 26 | 6 | ComboBox |
| CommandBarMenuItemMouseOverBrushKey | FFFAFAFA | 26 | 16 | ?, MenuItem |
| SurfaceBackgroundFillDefaultBrushKey | FF2C2C2C | 25 | 22 | ?, CalloutSurface |
| ControlStrongStrokeDefaultBrushKey | 9AFFFFFF | 25 | 19 | ToggleSwitch, CheckBox |
| TextOnAccentFillSecondaryBrushKey | 80000000 | 22 | 22 | Button, CheckBox |

## Tokens sem hex na paleta escura (56)

BackgroundTextBrushKey, ComboBoxMouseOverBackgroundGradientBrushKey, ComboBoxPopupBackgroundGradientBrushKey, CommandBarGradientBrushKey, CommandBarMenuBackgroundGradientBrushKey, CommandBarMenuItemMouseOverTextBrushKey, CommandBarMenuItemMouseOverTextColorKey, CommandBarMouseDownBackgroundGradientBrushKey, CommandBarMouseOverBackgroundGradientBrushKey, CommandBarOptionsMouseDownBackgroundVerticalGradientBrushKey, CommandBarOptionsMouseOverBackgroundVerticalGradientBrushKey, CommandShelfHighlightGradientBrushKey, DialogTextBrushKey, DockTargetButtonBackgroundGradientBrushKey, DockTargetGlyphBackgroundGradientBrushKey, DropDownMouseOverBackgroundGradientBrushKey, DropDownPopupBackgroundGradientBrushKey, FileTabInactiveGradientBrushKey, FileTabSelectedGradientBrushKey, Instance, MainWindowActiveCaptionTextBrushKey, MainWindowInactiveCaptionTextBrushKey, PanelDisabledHyperlinkInsideSelectionActiveTextBrushKey, PanelDisabledHyperlinkInsideSelectionInactiveTextBrushKey, PanelHyperlinkInsideSelectionActiveTextBrushKey, PanelHyperlinkInsideSelectionTextBrushKey, SelectedItemActiveBrushKey, SelectedItemActiveTextBrushKey, SelectedItemInactiveBrushKey, SelectedItemInactiveTextBrushKey, StartPageTabBackgroundBrushKey, StatusBarBuildingTextBrushKey, StatusBarDebuggingTextBrushKey, StatusBarDefaultTextBrushKey, StatusBarNoSolutionTextBrushKey, SystemActiveBorderBrushKey, SystemActiveBorderColorKey, SystemButtonFaceBrushKey, SystemButtonTextBrushKey, SystemCaptionTextBrushKey, SystemGrayTextBrushKey, SystemGrayTextColorKey, SystemHighlightBrushKey, SystemHighlightTextBrushKey, SystemHighlightTextColorKey, SystemInactiveBorderBrushKey, SystemWindowBrushKey, ToolTipTextBrushKey, ToolWindowTabMouseOverBackgroundGradientBrushKey, ToolboxContentMouseOverTextBrushKey, ToolboxContentMouseOverTextColorKey, ToolboxContentTextBrushKey, ToolboxContentTextColorKey, ToolboxDisabledContentTextColorKey, TreeViewSelectedItemInactiveBrushKey, TreeViewSelectedItemInactiveTextBrushKey