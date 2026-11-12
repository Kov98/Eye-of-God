using System;

namespace Microsoft.VisualStudio.Shell.Styles;

public static class ShellColors
{
	public static readonly Guid Category = new Guid("73708ded-2d56-4aad-b8eb-73b20d3f4bff");

	private static ThemeResourceKey _AccentFillAltColorKey;

	private static ThemeResourceKey _AccentFillAltBrushKey;

	private static ThemeResourceKey _AccentFillDefaultColorKey;

	private static ThemeResourceKey _AccentFillDefaultBrushKey;

	private static ThemeResourceKey _AccentFillDisabledColorKey;

	private static ThemeResourceKey _AccentFillDisabledBrushKey;

	private static ThemeResourceKey _AccentFillSecondaryColorKey;

	private static ThemeResourceKey _AccentFillSecondaryBrushKey;

	private static ThemeResourceKey _AccentFillSelectedTextBackgroundColorKey;

	private static ThemeResourceKey _AccentFillSelectedTextBackgroundBrushKey;

	private static ThemeResourceKey _AccentFillSelectedTextBackgroundSubtleColorKey;

	private static ThemeResourceKey _AccentFillSelectedTextBackgroundSubtleBrushKey;

	private static ThemeResourceKey _AccentFillSenaryColorKey;

	private static ThemeResourceKey _AccentFillSenaryBrushKey;

	private static ThemeResourceKey _AccentFillTertiaryColorKey;

	private static ThemeResourceKey _AccentFillTertiaryBrushKey;

	private static ThemeResourceKey _AccentTextFillDisabledColorKey;

	private static ThemeResourceKey _AccentTextFillDisabledBrushKey;

	private static ThemeResourceKey _AccentTextFillPrimaryColorKey;

	private static ThemeResourceKey _AccentTextFillPrimaryBrushKey;

	private static ThemeResourceKey _AccentTextFillSecondaryColorKey;

	private static ThemeResourceKey _AccentTextFillSecondaryBrushKey;

	private static ThemeResourceKey _AccentTextFillTertiaryColorKey;

	private static ThemeResourceKey _AccentTextFillTertiaryBrushKey;

	private static ThemeResourceKey _CardBackgroundFillDefaultColorKey;

	private static ThemeResourceKey _CardBackgroundFillDefaultBrushKey;

	private static ThemeResourceKey _CardBackgroundFillSecondaryColorKey;

	private static ThemeResourceKey _CardBackgroundFillSecondaryBrushKey;

	private static ThemeResourceKey _CardBackgroundFillTertiaryColorKey;

	private static ThemeResourceKey _CardBackgroundFillTertiaryBrushKey;

	private static ThemeResourceKey _CardStrokeDefaultColorKey;

	private static ThemeResourceKey _CardStrokeDefaultBrushKey;

	private static ThemeResourceKey _CardStrokeDefaultSolidColorKey;

	private static ThemeResourceKey _CardStrokeDefaultSolidBrushKey;

	private static ThemeResourceKey _CardStrokeDefaultSolidAltColorKey;

	private static ThemeResourceKey _CardStrokeDefaultSolidAltBrushKey;

	private static ThemeResourceKey _ControlAltFillDisabledColorKey;

	private static ThemeResourceKey _ControlAltFillDisabledBrushKey;

	private static ThemeResourceKey _ControlAltFillQuaternaryColorKey;

	private static ThemeResourceKey _ControlAltFillQuaternaryBrushKey;

	private static ThemeResourceKey _ControlAltFillSecondaryColorKey;

	private static ThemeResourceKey _ControlAltFillSecondaryBrushKey;

	private static ThemeResourceKey _ControlAltFillTertiaryColorKey;

	private static ThemeResourceKey _ControlAltFillTertiaryBrushKey;

	private static ThemeResourceKey _ControlAltFillTransparentColorKey;

	private static ThemeResourceKey _ControlAltFillTransparentBrushKey;

	private static ThemeResourceKey _ControlFillActiveInputColorKey;

	private static ThemeResourceKey _ControlFillActiveInputBrushKey;

	private static ThemeResourceKey _ControlFillDefaultColorKey;

	private static ThemeResourceKey _ControlFillDefaultBrushKey;

	private static ThemeResourceKey _ControlFillDisabledColorKey;

	private static ThemeResourceKey _ControlFillDisabledBrushKey;

	private static ThemeResourceKey _ControlFillQuaternaryColorKey;

	private static ThemeResourceKey _ControlFillQuaternaryBrushKey;

	private static ThemeResourceKey _ControlFillSecondaryColorKey;

	private static ThemeResourceKey _ControlFillSecondaryBrushKey;

	private static ThemeResourceKey _ControlFillTertiaryColorKey;

	private static ThemeResourceKey _ControlFillTertiaryBrushKey;

	private static ThemeResourceKey _ControlFillTransparentColorKey;

	private static ThemeResourceKey _ControlFillTransparentBrushKey;

	private static ThemeResourceKey _ControlOnImageFillDefaultColorKey;

	private static ThemeResourceKey _ControlOnImageFillDefaultBrushKey;

	private static ThemeResourceKey _ControlOnImageFillDisabledColorKey;

	private static ThemeResourceKey _ControlOnImageFillDisabledBrushKey;

	private static ThemeResourceKey _ControlOnImageFillSecondaryColorKey;

	private static ThemeResourceKey _ControlOnImageFillSecondaryBrushKey;

	private static ThemeResourceKey _ControlOnImageFillTertiaryColorKey;

	private static ThemeResourceKey _ControlOnImageFillTertiaryBrushKey;

	private static ThemeResourceKey _ControlSolidFillDefaultColorKey;

	private static ThemeResourceKey _ControlSolidFillDefaultBrushKey;

	private static ThemeResourceKey _ControlStrokeDefaultColorKey;

	private static ThemeResourceKey _ControlStrokeDefaultBrushKey;

	private static ThemeResourceKey _ControlStrokeForStrongFillWhenOnImageColorKey;

	private static ThemeResourceKey _ControlStrokeForStrongFillWhenOnImageBrushKey;

	private static ThemeResourceKey _ControlStrokeOnAccentDefaultColorKey;

	private static ThemeResourceKey _ControlStrokeOnAccentDefaultBrushKey;

	private static ThemeResourceKey _ControlStrokeOnAccentDisabledColorKey;

	private static ThemeResourceKey _ControlStrokeOnAccentDisabledBrushKey;

	private static ThemeResourceKey _ControlStrokeOnAccentSecondaryColorKey;

	private static ThemeResourceKey _ControlStrokeOnAccentSecondaryBrushKey;

	private static ThemeResourceKey _ControlStrokeOnAccentTertiaryColorKey;

	private static ThemeResourceKey _ControlStrokeOnAccentTertiaryBrushKey;

	private static ThemeResourceKey _ControlStrokeSecondaryColorKey;

	private static ThemeResourceKey _ControlStrokeSecondaryBrushKey;

	private static ThemeResourceKey _ControlStrokeTransparentColorKey;

	private static ThemeResourceKey _ControlStrokeTransparentBrushKey;

	private static ThemeResourceKey _ControlStrongFillDefaultColorKey;

	private static ThemeResourceKey _ControlStrongFillDefaultBrushKey;

	private static ThemeResourceKey _ControlStrongFillDisabledColorKey;

	private static ThemeResourceKey _ControlStrongFillDisabledBrushKey;

	private static ThemeResourceKey _ControlStrongStrokeDefaultColorKey;

	private static ThemeResourceKey _ControlStrongStrokeDefaultBrushKey;

	private static ThemeResourceKey _ControlStrongStrokeDisabledColorKey;

	private static ThemeResourceKey _ControlStrongStrokeDisabledBrushKey;

	private static ThemeResourceKey _DividerStrokeDefaultColorKey;

	private static ThemeResourceKey _DividerStrokeDefaultBrushKey;

	private static ThemeResourceKey _FocusStrokeInnerColorKey;

	private static ThemeResourceKey _FocusStrokeInnerBrushKey;

	private static ThemeResourceKey _FocusStrokeOuterColorKey;

	private static ThemeResourceKey _FocusStrokeOuterBrushKey;

	private static ThemeResourceKey _HyperlinkFillDisabledColorKey;

	private static ThemeResourceKey _HyperlinkFillDisabledBrushKey;

	private static ThemeResourceKey _HyperlinkFillPrimaryColorKey;

	private static ThemeResourceKey _HyperlinkFillPrimaryBrushKey;

	private static ThemeResourceKey _HyperlinkFillSecondaryColorKey;

	private static ThemeResourceKey _HyperlinkFillSecondaryBrushKey;

	private static ThemeResourceKey _HyperlinkFillTertiaryColorKey;

	private static ThemeResourceKey _HyperlinkFillTertiaryBrushKey;

	private static ThemeResourceKey _LayerFillAltColorKey;

	private static ThemeResourceKey _LayerFillAltBrushKey;

	private static ThemeResourceKey _LayerFillDefaultColorKey;

	private static ThemeResourceKey _LayerFillDefaultBrushKey;

	private static ThemeResourceKey _ShadowFlyoutColorKey;

	private static ThemeResourceKey _ShadowFlyoutBrushKey;

	private static ThemeResourceKey _SmokeFillDefaultColorKey;

	private static ThemeResourceKey _SmokeFillDefaultBrushKey;

	private static ThemeResourceKey _SmokeFillInverseColorKey;

	private static ThemeResourceKey _SmokeFillInverseBrushKey;

	private static ThemeResourceKey _SolidBackgroundFillBaseColorKey;

	private static ThemeResourceKey _SolidBackgroundFillBaseBrushKey;

	private static ThemeResourceKey _SolidBackgroundFillBaseAltColorKey;

	private static ThemeResourceKey _SolidBackgroundFillBaseAltBrushKey;

	private static ThemeResourceKey _SolidBackgroundFillQuaternaryColorKey;

	private static ThemeResourceKey _SolidBackgroundFillQuaternaryBrushKey;

	private static ThemeResourceKey _SolidBackgroundFillQuinaryColorKey;

	private static ThemeResourceKey _SolidBackgroundFillQuinaryBrushKey;

	private static ThemeResourceKey _SolidBackgroundFillSecondaryColorKey;

	private static ThemeResourceKey _SolidBackgroundFillSecondaryBrushKey;

	private static ThemeResourceKey _SolidBackgroundFillSenaryColorKey;

	private static ThemeResourceKey _SolidBackgroundFillSenaryBrushKey;

	private static ThemeResourceKey _SolidBackgroundFillTertiaryColorKey;

	private static ThemeResourceKey _SolidBackgroundFillTertiaryBrushKey;

	private static ThemeResourceKey _SubtleFillDisabledColorKey;

	private static ThemeResourceKey _SubtleFillDisabledBrushKey;

	private static ThemeResourceKey _SubtleFillSecondaryColorKey;

	private static ThemeResourceKey _SubtleFillSecondaryBrushKey;

	private static ThemeResourceKey _SubtleFillTertiaryColorKey;

	private static ThemeResourceKey _SubtleFillTertiaryBrushKey;

	private static ThemeResourceKey _SubtleFillTransparentColorKey;

	private static ThemeResourceKey _SubtleFillTransparentBrushKey;

	private static ThemeResourceKey _SurfaceBackgroundFillDefaultColorKey;

	private static ThemeResourceKey _SurfaceBackgroundFillDefaultBrushKey;

	private static ThemeResourceKey _SurfaceStrokeDefaultColorKey;

	private static ThemeResourceKey _SurfaceStrokeDefaultBrushKey;

	private static ThemeResourceKey _SurfaceStrokeFlyoutColorKey;

	private static ThemeResourceKey _SurfaceStrokeFlyoutBrushKey;

	private static ThemeResourceKey _SystemFillAttentionColorKey;

	private static ThemeResourceKey _SystemFillAttentionBrushKey;

	private static ThemeResourceKey _SystemFillAttentionBackgroundColorKey;

	private static ThemeResourceKey _SystemFillAttentionBackgroundBrushKey;

	private static ThemeResourceKey _SystemFillCautionColorKey;

	private static ThemeResourceKey _SystemFillCautionBrushKey;

	private static ThemeResourceKey _SystemFillCautionBackgroundColorKey;

	private static ThemeResourceKey _SystemFillCautionBackgroundBrushKey;

	private static ThemeResourceKey _SystemFillCriticalColorKey;

	private static ThemeResourceKey _SystemFillCriticalBrushKey;

	private static ThemeResourceKey _SystemFillCriticalBackgroundColorKey;

	private static ThemeResourceKey _SystemFillCriticalBackgroundBrushKey;

	private static ThemeResourceKey _SystemFillNeutralColorKey;

	private static ThemeResourceKey _SystemFillNeutralBrushKey;

	private static ThemeResourceKey _SystemFillNeutralBackgroundColorKey;

	private static ThemeResourceKey _SystemFillNeutralBackgroundBrushKey;

	private static ThemeResourceKey _SystemFillSolidAttentionBackgroundColorKey;

	private static ThemeResourceKey _SystemFillSolidAttentionBackgroundBrushKey;

	private static ThemeResourceKey _SystemFillSolidNeutralColorKey;

	private static ThemeResourceKey _SystemFillSolidNeutralBrushKey;

	private static ThemeResourceKey _SystemFillSolidNeutralBackgroundColorKey;

	private static ThemeResourceKey _SystemFillSolidNeutralBackgroundBrushKey;

	private static ThemeResourceKey _SystemFillSuccessColorKey;

	private static ThemeResourceKey _SystemFillSuccessBrushKey;

	private static ThemeResourceKey _SystemFillSuccessBackgroundColorKey;

	private static ThemeResourceKey _SystemFillSuccessBackgroundBrushKey;

	private static ThemeResourceKey _TextFillDisabledColorKey;

	private static ThemeResourceKey _TextFillDisabledBrushKey;

	private static ThemeResourceKey _TextFillPrimaryColorKey;

	private static ThemeResourceKey _TextFillPrimaryBrushKey;

	private static ThemeResourceKey _TextFillSecondaryColorKey;

	private static ThemeResourceKey _TextFillSecondaryBrushKey;

	private static ThemeResourceKey _TextFillTertiaryColorKey;

	private static ThemeResourceKey _TextFillTertiaryBrushKey;

	private static ThemeResourceKey _TextOnAccentFillDisabledColorKey;

	private static ThemeResourceKey _TextOnAccentFillDisabledBrushKey;

	private static ThemeResourceKey _TextOnAccentFillPrimaryColorKey;

	private static ThemeResourceKey _TextOnAccentFillPrimaryBrushKey;

	private static ThemeResourceKey _TextOnAccentFillSecondaryColorKey;

	private static ThemeResourceKey _TextOnAccentFillSecondaryBrushKey;

	private static ThemeResourceKey _TextOnAccentFillSelectedTextColorKey;

	private static ThemeResourceKey _TextOnAccentFillSelectedTextBrushKey;

	public static ThemeResourceKey AccentFillAltColorKey
	{
		get
		{
			//IL_0014: Unknown result type (might be due to invalid IL or missing references)
			//IL_0019: Unknown result type (might be due to invalid IL or missing references)
			//IL_001f: Expected Obj, but got Unknown
			ThemeResourceKey val = _AccentFillAltColorKey;
			if (val == null)
			{
				ThemeResourceKey val2 = new ThemeResourceKey(Category, "AccentFillAlt", (ThemeResourceKeyType)1);
				_AccentFillAltColorKey = val2;
				val = val2;
			}
			return val;
		}
	}

	public static ThemeResourceKey AccentFillAltBrushKey
	{
		get
		{
			//IL_0014: Unknown result type (might be due to invalid IL or missing references)
			//IL_0019: Unknown result type (might be due to invalid IL or missing references)
			//IL_001f: Expected Obj, but got Unknown
			ThemeResourceKey val = _AccentFillAltBrushKey;
			if (val == null)
			{
				ThemeResourceKey val2 = new ThemeResourceKey(Category, "AccentFillAlt", (ThemeResourceKeyType)3);
				_AccentFillAltBrushKey = val2;
				val = val2;
			}
			return val;
		}
	}

	public static ThemeResourceKey AccentFillDefaultColorKey
	{
		get
		{
			//IL_0014: Unknown result type (might be due to invalid IL or missing references)
			//IL_0019: Unknown result type (might be due to invalid IL or missing references)
			//IL_001f: Expected Obj, but got Unknown
			ThemeResourceKey val = _AccentFillDefaultColorKey;
			if (val == null)
			{
				ThemeResourceKey val2 = new ThemeResourceKey(Category, "AccentFillDefault", (ThemeResourceKeyType)1);
				_AccentFillDefaultColorKey = val2;
				val = val2;
			}
			return val;
		}
	}

	public static ThemeResourceKey AccentFillDefaultBrushKey
	{
		get
		{
			//IL_0014: Unknown result type (might be due to invalid IL or missing references)
			//IL_0019: Unknown result type (might be due to invalid IL or missing references)
			//IL_001f: Expected Obj, but got Unknown
			ThemeResourceKey val = _AccentFillDefaultBrushKey;
			if (val == null)
			{
				ThemeResourceKey val2 = new ThemeResourceKey(Category, "AccentFillDefault", (ThemeResourceKeyType)3);
				_AccentFillDefaultBrushKey = val2;
				val = val2;
			}
			return val;
		}
	}

	public static ThemeResourceKey AccentFillDisabledColorKey
	{
		get
		{
			//IL_0014: Unknown result type (might be due to invalid IL or missing references)
			//IL_0019: Unknown result type (might be due to invalid IL or missing references)
			//IL_001f: Expected Obj, but got Unknown
			ThemeResourceKey val = _AccentFillDisabledColorKey;
			if (val == null)
			{
				ThemeResourceKey val2 = new ThemeResourceKey(Category, "AccentFillDisabled", (ThemeResourceKeyType)1);
				_AccentFillDisabledColorKey = val2;
				val = val2;
			}
			return val;
		}
	}

	public static ThemeResourceKey AccentFillDisabledBrushKey
	{
		get
		{
			//IL_0014: Unknown result type (might be due to invalid IL or missing references)
			//IL_0019: Unknown result type (might be due to invalid IL or missing references)
			//IL_001f: Expected Obj, but got Unknown
			ThemeResourceKey val = _AccentFillDisabledBrushKey;
			if (val == null)
			{
				ThemeResourceKey val2 = new ThemeResourceKey(Category, "AccentFillDisabled", (ThemeResourceKeyType)3);
				_AccentFillDisabledBrushKey = val2;
				val = val2;
			}
			return val;
		}
	}

	public static ThemeResourceKey AccentFillSecondaryColorKey
	{
		get
		{
			//IL_0014: Unknown result type (might be due to invalid IL or missing references)
			//IL_0019: Unknown result type (might be due to invalid IL or missing references)
			//IL_001f: Expected Obj, but got Unknown
			ThemeResourceKey val = _AccentFillSecondaryColorKey;
			if (val == null)
			{
				ThemeResourceKey val2 = new ThemeResourceKey(Category, "AccentFillSecondary", (ThemeResourceKeyType)1);
				_AccentFillSecondaryColorKey = val2;
				val = val2;
			}
			return val;
		}
	}

	public static ThemeResourceKey AccentFillSecondaryBrushKey
	{
		get
		{
			//IL_0014: Unknown result type (might be due to invalid IL or missing references)
			//IL_0019: Unknown result type (might be due to invalid IL or missing references)
			//IL_001f: Expected Obj, but got Unknown
			ThemeResourceKey val = _AccentFillSecondaryBrushKey;
			if (val == null)
			{
				ThemeResourceKey val2 = new ThemeResourceKey(Category, "AccentFillSecondary", (ThemeResourceKeyType)3);
				_AccentFillSecondaryBrushKey = val2;
				val = val2;
			}
			return val;
		}
	}

	public static ThemeResourceKey AccentFillSelectedTextBackgroundColorKey
	{
		get
		{
			//IL_0014: Unknown result type (might be due to invalid IL or missing references)
			//IL_0019: Unknown result type (might be due to invalid IL or missing references)
			//IL_001f: Expected Obj, but got Unknown
			ThemeResourceKey val = _AccentFillSelectedTextBackgroundColorKey;
			if (val == null)
			{
				ThemeResourceKey val2 = new ThemeResourceKey(Category, "AccentFillSelectedTextBackground", (ThemeResourceKeyType)1);
				_AccentFillSelectedTextBackgroundColorKey = val2;
				val = val2;
			}
			return val;
		}
	}

	public static ThemeResourceKey AccentFillSelectedTextBackgroundBrushKey
	{
		get
		{
			//IL_0014: Unknown result type (might be due to invalid IL or missing references)
			//IL_0019: Unknown result type (might be due to invalid IL or missing references)
			//IL_001f: Expected Obj, but got Unknown
			ThemeResourceKey val = _AccentFillSelectedTextBackgroundBrushKey;
			if (val == null)
			{
				ThemeResourceKey val2 = new ThemeResourceKey(Category, "AccentFillSelectedTextBackground", (ThemeResourceKeyType)3);
				_AccentFillSelectedTextBackgroundBrushKey = val2;
				val = val2;
			}
			return val;
		}
	}

	public static ThemeResourceKey AccentFillSelectedTextBackgroundSubtleColorKey
	{
		get
		{
			//IL_0014: Unknown result type (might be due to invalid IL or missing references)
			//IL_0019: Unknown result type (might be due to invalid IL or missing references)
			//IL_001f: Expected Obj, but got Unknown
			ThemeResourceKey val = _AccentFillSelectedTextBackgroundSubtleColorKey;
			if (val == null)
			{
				ThemeResourceKey val2 = new ThemeResourceKey(Category, "AccentFillSelectedTextBackgroundSubtle", (ThemeResourceKeyType)1);
				_AccentFillSelectedTextBackgroundSubtleColorKey = val2;
				val = val2;
			}
			return val;
		}
	}

	public static ThemeResourceKey AccentFillSelectedTextBackgroundSubtleBrushKey
	{
		get
		{
			//IL_0014: Unknown result type (might be due to invalid IL or missing references)
			//IL_0019: Unknown result type (might be due to invalid IL or missing references)
			//IL_001f: Expected Obj, but got Unknown
			ThemeResourceKey val = _AccentFillSelectedTextBackgroundSubtleBrushKey;
			if (val == null)
			{
				ThemeResourceKey val2 = new ThemeResourceKey(Category, "AccentFillSelectedTextBackgroundSubtle", (ThemeResourceKeyType)3);
				_AccentFillSelectedTextBackgroundSubtleBrushKey = val2;
				val = val2;
			}
			return val;
		}
	}

	public static ThemeResourceKey AccentFillSenaryColorKey
	{
		get
		{
			//IL_0014: Unknown result type (might be due to invalid IL or missing references)
			//IL_0019: Unknown result type (might be due to invalid IL or missing references)
			//IL_001f: Expected Obj, but got Unknown
			ThemeResourceKey val = _AccentFillSenaryColorKey;
			if (val == null)
			{
				ThemeResourceKey val2 = new ThemeResourceKey(Category, "AccentFillSenary", (ThemeResourceKeyType)1);
				_AccentFillSenaryColorKey = val2;
				val = val2;
			}
			return val;
		}
	}

	public static ThemeResourceKey AccentFillSenaryBrushKey
	{
		get
		{
			//IL_0014: Unknown result type (might be due to invalid IL or missing references)
			//IL_0019: Unknown result type (might be due to invalid IL or missing references)
			//IL_001f: Expected Obj, but got Unknown
			ThemeResourceKey val = _AccentFillSenaryBrushKey;
			if (val == null)
			{
				ThemeResourceKey val2 = new ThemeResourceKey(Category, "AccentFillSenary", (ThemeResourceKeyType)3);
				_AccentFillSenaryBrushKey = val2;
				val = val2;
			}
			return val;
		}
	}

	public static ThemeResourceKey AccentFillTertiaryColorKey
	{
		get
		{
			//IL_0014: Unknown result type (might be due to invalid IL or missing references)
			//IL_0019: Unknown result type (might be due to invalid IL or missing references)
			//IL_001f: Expected Obj, but got Unknown
			ThemeResourceKey val = _AccentFillTertiaryColorKey;
			if (val == null)
			{
				ThemeResourceKey val2 = new ThemeResourceKey(Category, "AccentFillTertiary", (ThemeResourceKeyType)1);
				_AccentFillTertiaryColorKey = val2;
				val = val2;
			}
			return val;
		}
	}

	public static ThemeResourceKey AccentFillTertiaryBrushKey
	{
		get
		{
			//IL_0014: Unknown result type (might be due to invalid IL or missing references)
			//IL_0019: Unknown result type (might be due to invalid IL or missing references)
			//IL_001f: Expected Obj, but got Unknown
			ThemeResourceKey val = _AccentFillTertiaryBrushKey;
			if (val == null)
			{
				ThemeResourceKey val2 = new ThemeResourceKey(Category, "AccentFillTertiary", (ThemeResourceKeyType)3);
				_AccentFillTertiaryBrushKey = val2;
				val = val2;
			}
			return val;
		}
	}

	public static ThemeResourceKey AccentTextFillDisabledColorKey
	{
		get
		{
			//IL_0014: Unknown result type (might be due to invalid IL or missing references)
			//IL_0019: Unknown result type (might be due to invalid IL or missing references)
			//IL_001f: Expected Obj, but got Unknown
			ThemeResourceKey val = _AccentTextFillDisabledColorKey;
			if (val == null)
			{
				ThemeResourceKey val2 = new ThemeResourceKey(Category, "AccentTextFillDisabled", (ThemeResourceKeyType)1);
				_AccentTextFillDisabledColorKey = val2;
				val = val2;
			}
			return val;
		}
	}

	public static ThemeResourceKey AccentTextFillDisabledBrushKey
	{
		get
		{
			//IL_0014: Unknown result type (might be due to invalid IL or missing references)
			//IL_0019: Unknown result type (might be due to invalid IL or missing references)
			//IL_001f: Expected Obj, but got Unknown
			ThemeResourceKey val = _AccentTextFillDisabledBrushKey;
			if (val == null)
			{
				ThemeResourceKey val2 = new ThemeResourceKey(Category, "AccentTextFillDisabled", (ThemeResourceKeyType)3);
				_AccentTextFillDisabledBrushKey = val2;
				val = val2;
			}
			return val;
		}
	}

	public static ThemeResourceKey AccentTextFillPrimaryColorKey
	{
		get
		{
			//IL_0014: Unknown result type (might be due to invalid IL or missing references)
			//IL_0019: Unknown result type (might be due to invalid IL or missing references)
			//IL_001f: Expected Obj, but got Unknown
			ThemeResourceKey val = _AccentTextFillPrimaryColorKey;
			if (val == null)
			{
				ThemeResourceKey val2 = new ThemeResourceKey(Category, "AccentTextFillPrimary", (ThemeResourceKeyType)1);
				_AccentTextFillPrimaryColorKey = val2;
				val = val2;
			}
			return val;
		}
	}

	public static ThemeResourceKey AccentTextFillPrimaryBrushKey
	{
		get
		{
			//IL_0014: Unknown result type (might be due to invalid IL or missing references)
			//IL_0019: Unknown result type (might be due to invalid IL or missing references)
			//IL_001f: Expected Obj, but got Unknown
			ThemeResourceKey val = _AccentTextFillPrimaryBrushKey;
			if (val == null)
			{
				ThemeResourceKey val2 = new ThemeResourceKey(Category, "AccentTextFillPrimary", (ThemeResourceKeyType)3);
				_AccentTextFillPrimaryBrushKey = val2;
				val = val2;
			}
			return val;
		}
	}

	public static ThemeResourceKey AccentTextFillSecondaryColorKey
	{
		get
		{
			//IL_0014: Unknown result type (might be due to invalid IL or missing references)
			//IL_0019: Unknown result type (might be due to invalid IL or missing references)
			//IL_001f: Expected Obj, but got Unknown
			ThemeResourceKey val = _AccentTextFillSecondaryColorKey;
			if (val == null)
			{
				ThemeResourceKey val2 = new ThemeResourceKey(Category, "AccentTextFillSecondary", (ThemeResourceKeyType)1);
				_AccentTextFillSecondaryColorKey = val2;
				val = val2;
			}
			return val;
		}
	}

	public static ThemeResourceKey AccentTextFillSecondaryBrushKey
	{
		get
		{
			//IL_0014: Unknown result type (might be due to invalid IL or missing references)
			//IL_0019: Unknown result type (might be due to invalid IL or missing references)
			//IL_001f: Expected Obj, but got Unknown
			ThemeResourceKey val = _AccentTextFillSecondaryBrushKey;
			if (val == null)
			{
				ThemeResourceKey val2 = new ThemeResourceKey(Category, "AccentTextFillSecondary", (ThemeResourceKeyType)3);
				_AccentTextFillSecondaryBrushKey = val2;
				val = val2;
			}
			return val;
		}
	}

	public static ThemeResourceKey AccentTextFillTertiaryColorKey
	{
		get
		{
			//IL_0014: Unknown result type (might be due to invalid IL or missing references)
			//IL_0019: Unknown result type (might be due to invalid IL or missing references)
			//IL_001f: Expected Obj, but got Unknown
			ThemeResourceKey val = _AccentTextFillTertiaryColorKey;
			if (val == null)
			{
				ThemeResourceKey val2 = new ThemeResourceKey(Category, "AccentTextFillTertiary", (ThemeResourceKeyType)1);
				_AccentTextFillTertiaryColorKey = val2;
				val = val2;
			}
			return val;
		}
	}

	public static ThemeResourceKey AccentTextFillTertiaryBrushKey
	{
		get
		{
			//IL_0014: Unknown result type (might be due to invalid IL or missing references)
			//IL_0019: Unknown result type (might be due to invalid IL or missing references)
			//IL_001f: Expected Obj, but got Unknown
			ThemeResourceKey val = _AccentTextFillTertiaryBrushKey;
			if (val == null)
			{
				ThemeResourceKey val2 = new ThemeResourceKey(Category, "AccentTextFillTertiary", (ThemeResourceKeyType)3);
				_AccentTextFillTertiaryBrushKey = val2;
				val = val2;
			}
			return val;
		}
	}

	public static ThemeResourceKey CardBackgroundFillDefaultColorKey
	{
		get
		{
			//IL_0014: Unknown result type (might be due to invalid IL or missing references)
			//IL_0019: Unknown result type (might be due to invalid IL or missing references)
			//IL_001f: Expected Obj, but got Unknown
			ThemeResourceKey val = _CardBackgroundFillDefaultColorKey;
			if (val == null)
			{
				ThemeResourceKey val2 = new ThemeResourceKey(Category, "CardBackgroundFillDefault", (ThemeResourceKeyType)1);
				_CardBackgroundFillDefaultColorKey = val2;
				val = val2;
			}
			return val;
		}
	}

	public static ThemeResourceKey CardBackgroundFillDefaultBrushKey
	{
		get
		{
			//IL_0014: Unknown result type (might be due to invalid IL or missing references)
			//IL_0019: Unknown result type (might be due to invalid IL or missing references)
			//IL_001f: Expected Obj, but got Unknown
			ThemeResourceKey val = _CardBackgroundFillDefaultBrushKey;
			if (val == null)
			{
				ThemeResourceKey val2 = new ThemeResourceKey(Category, "CardBackgroundFillDefault", (ThemeResourceKeyType)3);
				_CardBackgroundFillDefaultBrushKey = val2;
				val = val2;
			}
			return val;
		}
	}

	public static ThemeResourceKey CardBackgroundFillSecondaryColorKey
	{
		get
		{
			//IL_0014: Unknown result type (might be due to invalid IL or missing references)
			//IL_0019: Unknown result type (might be due to invalid IL or missing references)
			//IL_001f: Expected Obj, but got Unknown
			ThemeResourceKey val = _CardBackgroundFillSecondaryColorKey;
			if (val == null)
			{
				ThemeResourceKey val2 = new ThemeResourceKey(Category, "CardBackgroundFillSecondary", (ThemeResourceKeyType)1);
				_CardBackgroundFillSecondaryColorKey = val2;
				val = val2;
			}
			return val;
		}
	}

	public static ThemeResourceKey CardBackgroundFillSecondaryBrushKey
	{
		get
		{
			//IL_0014: Unknown result type (might be due to invalid IL or missing references)
			//IL_0019: Unknown result type (might be due to invalid IL or missing references)
			//IL_001f: Expected Obj, but got Unknown
			ThemeResourceKey val = _CardBackgroundFillSecondaryBrushKey;
			if (val == null)
			{
				ThemeResourceKey val2 = new ThemeResourceKey(Category, "CardBackgroundFillSecondary", (ThemeResourceKeyType)3);
				_CardBackgroundFillSecondaryBrushKey = val2;
				val = val2;
			}
			return val;
		}
	}

	public static ThemeResourceKey CardBackgroundFillTertiaryColorKey
	{
		get
		{
			//IL_0014: Unknown result type (might be due to invalid IL or missing references)
			//IL_0019: Unknown result type (might be due to invalid IL or missing references)
			//IL_001f: Expected Obj, but got Unknown
			ThemeResourceKey val = _CardBackgroundFillTertiaryColorKey;
			if (val == null)
			{
				ThemeResourceKey val2 = new ThemeResourceKey(Category, "CardBackgroundFillTertiary", (ThemeResourceKeyType)1);
				_CardBackgroundFillTertiaryColorKey = val2;
				val = val2;
			}
			return val;
		}
	}

	public static ThemeResourceKey CardBackgroundFillTertiaryBrushKey
	{
		get
		{
			//IL_0014: Unknown result type (might be due to invalid IL or missing references)
			//IL_0019: Unknown result type (might be due to invalid IL or missing references)
			//IL_001f: Expected Obj, but got Unknown
			ThemeResourceKey val = _CardBackgroundFillTertiaryBrushKey;
			if (val == null)
			{
				ThemeResourceKey val2 = new ThemeResourceKey(Category, "CardBackgroundFillTertiary", (ThemeResourceKeyType)3);
				_CardBackgroundFillTertiaryBrushKey = val2;
				val = val2;
			}
			return val;
		}
	}

	public static ThemeResourceKey CardStrokeDefaultColorKey
	{
		get
		{
			//IL_0014: Unknown result type (might be due to invalid IL or missing references)
			//IL_0019: Unknown result type (might be due to invalid IL or missing references)
			//IL_001f: Expected Obj, but got Unknown
			ThemeResourceKey val = _CardStrokeDefaultColorKey;
			if (val == null)
			{
				ThemeResourceKey val2 = new ThemeResourceKey(Category, "CardStrokeDefault", (ThemeResourceKeyType)1);
				_CardStrokeDefaultColorKey = val2;
				val = val2;
			}
			return val;
		}
	}

	public static ThemeResourceKey CardStrokeDefaultBrushKey
	{
		get
		{
			//IL_0014: Unknown result type (might be due to invalid IL or missing references)
			//IL_0019: Unknown result type (might be due to invalid IL or missing references)
			//IL_001f: Expected Obj, but got Unknown
			ThemeResourceKey val = _CardStrokeDefaultBrushKey;
			if (val == null)
			{
				ThemeResourceKey val2 = new ThemeResourceKey(Category, "CardStrokeDefault", (ThemeResourceKeyType)3);
				_CardStrokeDefaultBrushKey = val2;
				val = val2;
			}
			return val;
		}
	}

	public static ThemeResourceKey CardStrokeDefaultSolidColorKey
	{
		get
		{
			//IL_0014: Unknown result type (might be due to invalid IL or missing references)
			//IL_0019: Unknown result type (might be due to invalid IL or missing references)
			//IL_001f: Expected Obj, but got Unknown
			ThemeResourceKey val = _CardStrokeDefaultSolidColorKey;
			if (val == null)
			{
				ThemeResourceKey val2 = new ThemeResourceKey(Category, "CardStrokeDefaultSolid", (ThemeResourceKeyType)1);
				_CardStrokeDefaultSolidColorKey = val2;
				val = val2;
			}
			return val;
		}
	}

	public static ThemeResourceKey CardStrokeDefaultSolidBrushKey
	{
		get
		{
			//IL_0014: Unknown result type (might be due to invalid IL or missing references)
			//IL_0019: Unknown result type (might be due to invalid IL or missing references)
			//IL_001f: Expected Obj, but got Unknown
			ThemeResourceKey val = _CardStrokeDefaultSolidBrushKey;
			if (val == null)
			{
				ThemeResourceKey val2 = new ThemeResourceKey(Category, "CardStrokeDefaultSolid", (ThemeResourceKeyType)3);
				_CardStrokeDefaultSolidBrushKey = val2;
				val = val2;
			}
			return val;
		}
	}

	public static ThemeResourceKey CardStrokeDefaultSolidAltColorKey
	{
		get
		{
			//IL_0014: Unknown result type (might be due to invalid IL or missing references)
			//IL_0019: Unknown result type (might be due to invalid IL or missing references)
			//IL_001f: Expected Obj, but got Unknown
			ThemeResourceKey val = _CardStrokeDefaultSolidAltColorKey;
			if (val == null)
			{
				ThemeResourceKey val2 = new ThemeResourceKey(Category, "CardStrokeDefaultSolidAlt", (ThemeResourceKeyType)1);
				_CardStrokeDefaultSolidAltColorKey = val2;
				val = val2;
			}
			return val;
		}
	}

	public static ThemeResourceKey CardStrokeDefaultSolidAltBrushKey
	{
		get
		{
			//IL_0014: Unknown result type (might be due to invalid IL or missing references)
			//IL_0019: Unknown result type (might be due to invalid IL or missing references)
			//IL_001f: Expected Obj, but got Unknown
			ThemeResourceKey val = _CardStrokeDefaultSolidAltBrushKey;
			if (val == null)
			{
				ThemeResourceKey val2 = new ThemeResourceKey(Category, "CardStrokeDefaultSolidAlt", (ThemeResourceKeyType)3);
				_CardStrokeDefaultSolidAltBrushKey = val2;
				val = val2;
			}
			return val;
		}
	}

	public static ThemeResourceKey ControlAltFillDisabledColorKey
	{
		get
		{
			//IL_0014: Unknown result type (might be due to invalid IL or missing references)
			//IL_0019: Unknown result type (might be due to invalid IL or missing references)
			//IL_001f: Expected Obj, but got Unknown
			ThemeResourceKey val = _ControlAltFillDisabledColorKey;
			if (val == null)
			{
				ThemeResourceKey val2 = new ThemeResourceKey(Category, "ControlAltFillDisabled", (ThemeResourceKeyType)1);
				_ControlAltFillDisabledColorKey = val2;
				val = val2;
			}
			return val;
		}
	}

	public static ThemeResourceKey ControlAltFillDisabledBrushKey
	{
		get
		{
			//IL_0014: Unknown result type (might be due to invalid IL or missing references)
			//IL_0019: Unknown result type (might be due to invalid IL or missing references)
			//IL_001f: Expected Obj, but got Unknown
			ThemeResourceKey val = _ControlAltFillDisabledBrushKey;
			if (val == null)
			{
				ThemeResourceKey val2 = new ThemeResourceKey(Category, "ControlAltFillDisabled", (ThemeResourceKeyType)3);
				_ControlAltFillDisabledBrushKey = val2;
				val = val2;
			}
			return val;
		}
	}

	public static ThemeResourceKey ControlAltFillQuaternaryColorKey
	{
		get
		{
			//IL_0014: Unknown result type (might be due to invalid IL or missing references)
			//IL_0019: Unknown result type (might be due to invalid IL or missing references)
			//IL_001f: Expected Obj, but got Unknown
			ThemeResourceKey val = _ControlAltFillQuaternaryColorKey;
			if (val == null)
			{
				ThemeResourceKey val2 = new ThemeResourceKey(Category, "ControlAltFillQuaternary", (ThemeResourceKeyType)1);
				_ControlAltFillQuaternaryColorKey = val2;
				val = val2;
			}
			return val;
		}
	}

	public static ThemeResourceKey ControlAltFillQuaternaryBrushKey
	{
		get
		{
			//IL_0014: Unknown result type (might be due to invalid IL or missing references)
			//IL_0019: Unknown result type (might be due to invalid IL or missing references)
			//IL_001f: Expected Obj, but got Unknown
			ThemeResourceKey val = _ControlAltFillQuaternaryBrushKey;
			if (val == null)
			{
				ThemeResourceKey val2 = new ThemeResourceKey(Category, "ControlAltFillQuaternary", (ThemeResourceKeyType)3);
				_ControlAltFillQuaternaryBrushKey = val2;
				val = val2;
			}
			return val;
		}
	}

	public static ThemeResourceKey ControlAltFillSecondaryColorKey
	{
		get
		{
			//IL_0014: Unknown result type (might be due to invalid IL or missing references)
			//IL_0019: Unknown result type (might be due to invalid IL or missing references)
			//IL_001f: Expected Obj, but got Unknown
			ThemeResourceKey val = _ControlAltFillSecondaryColorKey;
			if (val == null)
			{
				ThemeResourceKey val2 = new ThemeResourceKey(Category, "ControlAltFillSecondary", (ThemeResourceKeyType)1);
				_ControlAltFillSecondaryColorKey = val2;
				val = val2;
			}
			return val;
		}
	}

	public static ThemeResourceKey ControlAltFillSecondaryBrushKey
	{
		get
		{
			//IL_0014: Unknown result type (might be due to invalid IL or missing references)
			//IL_0019: Unknown result type (might be due to invalid IL or missing references)
			//IL_001f: Expected Obj, but got Unknown
			ThemeResourceKey val = _ControlAltFillSecondaryBrushKey;
			if (val == null)
			{
				ThemeResourceKey val2 = new ThemeResourceKey(Category, "ControlAltFillSecondary", (ThemeResourceKeyType)3);
				_ControlAltFillSecondaryBrushKey = val2;
				val = val2;
			}
			return val;
		}
	}

	public static ThemeResourceKey ControlAltFillTertiaryColorKey
	{
		get
		{
			//IL_0014: Unknown result type (might be due to invalid IL or missing references)
			//IL_0019: Unknown result type (might be due to invalid IL or missing references)
			//IL_001f: Expected Obj, but got Unknown
			ThemeResourceKey val = _ControlAltFillTertiaryColorKey;
			if (val == null)
			{
				ThemeResourceKey val2 = new ThemeResourceKey(Category, "ControlAltFillTertiary", (ThemeResourceKeyType)1);
				_ControlAltFillTertiaryColorKey = val2;
				val = val2;
			}
			return val;
		}
	}

	public static ThemeResourceKey ControlAltFillTertiaryBrushKey
	{
		get
		{
			//IL_0014: Unknown result type (might be due to invalid IL or missing references)
			//IL_0019: Unknown result type (might be due to invalid IL or missing references)
			//IL_001f: Expected Obj, but got Unknown
			ThemeResourceKey val = _ControlAltFillTertiaryBrushKey;
			if (val == null)
			{
				ThemeResourceKey val2 = new ThemeResourceKey(Category, "ControlAltFillTertiary", (ThemeResourceKeyType)3);
				_ControlAltFillTertiaryBrushKey = val2;
				val = val2;
			}
			return val;
		}
	}

	public static ThemeResourceKey ControlAltFillTransparentColorKey
	{
		get
		{
			//IL_0014: Unknown result type (might be due to invalid IL or missing references)
			//IL_0019: Unknown result type (might be due to invalid IL or missing references)
			//IL_001f: Expected Obj, but got Unknown
			ThemeResourceKey val = _ControlAltFillTransparentColorKey;
			if (val == null)
			{
				ThemeResourceKey val2 = new ThemeResourceKey(Category, "ControlAltFillTransparent", (ThemeResourceKeyType)1);
				_ControlAltFillTransparentColorKey = val2;
				val = val2;
			}
			return val;
		}
	}

	public static ThemeResourceKey ControlAltFillTransparentBrushKey
	{
		get
		{
			//IL_0014: Unknown result type (might be due to invalid IL or missing references)
			//IL_0019: Unknown result type (might be due to invalid IL or missing references)
			//IL_001f: Expected Obj, but got Unknown
			ThemeResourceKey val = _ControlAltFillTransparentBrushKey;
			if (val == null)
			{
				ThemeResourceKey val2 = new ThemeResourceKey(Category, "ControlAltFillTransparent", (ThemeResourceKeyType)3);
				_ControlAltFillTransparentBrushKey = val2;
				val = val2;
			}
			return val;
		}
	}

	public static ThemeResourceKey ControlFillActiveInputColorKey
	{
		get
		{
			//IL_0014: Unknown result type (might be due to invalid IL or missing references)
			//IL_0019: Unknown result type (might be due to invalid IL or missing references)
			//IL_001f: Expected Obj, but got Unknown
			ThemeResourceKey val = _ControlFillActiveInputColorKey;
			if (val == null)
			{
				ThemeResourceKey val2 = new ThemeResourceKey(Category, "ControlFillActiveInput", (ThemeResourceKeyType)1);
				_ControlFillActiveInputColorKey = val2;
				val = val2;
			}
			return val;
		}
	}

	public static ThemeResourceKey ControlFillActiveInputBrushKey
	{
		get
		{
			//IL_0014: Unknown result type (might be due to invalid IL or missing references)
			//IL_0019: Unknown result type (might be due to invalid IL or missing references)
			//IL_001f: Expected Obj, but got Unknown
			ThemeResourceKey val = _ControlFillActiveInputBrushKey;
			if (val == null)
			{
				ThemeResourceKey val2 = new ThemeResourceKey(Category, "ControlFillActiveInput", (ThemeResourceKeyType)3);
				_ControlFillActiveInputBrushKey = val2;
				val = val2;
			}
			return val;
		}
	}

	public static ThemeResourceKey ControlFillDefaultColorKey
	{
		get
		{
			//IL_0014: Unknown result type (might be due to invalid IL or missing references)
			//IL_0019: Unknown result type (might be due to invalid IL or missing references)
			//IL_001f: Expected Obj, but got Unknown
			ThemeResourceKey val = _ControlFillDefaultColorKey;
			if (val == null)
			{
				ThemeResourceKey val2 = new ThemeResourceKey(Category, "ControlFillDefault", (ThemeResourceKeyType)1);
				_ControlFillDefaultColorKey = val2;
				val = val2;
			}
			return val;
		}
	}

	public static ThemeResourceKey ControlFillDefaultBrushKey
	{
		get
		{
			//IL_0014: Unknown result type (might be due to invalid IL or missing references)
			//IL_0019: Unknown result type (might be due to invalid IL or missing references)
			//IL_001f: Expected Obj, but got Unknown
			ThemeResourceKey val = _ControlFillDefaultBrushKey;
			if (val == null)
			{
				ThemeResourceKey val2 = new ThemeResourceKey(Category, "ControlFillDefault", (ThemeResourceKeyType)3);
				_ControlFillDefaultBrushKey = val2;
				val = val2;
			}
			return val;
		}
	}

	public static ThemeResourceKey ControlFillDisabledColorKey
	{
		get
		{
			//IL_0014: Unknown result type (might be due to invalid IL or missing references)
			//IL_0019: Unknown result type (might be due to invalid IL or missing references)
			//IL_001f: Expected Obj, but got Unknown
			ThemeResourceKey val = _ControlFillDisabledColorKey;
			if (val == null)
			{
				ThemeResourceKey val2 = new ThemeResourceKey(Category, "ControlFillDisabled", (ThemeResourceKeyType)1);
				_ControlFillDisabledColorKey = val2;
				val = val2;
			}
			return val;
		}
	}

	public static ThemeResourceKey ControlFillDisabledBrushKey
	{
		get
		{
			//IL_0014: Unknown result type (might be due to invalid IL or missing references)
			//IL_0019: Unknown result type (might be due to invalid IL or missing references)
			//IL_001f: Expected Obj, but got Unknown
			ThemeResourceKey val = _ControlFillDisabledBrushKey;
			if (val == null)
			{
				ThemeResourceKey val2 = new ThemeResourceKey(Category, "ControlFillDisabled", (ThemeResourceKeyType)3);
				_ControlFillDisabledBrushKey = val2;
				val = val2;
			}
			return val;
		}
	}

	public static ThemeResourceKey ControlFillQuaternaryColorKey
	{
		get
		{
			//IL_0014: Unknown result type (might be due to invalid IL or missing references)
			//IL_0019: Unknown result type (might be due to invalid IL or missing references)
			//IL_001f: Expected Obj, but got Unknown
			ThemeResourceKey val = _ControlFillQuaternaryColorKey;
			if (val == null)
			{
				ThemeResourceKey val2 = new ThemeResourceKey(Category, "ControlFillQuaternary", (ThemeResourceKeyType)1);
				_ControlFillQuaternaryColorKey = val2;
				val = val2;
			}
			return val;
		}
	}

	public static ThemeResourceKey ControlFillQuaternaryBrushKey
	{
		get
		{
			//IL_0014: Unknown result type (might be due to invalid IL or missing references)
			//IL_0019: Unknown result type (might be due to invalid IL or missing references)
			//IL_001f: Expected Obj, but got Unknown
			ThemeResourceKey val = _ControlFillQuaternaryBrushKey;
			if (val == null)
			{
				ThemeResourceKey val2 = new ThemeResourceKey(Category, "ControlFillQuaternary", (ThemeResourceKeyType)3);
				_ControlFillQuaternaryBrushKey = val2;
				val = val2;
			}
			return val;
		}
	}

	public static ThemeResourceKey ControlFillSecondaryColorKey
	{
		get
		{
			//IL_0014: Unknown result type (might be due to invalid IL or missing references)
			//IL_0019: Unknown result type (might be due to invalid IL or missing references)
			//IL_001f: Expected Obj, but got Unknown
			ThemeResourceKey val = _ControlFillSecondaryColorKey;
			if (val == null)
			{
				ThemeResourceKey val2 = new ThemeResourceKey(Category, "ControlFillSecondary", (ThemeResourceKeyType)1);
				_ControlFillSecondaryColorKey = val2;
				val = val2;
			}
			return val;
		}
	}

	public static ThemeResourceKey ControlFillSecondaryBrushKey
	{
		get
		{
			//IL_0014: Unknown result type (might be due to invalid IL or missing references)
			//IL_0019: Unknown result type (might be due to invalid IL or missing references)
			//IL_001f: Expected Obj, but got Unknown
			ThemeResourceKey val = _ControlFillSecondaryBrushKey;
			if (val == null)
			{
				ThemeResourceKey val2 = new ThemeResourceKey(Category, "ControlFillSecondary", (ThemeResourceKeyType)3);
				_ControlFillSecondaryBrushKey = val2;
				val = val2;
			}
			return val;
		}
	}

	public static ThemeResourceKey ControlFillTertiaryColorKey
	{
		get
		{
			//IL_0014: Unknown result type (might be due to invalid IL or missing references)
			//IL_0019: Unknown result type (might be due to invalid IL or missing references)
			//IL_001f: Expected Obj, but got Unknown
			ThemeResourceKey val = _ControlFillTertiaryColorKey;
			if (val == null)
			{
				ThemeResourceKey val2 = new ThemeResourceKey(Category, "ControlFillTertiary", (ThemeResourceKeyType)1);
				_ControlFillTertiaryColorKey = val2;
				val = val2;
			}
			return val;
		}
	}

	public static ThemeResourceKey ControlFillTertiaryBrushKey
	{
		get
		{
			//IL_0014: Unknown result type (might be due to invalid IL or missing references)
			//IL_0019: Unknown result type (might be due to invalid IL or missing references)
			//IL_001f: Expected Obj, but got Unknown
			ThemeResourceKey val = _ControlFillTertiaryBrushKey;
			if (val == null)
			{
				ThemeResourceKey val2 = new ThemeResourceKey(Category, "ControlFillTertiary", (ThemeResourceKeyType)3);
				_ControlFillTertiaryBrushKey = val2;
				val = val2;
			}
			return val;
		}
	}

	public static ThemeResourceKey ControlFillTransparentColorKey
	{
		get
		{
			//IL_0014: Unknown result type (might be due to invalid IL or missing references)
			//IL_0019: Unknown result type (might be due to invalid IL or missing references)
			//IL_001f: Expected Obj, but got Unknown
			ThemeResourceKey val = _ControlFillTransparentColorKey;
			if (val == null)
			{
				ThemeResourceKey val2 = new ThemeResourceKey(Category, "ControlFillTransparent", (ThemeResourceKeyType)1);
				_ControlFillTransparentColorKey = val2;
				val = val2;
			}
			return val;
		}
	}

	public static ThemeResourceKey ControlFillTransparentBrushKey
	{
		get
		{
			//IL_0014: Unknown result type (might be due to invalid IL or missing references)
			//IL_0019: Unknown result type (might be due to invalid IL or missing references)
			//IL_001f: Expected Obj, but got Unknown
			ThemeResourceKey val = _ControlFillTransparentBrushKey;
			if (val == null)
			{
				ThemeResourceKey val2 = new ThemeResourceKey(Category, "ControlFillTransparent", (ThemeResourceKeyType)3);
				_ControlFillTransparentBrushKey = val2;
				val = val2;
			}
			return val;
		}
	}

	public static ThemeResourceKey ControlOnImageFillDefaultColorKey
	{
		get
		{
			//IL_0014: Unknown result type (might be due to invalid IL or missing references)
			//IL_0019: Unknown result type (might be due to invalid IL or missing references)
			//IL_001f: Expected Obj, but got Unknown
			ThemeResourceKey val = _ControlOnImageFillDefaultColorKey;
			if (val == null)
			{
				ThemeResourceKey val2 = new ThemeResourceKey(Category, "ControlOnImageFillDefault", (ThemeResourceKeyType)1);
				_ControlOnImageFillDefaultColorKey = val2;
				val = val2;
			}
			return val;
		}
	}

	public static ThemeResourceKey ControlOnImageFillDefaultBrushKey
	{
		get
		{
			//IL_0014: Unknown result type (might be due to invalid IL or missing references)
			//IL_0019: Unknown result type (might be due to invalid IL or missing references)
			//IL_001f: Expected Obj, but got Unknown
			ThemeResourceKey val = _ControlOnImageFillDefaultBrushKey;
			if (val == null)
			{
				ThemeResourceKey val2 = new ThemeResourceKey(Category, "ControlOnImageFillDefault", (ThemeResourceKeyType)3);
				_ControlOnImageFillDefaultBrushKey = val2;
				val = val2;
			}
			return val;
		}
	}

	public static ThemeResourceKey ControlOnImageFillDisabledColorKey
	{
		get
		{
			//IL_0014: Unknown result type (might be due to invalid IL or missing references)
			//IL_0019: Unknown result type (might be due to invalid IL or missing references)
			//IL_001f: Expected Obj, but got Unknown
			ThemeResourceKey val = _ControlOnImageFillDisabledColorKey;
			if (val == null)
			{
				ThemeResourceKey val2 = new ThemeResourceKey(Category, "ControlOnImageFillDisabled", (ThemeResourceKeyType)1);
				_ControlOnImageFillDisabledColorKey = val2;
				val = val2;
			}
			return val;
		}
	}

	public static ThemeResourceKey ControlOnImageFillDisabledBrushKey
	{
		get
		{
			//IL_0014: Unknown result type (might be due to invalid IL or missing references)
			//IL_0019: Unknown result type (might be due to invalid IL or missing references)
			//IL_001f: Expected Obj, but got Unknown
			ThemeResourceKey val = _ControlOnImageFillDisabledBrushKey;
			if (val == null)
			{
				ThemeResourceKey val2 = new ThemeResourceKey(Category, "ControlOnImageFillDisabled", (ThemeResourceKeyType)3);
				_ControlOnImageFillDisabledBrushKey = val2;
				val = val2;
			}
			return val;
		}
	}

	public static ThemeResourceKey ControlOnImageFillSecondaryColorKey
	{
		get
		{
			//IL_0014: Unknown result type (might be due to invalid IL or missing references)
			//IL_0019: Unknown result type (might be due to invalid IL or missing references)
			//IL_001f: Expected Obj, but got Unknown
			ThemeResourceKey val = _ControlOnImageFillSecondaryColorKey;
			if (val == null)
			{
				ThemeResourceKey val2 = new ThemeResourceKey(Category, "ControlOnImageFillSecondary", (ThemeResourceKeyType)1);
				_ControlOnImageFillSecondaryColorKey = val2;
				val = val2;
			}
			return val;
		}
	}

	public static ThemeResourceKey ControlOnImageFillSecondaryBrushKey
	{
		get
		{
			//IL_0014: Unknown result type (might be due to invalid IL or missing references)
			//IL_0019: Unknown result type (might be due to invalid IL or missing references)
			//IL_001f: Expected Obj, but got Unknown
			ThemeResourceKey val = _ControlOnImageFillSecondaryBrushKey;
			if (val == null)
			{
				ThemeResourceKey val2 = new ThemeResourceKey(Category, "ControlOnImageFillSecondary", (ThemeResourceKeyType)3);
				_ControlOnImageFillSecondaryBrushKey = val2;
				val = val2;
			}
			return val;
		}
	}

	public static ThemeResourceKey ControlOnImageFillTertiaryColorKey
	{
		get
		{
			//IL_0014: Unknown result type (might be due to invalid IL or missing references)
			//IL_0019: Unknown result type (might be due to invalid IL or missing references)
			//IL_001f: Expected Obj, but got Unknown
			ThemeResourceKey val = _ControlOnImageFillTertiaryColorKey;
			if (val == null)
			{
				ThemeResourceKey val2 = new ThemeResourceKey(Category, "ControlOnImageFillTertiary", (ThemeResourceKeyType)1);
				_ControlOnImageFillTertiaryColorKey = val2;
				val = val2;
			}
			return val;
		}
	}

	public static ThemeResourceKey ControlOnImageFillTertiaryBrushKey
	{
		get
		{
			//IL_0014: Unknown result type (might be due to invalid IL or missing references)
			//IL_0019: Unknown result type (might be due to invalid IL or missing references)
			//IL_001f: Expected Obj, but got Unknown
			ThemeResourceKey val = _ControlOnImageFillTertiaryBrushKey;
			if (val == null)
			{
				ThemeResourceKey val2 = new ThemeResourceKey(Category, "ControlOnImageFillTertiary", (ThemeResourceKeyType)3);
				_ControlOnImageFillTertiaryBrushKey = val2;
				val = val2;
			}
			return val;
		}
	}

	public static ThemeResourceKey ControlSolidFillDefaultColorKey
	{
		get
		{
			//IL_0014: Unknown result type (might be due to invalid IL or missing references)
			//IL_0019: Unknown result type (might be due to invalid IL or missing references)
			//IL_001f: Expected Obj, but got Unknown
			ThemeResourceKey val = _ControlSolidFillDefaultColorKey;
			if (val == null)
			{
				ThemeResourceKey val2 = new ThemeResourceKey(Category, "ControlSolidFillDefault", (ThemeResourceKeyType)1);
				_ControlSolidFillDefaultColorKey = val2;
				val = val2;
			}
			return val;
		}
	}

	public static ThemeResourceKey ControlSolidFillDefaultBrushKey
	{
		get
		{
			//IL_0014: Unknown result type (might be due to invalid IL or missing references)
			//IL_0019: Unknown result type (might be due to invalid IL or missing references)
			//IL_001f: Expected Obj, but got Unknown
			ThemeResourceKey val = _ControlSolidFillDefaultBrushKey;
			if (val == null)
			{
				ThemeResourceKey val2 = new ThemeResourceKey(Category, "ControlSolidFillDefault", (ThemeResourceKeyType)3);
				_ControlSolidFillDefaultBrushKey = val2;
				val = val2;
			}
			return val;
		}
	}

	public static ThemeResourceKey ControlStrokeDefaultColorKey
	{
		get
		{
			//IL_0014: Unknown result type (might be due to invalid IL or missing references)
			//IL_0019: Unknown result type (might be due to invalid IL or missing references)
			//IL_001f: Expected Obj, but got Unknown
			ThemeResourceKey val = _ControlStrokeDefaultColorKey;
			if (val == null)
			{
				ThemeResourceKey val2 = new ThemeResourceKey(Category, "ControlStrokeDefault", (ThemeResourceKeyType)1);
				_ControlStrokeDefaultColorKey = val2;
				val = val2;
			}
			return val;
		}
	}

	public static ThemeResourceKey ControlStrokeDefaultBrushKey
	{
		get
		{
			//IL_0014: Unknown result type (might be due to invalid IL or missing references)
			//IL_0019: Unknown result type (might be due to invalid IL or missing references)
			//IL_001f: Expected Obj, but got Unknown
			ThemeResourceKey val = _ControlStrokeDefaultBrushKey;
			if (val == null)
			{
				ThemeResourceKey val2 = new ThemeResourceKey(Category, "ControlStrokeDefault", (ThemeResourceKeyType)3);
				_ControlStrokeDefaultBrushKey = val2;
				val = val2;
			}
			return val;
		}
	}

	public static ThemeResourceKey ControlStrokeForStrongFillWhenOnImageColorKey
	{
		get
		{
			//IL_0014: Unknown result type (might be due to invalid IL or missing references)
			//IL_0019: Unknown result type (might be due to invalid IL or missing references)
			//IL_001f: Expected Obj, but got Unknown
			ThemeResourceKey val = _ControlStrokeForStrongFillWhenOnImageColorKey;
			if (val == null)
			{
				ThemeResourceKey val2 = new ThemeResourceKey(Category, "ControlStrokeForStrongFillWhenOnImage", (ThemeResourceKeyType)1);
				_ControlStrokeForStrongFillWhenOnImageColorKey = val2;
				val = val2;
			}
			return val;
		}
	}

	public static ThemeResourceKey ControlStrokeForStrongFillWhenOnImageBrushKey
	{
		get
		{
			//IL_0014: Unknown result type (might be due to invalid IL or missing references)
			//IL_0019: Unknown result type (might be due to invalid IL or missing references)
			//IL_001f: Expected Obj, but got Unknown
			ThemeResourceKey val = _ControlStrokeForStrongFillWhenOnImageBrushKey;
			if (val == null)
			{
				ThemeResourceKey val2 = new ThemeResourceKey(Category, "ControlStrokeForStrongFillWhenOnImage", (ThemeResourceKeyType)3);
				_ControlStrokeForStrongFillWhenOnImageBrushKey = val2;
				val = val2;
			}
			return val;
		}
	}

	public static ThemeResourceKey ControlStrokeOnAccentDefaultColorKey
	{
		get
		{
			//IL_0014: Unknown result type (might be due to invalid IL or missing references)
			//IL_0019: Unknown result type (might be due to invalid IL or missing references)
			//IL_001f: Expected Obj, but got Unknown
			ThemeResourceKey val = _ControlStrokeOnAccentDefaultColorKey;
			if (val == null)
			{
				ThemeResourceKey val2 = new ThemeResourceKey(Category, "ControlStrokeOnAccentDefault", (ThemeResourceKeyType)1);
				_ControlStrokeOnAccentDefaultColorKey = val2;
				val = val2;
			}
			return val;
		}
	}

	public static ThemeResourceKey ControlStrokeOnAccentDefaultBrushKey
	{
		get
		{
			//IL_0014: Unknown result type (might be due to invalid IL or missing references)
			//IL_0019: Unknown result type (might be due to invalid IL or missing references)
			//IL_001f: Expected Obj, but got Unknown
			ThemeResourceKey val = _ControlStrokeOnAccentDefaultBrushKey;
			if (val == null)
			{
				ThemeResourceKey val2 = new ThemeResourceKey(Category, "ControlStrokeOnAccentDefault", (ThemeResourceKeyType)3);
				_ControlStrokeOnAccentDefaultBrushKey = val2;
				val = val2;
			}
			return val;
		}
	}

	public static ThemeResourceKey ControlStrokeOnAccentDisabledColorKey
	{
		get
		{
			//IL_0014: Unknown result type (might be due to invalid IL or missing references)
			//IL_0019: Unknown result type (might be due to invalid IL or missing references)
			//IL_001f: Expected Obj, but got Unknown
			ThemeResourceKey val = _ControlStrokeOnAccentDisabledColorKey;
			if (val == null)
			{
				ThemeResourceKey val2 = new ThemeResourceKey(Category, "ControlStrokeOnAccentDisabled", (ThemeResourceKeyType)1);
				_ControlStrokeOnAccentDisabledColorKey = val2;
				val = val2;
			}
			return val;
		}
	}

	public static ThemeResourceKey ControlStrokeOnAccentDisabledBrushKey
	{
		get
		{
			//IL_0014: Unknown result type (might be due to invalid IL or missing references)
			//IL_0019: Unknown result type (might be due to invalid IL or missing references)
			//IL_001f: Expected Obj, but got Unknown
			ThemeResourceKey val = _ControlStrokeOnAccentDisabledBrushKey;
			if (val == null)
			{
				ThemeResourceKey val2 = new ThemeResourceKey(Category, "ControlStrokeOnAccentDisabled", (ThemeResourceKeyType)3);
				_ControlStrokeOnAccentDisabledBrushKey = val2;
				val = val2;
			}
			return val;
		}
	}

	public static ThemeResourceKey ControlStrokeOnAccentSecondaryColorKey
	{
		get
		{
			//IL_0014: Unknown result type (might be due to invalid IL or missing references)
			//IL_0019: Unknown result type (might be due to invalid IL or missing references)
			//IL_001f: Expected Obj, but got Unknown
			ThemeResourceKey val = _ControlStrokeOnAccentSecondaryColorKey;
			if (val == null)
			{
				ThemeResourceKey val2 = new ThemeResourceKey(Category, "ControlStrokeOnAccentSecondary", (ThemeResourceKeyType)1);
				_ControlStrokeOnAccentSecondaryColorKey = val2;
				val = val2;
			}
			return val;
		}
	}

	public static ThemeResourceKey ControlStrokeOnAccentSecondaryBrushKey
	{
		get
		{
			//IL_0014: Unknown result type (might be due to invalid IL or missing references)
			//IL_0019: Unknown result type (might be due to invalid IL or missing references)
			//IL_001f: Expected Obj, but got Unknown
			ThemeResourceKey val = _ControlStrokeOnAccentSecondaryBrushKey;
			if (val == null)
			{
				ThemeResourceKey val2 = new ThemeResourceKey(Category, "ControlStrokeOnAccentSecondary", (ThemeResourceKeyType)3);
				_ControlStrokeOnAccentSecondaryBrushKey = val2;
				val = val2;
			}
			return val;
		}
	}

	public static ThemeResourceKey ControlStrokeOnAccentTertiaryColorKey
	{
		get
		{
			//IL_0014: Unknown result type (might be due to invalid IL or missing references)
			//IL_0019: Unknown result type (might be due to invalid IL or missing references)
			//IL_001f: Expected Obj, but got Unknown
			ThemeResourceKey val = _ControlStrokeOnAccentTertiaryColorKey;
			if (val == null)
			{
				ThemeResourceKey val2 = new ThemeResourceKey(Category, "ControlStrokeOnAccentTertiary", (ThemeResourceKeyType)1);
				_ControlStrokeOnAccentTertiaryColorKey = val2;
				val = val2;
			}
			return val;
		}
	}

	public static ThemeResourceKey ControlStrokeOnAccentTertiaryBrushKey
	{
		get
		{
			//IL_0014: Unknown result type (might be due to invalid IL or missing references)
			//IL_0019: Unknown result type (might be due to invalid IL or missing references)
			//IL_001f: Expected Obj, but got Unknown
			ThemeResourceKey val = _ControlStrokeOnAccentTertiaryBrushKey;
			if (val == null)
			{
				ThemeResourceKey val2 = new ThemeResourceKey(Category, "ControlStrokeOnAccentTertiary", (ThemeResourceKeyType)3);
				_ControlStrokeOnAccentTertiaryBrushKey = val2;
				val = val2;
			}
			return val;
		}
	}

	public static ThemeResourceKey ControlStrokeSecondaryColorKey
	{
		get
		{
			//IL_0014: Unknown result type (might be due to invalid IL or missing references)
			//IL_0019: Unknown result type (might be due to invalid IL or missing references)
			//IL_001f: Expected Obj, but got Unknown
			ThemeResourceKey val = _ControlStrokeSecondaryColorKey;
			if (val == null)
			{
				ThemeResourceKey val2 = new ThemeResourceKey(Category, "ControlStrokeSecondary", (ThemeResourceKeyType)1);
				_ControlStrokeSecondaryColorKey = val2;
				val = val2;
			}
			return val;
		}
	}

	public static ThemeResourceKey ControlStrokeSecondaryBrushKey
	{
		get
		{
			//IL_0014: Unknown result type (might be due to invalid IL or missing references)
			//IL_0019: Unknown result type (might be due to invalid IL or missing references)
			//IL_001f: Expected Obj, but got Unknown
			ThemeResourceKey val = _ControlStrokeSecondaryBrushKey;
			if (val == null)
			{
				ThemeResourceKey val2 = new ThemeResourceKey(Category, "ControlStrokeSecondary", (ThemeResourceKeyType)3);
				_ControlStrokeSecondaryBrushKey = val2;
				val = val2;
			}
			return val;
		}
	}

	public static ThemeResourceKey ControlStrokeTransparentColorKey
	{
		get
		{
			//IL_0014: Unknown result type (might be due to invalid IL or missing references)
			//IL_0019: Unknown result type (might be due to invalid IL or missing references)
			//IL_001f: Expected Obj, but got Unknown
			ThemeResourceKey val = _ControlStrokeTransparentColorKey;
			if (val == null)
			{
				ThemeResourceKey val2 = new ThemeResourceKey(Category, "ControlStrokeTransparent", (ThemeResourceKeyType)1);
				_ControlStrokeTransparentColorKey = val2;
				val = val2;
			}
			return val;
		}
	}

	public static ThemeResourceKey ControlStrokeTransparentBrushKey
	{
		get
		{
			//IL_0014: Unknown result type (might be due to invalid IL or missing references)
			//IL_0019: Unknown result type (might be due to invalid IL or missing references)
			//IL_001f: Expected Obj, but got Unknown
			ThemeResourceKey val = _ControlStrokeTransparentBrushKey;
			if (val == null)
			{
				ThemeResourceKey val2 = new ThemeResourceKey(Category, "ControlStrokeTransparent", (ThemeResourceKeyType)3);
				_ControlStrokeTransparentBrushKey = val2;
				val = val2;
			}
			return val;
		}
	}

	public static ThemeResourceKey ControlStrongFillDefaultColorKey
	{
		get
		{
			//IL_0014: Unknown result type (might be due to invalid IL or missing references)
			//IL_0019: Unknown result type (might be due to invalid IL or missing references)
			//IL_001f: Expected Obj, but got Unknown
			ThemeResourceKey val = _ControlStrongFillDefaultColorKey;
			if (val == null)
			{
				ThemeResourceKey val2 = new ThemeResourceKey(Category, "ControlStrongFillDefault", (ThemeResourceKeyType)1);
				_ControlStrongFillDefaultColorKey = val2;
				val = val2;
			}
			return val;
		}
	}

	public static ThemeResourceKey ControlStrongFillDefaultBrushKey
	{
		get
		{
			//IL_0014: Unknown result type (might be due to invalid IL or missing references)
			//IL_0019: Unknown result type (might be due to invalid IL or missing references)
			//IL_001f: Expected Obj, but got Unknown
			ThemeResourceKey val = _ControlStrongFillDefaultBrushKey;
			if (val == null)
			{
				ThemeResourceKey val2 = new ThemeResourceKey(Category, "ControlStrongFillDefault", (ThemeResourceKeyType)3);
				_ControlStrongFillDefaultBrushKey = val2;
				val = val2;
			}
			return val;
		}
	}

	public static ThemeResourceKey ControlStrongFillDisabledColorKey
	{
		get
		{
			//IL_0014: Unknown result type (might be due to invalid IL or missing references)
			//IL_0019: Unknown result type (might be due to invalid IL or missing references)
			//IL_001f: Expected Obj, but got Unknown
			ThemeResourceKey val = _ControlStrongFillDisabledColorKey;
			if (val == null)
			{
				ThemeResourceKey val2 = new ThemeResourceKey(Category, "ControlStrongFillDisabled", (ThemeResourceKeyType)1);
				_ControlStrongFillDisabledColorKey = val2;
				val = val2;
			}
			return val;
		}
	}

	public static ThemeResourceKey ControlStrongFillDisabledBrushKey
	{
		get
		{
			//IL_0014: Unknown result type (might be due to invalid IL or missing references)
			//IL_0019: Unknown result type (might be due to invalid IL or missing references)
			//IL_001f: Expected Obj, but got Unknown
			ThemeResourceKey val = _ControlStrongFillDisabledBrushKey;
			if (val == null)
			{
				ThemeResourceKey val2 = new ThemeResourceKey(Category, "ControlStrongFillDisabled", (ThemeResourceKeyType)3);
				_ControlStrongFillDisabledBrushKey = val2;
				val = val2;
			}
			return val;
		}
	}

	public static ThemeResourceKey ControlStrongStrokeDefaultColorKey
	{
		get
		{
			//IL_0014: Unknown result type (might be due to invalid IL or missing references)
			//IL_0019: Unknown result type (might be due to invalid IL or missing references)
			//IL_001f: Expected Obj, but got Unknown
			ThemeResourceKey val = _ControlStrongStrokeDefaultColorKey;
			if (val == null)
			{
				ThemeResourceKey val2 = new ThemeResourceKey(Category, "ControlStrongStrokeDefault", (ThemeResourceKeyType)1);
				_ControlStrongStrokeDefaultColorKey = val2;
				val = val2;
			}
			return val;
		}
	}

	public static ThemeResourceKey ControlStrongStrokeDefaultBrushKey
	{
		get
		{
			//IL_0014: Unknown result type (might be due to invalid IL or missing references)
			//IL_0019: Unknown result type (might be due to invalid IL or missing references)
			//IL_001f: Expected Obj, but got Unknown
			ThemeResourceKey val = _ControlStrongStrokeDefaultBrushKey;
			if (val == null)
			{
				ThemeResourceKey val2 = new ThemeResourceKey(Category, "ControlStrongStrokeDefault", (ThemeResourceKeyType)3);
				_ControlStrongStrokeDefaultBrushKey = val2;
				val = val2;
			}
			return val;
		}
	}

	public static ThemeResourceKey ControlStrongStrokeDisabledColorKey
	{
		get
		{
			//IL_0014: Unknown result type (might be due to invalid IL or missing references)
			//IL_0019: Unknown result type (might be due to invalid IL or missing references)
			//IL_001f: Expected Obj, but got Unknown
			ThemeResourceKey val = _ControlStrongStrokeDisabledColorKey;
			if (val == null)
			{
				ThemeResourceKey val2 = new ThemeResourceKey(Category, "ControlStrongStrokeDisabled", (ThemeResourceKeyType)1);
				_ControlStrongStrokeDisabledColorKey = val2;
				val = val2;
			}
			return val;
		}
	}

	public static ThemeResourceKey ControlStrongStrokeDisabledBrushKey
	{
		get
		{
			//IL_0014: Unknown result type (might be due to invalid IL or missing references)
			//IL_0019: Unknown result type (might be due to invalid IL or missing references)
			//IL_001f: Expected Obj, but got Unknown
			ThemeResourceKey val = _ControlStrongStrokeDisabledBrushKey;
			if (val == null)
			{
				ThemeResourceKey val2 = new ThemeResourceKey(Category, "ControlStrongStrokeDisabled", (ThemeResourceKeyType)3);
				_ControlStrongStrokeDisabledBrushKey = val2;
				val = val2;
			}
			return val;
		}
	}

	public static ThemeResourceKey DividerStrokeDefaultColorKey
	{
		get
		{
			//IL_0014: Unknown result type (might be due to invalid IL or missing references)
			//IL_0019: Unknown result type (might be due to invalid IL or missing references)
			//IL_001f: Expected Obj, but got Unknown
			ThemeResourceKey val = _DividerStrokeDefaultColorKey;
			if (val == null)
			{
				ThemeResourceKey val2 = new ThemeResourceKey(Category, "DividerStrokeDefault", (ThemeResourceKeyType)1);
				_DividerStrokeDefaultColorKey = val2;
				val = val2;
			}
			return val;
		}
	}

	public static ThemeResourceKey DividerStrokeDefaultBrushKey
	{
		get
		{
			//IL_0014: Unknown result type (might be due to invalid IL or missing references)
			//IL_0019: Unknown result type (might be due to invalid IL or missing references)
			//IL_001f: Expected Obj, but got Unknown
			ThemeResourceKey val = _DividerStrokeDefaultBrushKey;
			if (val == null)
			{
				ThemeResourceKey val2 = new ThemeResourceKey(Category, "DividerStrokeDefault", (ThemeResourceKeyType)3);
				_DividerStrokeDefaultBrushKey = val2;
				val = val2;
			}
			return val;
		}
	}

	public static ThemeResourceKey FocusStrokeInnerColorKey
	{
		get
		{
			//IL_0014: Unknown result type (might be due to invalid IL or missing references)
			//IL_0019: Unknown result type (might be due to invalid IL or missing references)
			//IL_001f: Expected Obj, but got Unknown
			ThemeResourceKey val = _FocusStrokeInnerColorKey;
			if (val == null)
			{
				ThemeResourceKey val2 = new ThemeResourceKey(Category, "FocusStrokeInner", (ThemeResourceKeyType)1);
				_FocusStrokeInnerColorKey = val2;
				val = val2;
			}
			return val;
		}
	}

	public static ThemeResourceKey FocusStrokeInnerBrushKey
	{
		get
		{
			//IL_0014: Unknown result type (might be due to invalid IL or missing references)
			//IL_0019: Unknown result type (might be due to invalid IL or missing references)
			//IL_001f: Expected Obj, but got Unknown
			ThemeResourceKey val = _FocusStrokeInnerBrushKey;
			if (val == null)
			{
				ThemeResourceKey val2 = new ThemeResourceKey(Category, "FocusStrokeInner", (ThemeResourceKeyType)3);
				_FocusStrokeInnerBrushKey = val2;
				val = val2;
			}
			return val;
		}
	}

	public static ThemeResourceKey FocusStrokeOuterColorKey
	{
		get
		{
			//IL_0014: Unknown result type (might be due to invalid IL or missing references)
			//IL_0019: Unknown result type (might be due to invalid IL or missing references)
			//IL_001f: Expected Obj, but got Unknown
			ThemeResourceKey val = _FocusStrokeOuterColorKey;
			if (val == null)
			{
				ThemeResourceKey val2 = new ThemeResourceKey(Category, "FocusStrokeOuter", (ThemeResourceKeyType)1);
				_FocusStrokeOuterColorKey = val2;
				val = val2;
			}
			return val;
		}
	}

	public static ThemeResourceKey FocusStrokeOuterBrushKey
	{
		get
		{
			//IL_0014: Unknown result type (might be due to invalid IL or missing references)
			//IL_0019: Unknown result type (might be due to invalid IL or missing references)
			//IL_001f: Expected Obj, but got Unknown
			ThemeResourceKey val = _FocusStrokeOuterBrushKey;
			if (val == null)
			{
				ThemeResourceKey val2 = new ThemeResourceKey(Category, "FocusStrokeOuter", (ThemeResourceKeyType)3);
				_FocusStrokeOuterBrushKey = val2;
				val = val2;
			}
			return val;
		}
	}

	public static ThemeResourceKey HyperlinkFillDisabledColorKey
	{
		get
		{
			//IL_0014: Unknown result type (might be due to invalid IL or missing references)
			//IL_0019: Unknown result type (might be due to invalid IL or missing references)
			//IL_001f: Expected Obj, but got Unknown
			ThemeResourceKey val = _HyperlinkFillDisabledColorKey;
			if (val == null)
			{
				ThemeResourceKey val2 = new ThemeResourceKey(Category, "HyperlinkFillDisabled", (ThemeResourceKeyType)1);
				_HyperlinkFillDisabledColorKey = val2;
				val = val2;
			}
			return val;
		}
	}

	public static ThemeResourceKey HyperlinkFillDisabledBrushKey
	{
		get
		{
			//IL_0014: Unknown result type (might be due to invalid IL or missing references)
			//IL_0019: Unknown result type (might be due to invalid IL or missing references)
			//IL_001f: Expected Obj, but got Unknown
			ThemeResourceKey val = _HyperlinkFillDisabledBrushKey;
			if (val == null)
			{
				ThemeResourceKey val2 = new ThemeResourceKey(Category, "HyperlinkFillDisabled", (ThemeResourceKeyType)3);
				_HyperlinkFillDisabledBrushKey = val2;
				val = val2;
			}
			return val;
		}
	}

	public static ThemeResourceKey HyperlinkFillPrimaryColorKey
	{
		get
		{
			//IL_0014: Unknown result type (might be due to invalid IL or missing references)
			//IL_0019: Unknown result type (might be due to invalid IL or missing references)
			//IL_001f: Expected Obj, but got Unknown
			ThemeResourceKey val = _HyperlinkFillPrimaryColorKey;
			if (val == null)
			{
				ThemeResourceKey val2 = new ThemeResourceKey(Category, "HyperlinkFillPrimary", (ThemeResourceKeyType)1);
				_HyperlinkFillPrimaryColorKey = val2;
				val = val2;
			}
			return val;
		}
	}

	public static ThemeResourceKey HyperlinkFillPrimaryBrushKey
	{
		get
		{
			//IL_0014: Unknown result type (might be due to invalid IL or missing references)
			//IL_0019: Unknown result type (might be due to invalid IL or missing references)
			//IL_001f: Expected Obj, but got Unknown
			ThemeResourceKey val = _HyperlinkFillPrimaryBrushKey;
			if (val == null)
			{
				ThemeResourceKey val2 = new ThemeResourceKey(Category, "HyperlinkFillPrimary", (ThemeResourceKeyType)3);
				_HyperlinkFillPrimaryBrushKey = val2;
				val = val2;
			}
			return val;
		}
	}

	public static ThemeResourceKey HyperlinkFillSecondaryColorKey
	{
		get
		{
			//IL_0014: Unknown result type (might be due to invalid IL or missing references)
			//IL_0019: Unknown result type (might be due to invalid IL or missing references)
			//IL_001f: Expected Obj, but got Unknown
			ThemeResourceKey val = _HyperlinkFillSecondaryColorKey;
			if (val == null)
			{
				ThemeResourceKey val2 = new ThemeResourceKey(Category, "HyperlinkFillSecondary", (ThemeResourceKeyType)1);
				_HyperlinkFillSecondaryColorKey = val2;
				val = val2;
			}
			return val;
		}
	}

	public static ThemeResourceKey HyperlinkFillSecondaryBrushKey
	{
		get
		{
			//IL_0014: Unknown result type (might be due to invalid IL or missing references)
			//IL_0019: Unknown result type (might be due to invalid IL or missing references)
			//IL_001f: Expected Obj, but got Unknown
			ThemeResourceKey val = _HyperlinkFillSecondaryBrushKey;
			if (val == null)
			{
				ThemeResourceKey val2 = new ThemeResourceKey(Category, "HyperlinkFillSecondary", (ThemeResourceKeyType)3);
				_HyperlinkFillSecondaryBrushKey = val2;
				val = val2;
			}
			return val;
		}
	}

	public static ThemeResourceKey HyperlinkFillTertiaryColorKey
	{
		get
		{
			//IL_0014: Unknown result type (might be due to invalid IL or missing references)
			//IL_0019: Unknown result type (might be due to invalid IL or missing references)
			//IL_001f: Expected Obj, but got Unknown
			ThemeResourceKey val = _HyperlinkFillTertiaryColorKey;
			if (val == null)
			{
				ThemeResourceKey val2 = new ThemeResourceKey(Category, "HyperlinkFillTertiary", (ThemeResourceKeyType)1);
				_HyperlinkFillTertiaryColorKey = val2;
				val = val2;
			}
			return val;
		}
	}

	public static ThemeResourceKey HyperlinkFillTertiaryBrushKey
	{
		get
		{
			//IL_0014: Unknown result type (might be due to invalid IL or missing references)
			//IL_0019: Unknown result type (might be due to invalid IL or missing references)
			//IL_001f: Expected Obj, but got Unknown
			ThemeResourceKey val = _HyperlinkFillTertiaryBrushKey;
			if (val == null)
			{
				ThemeResourceKey val2 = new ThemeResourceKey(Category, "HyperlinkFillTertiary", (ThemeResourceKeyType)3);
				_HyperlinkFillTertiaryBrushKey = val2;
				val = val2;
			}
			return val;
		}
	}

	public static ThemeResourceKey LayerFillAltColorKey
	{
		get
		{
			//IL_0014: Unknown result type (might be due to invalid IL or missing references)
			//IL_0019: Unknown result type (might be due to invalid IL or missing references)
			//IL_001f: Expected Obj, but got Unknown
			ThemeResourceKey val = _LayerFillAltColorKey;
			if (val == null)
			{
				ThemeResourceKey val2 = new ThemeResourceKey(Category, "LayerFillAlt", (ThemeResourceKeyType)1);
				_LayerFillAltColorKey = val2;
				val = val2;
			}
			return val;
		}
	}

	public static ThemeResourceKey LayerFillAltBrushKey
	{
		get
		{
			//IL_0014: Unknown result type (might be due to invalid IL or missing references)
			//IL_0019: Unknown result type (might be due to invalid IL or missing references)
			//IL_001f: Expected Obj, but got Unknown
			ThemeResourceKey val = _LayerFillAltBrushKey;
			if (val == null)
			{
				ThemeResourceKey val2 = new ThemeResourceKey(Category, "LayerFillAlt", (ThemeResourceKeyType)3);
				_LayerFillAltBrushKey = val2;
				val = val2;
			}
			return val;
		}
	}

	public static ThemeResourceKey LayerFillDefaultColorKey
	{
		get
		{
			//IL_0014: Unknown result type (might be due to invalid IL or missing references)
			//IL_0019: Unknown result type (might be due to invalid IL or missing references)
			//IL_001f: Expected Obj, but got Unknown
			ThemeResourceKey val = _LayerFillDefaultColorKey;
			if (val == null)
			{
				ThemeResourceKey val2 = new ThemeResourceKey(Category, "LayerFillDefault", (ThemeResourceKeyType)1);
				_LayerFillDefaultColorKey = val2;
				val = val2;
			}
			return val;
		}
	}

	public static ThemeResourceKey LayerFillDefaultBrushKey
	{
		get
		{
			//IL_0014: Unknown result type (might be due to invalid IL or missing references)
			//IL_0019: Unknown result type (might be due to invalid IL or missing references)
			//IL_001f: Expected Obj, but got Unknown
			ThemeResourceKey val = _LayerFillDefaultBrushKey;
			if (val == null)
			{
				ThemeResourceKey val2 = new ThemeResourceKey(Category, "LayerFillDefault", (ThemeResourceKeyType)3);
				_LayerFillDefaultBrushKey = val2;
				val = val2;
			}
			return val;
		}
	}

	public static ThemeResourceKey ShadowFlyoutColorKey
	{
		get
		{
			//IL_0014: Unknown result type (might be due to invalid IL or missing references)
			//IL_0019: Unknown result type (might be due to invalid IL or missing references)
			//IL_001f: Expected Obj, but got Unknown
			ThemeResourceKey val = _ShadowFlyoutColorKey;
			if (val == null)
			{
				ThemeResourceKey val2 = new ThemeResourceKey(Category, "ShadowFlyout", (ThemeResourceKeyType)1);
				_ShadowFlyoutColorKey = val2;
				val = val2;
			}
			return val;
		}
	}

	public static ThemeResourceKey ShadowFlyoutBrushKey
	{
		get
		{
			//IL_0014: Unknown result type (might be due to invalid IL or missing references)
			//IL_0019: Unknown result type (might be due to invalid IL or missing references)
			//IL_001f: Expected Obj, but got Unknown
			ThemeResourceKey val = _ShadowFlyoutBrushKey;
			if (val == null)
			{
				ThemeResourceKey val2 = new ThemeResourceKey(Category, "ShadowFlyout", (ThemeResourceKeyType)3);
				_ShadowFlyoutBrushKey = val2;
				val = val2;
			}
			return val;
		}
	}

	public static ThemeResourceKey SmokeFillDefaultColorKey
	{
		get
		{
			//IL_0014: Unknown result type (might be due to invalid IL or missing references)
			//IL_0019: Unknown result type (might be due to invalid IL or missing references)
			//IL_001f: Expected Obj, but got Unknown
			ThemeResourceKey val = _SmokeFillDefaultColorKey;
			if (val == null)
			{
				ThemeResourceKey val2 = new ThemeResourceKey(Category, "SmokeFillDefault", (ThemeResourceKeyType)1);
				_SmokeFillDefaultColorKey = val2;
				val = val2;
			}
			return val;
		}
	}

	public static ThemeResourceKey SmokeFillDefaultBrushKey
	{
		get
		{
			//IL_0014: Unknown result type (might be due to invalid IL or missing references)
			//IL_0019: Unknown result type (might be due to invalid IL or missing references)
			//IL_001f: Expected Obj, but got Unknown
			ThemeResourceKey val = _SmokeFillDefaultBrushKey;
			if (val == null)
			{
				ThemeResourceKey val2 = new ThemeResourceKey(Category, "SmokeFillDefault", (ThemeResourceKeyType)3);
				_SmokeFillDefaultBrushKey = val2;
				val = val2;
			}
			return val;
		}
	}

	public static ThemeResourceKey SmokeFillInverseColorKey
	{
		get
		{
			//IL_0014: Unknown result type (might be due to invalid IL or missing references)
			//IL_0019: Unknown result type (might be due to invalid IL or missing references)
			//IL_001f: Expected Obj, but got Unknown
			ThemeResourceKey val = _SmokeFillInverseColorKey;
			if (val == null)
			{
				ThemeResourceKey val2 = new ThemeResourceKey(Category, "SmokeFillInverse", (ThemeResourceKeyType)1);
				_SmokeFillInverseColorKey = val2;
				val = val2;
			}
			return val;
		}
	}

	public static ThemeResourceKey SmokeFillInverseBrushKey
	{
		get
		{
			//IL_0014: Unknown result type (might be due to invalid IL or missing references)
			//IL_0019: Unknown result type (might be due to invalid IL or missing references)
			//IL_001f: Expected Obj, but got Unknown
			ThemeResourceKey val = _SmokeFillInverseBrushKey;
			if (val == null)
			{
				ThemeResourceKey val2 = new ThemeResourceKey(Category, "SmokeFillInverse", (ThemeResourceKeyType)3);
				_SmokeFillInverseBrushKey = val2;
				val = val2;
			}
			return val;
		}
	}

	public static ThemeResourceKey SolidBackgroundFillBaseColorKey
	{
		get
		{
			//IL_0014: Unknown result type (might be due to invalid IL or missing references)
			//IL_0019: Unknown result type (might be due to invalid IL or missing references)
			//IL_001f: Expected Obj, but got Unknown
			ThemeResourceKey val = _SolidBackgroundFillBaseColorKey;
			if (val == null)
			{
				ThemeResourceKey val2 = new ThemeResourceKey(Category, "SolidBackgroundFillBase", (ThemeResourceKeyType)1);
				_SolidBackgroundFillBaseColorKey = val2;
				val = val2;
			}
			return val;
		}
	}

	public static ThemeResourceKey SolidBackgroundFillBaseBrushKey
	{
		get
		{
			//IL_0014: Unknown result type (might be due to invalid IL or missing references)
			//IL_0019: Unknown result type (might be due to invalid IL or missing references)
			//IL_001f: Expected Obj, but got Unknown
			ThemeResourceKey val = _SolidBackgroundFillBaseBrushKey;
			if (val == null)
			{
				ThemeResourceKey val2 = new ThemeResourceKey(Category, "SolidBackgroundFillBase", (ThemeResourceKeyType)3);
				_SolidBackgroundFillBaseBrushKey = val2;
				val = val2;
			}
			return val;
		}
	}

	public static ThemeResourceKey SolidBackgroundFillBaseAltColorKey
	{
		get
		{
			//IL_0014: Unknown result type (might be due to invalid IL or missing references)
			//IL_0019: Unknown result type (might be due to invalid IL or missing references)
			//IL_001f: Expected Obj, but got Unknown
			ThemeResourceKey val = _SolidBackgroundFillBaseAltColorKey;
			if (val == null)
			{
				ThemeResourceKey val2 = new ThemeResourceKey(Category, "SolidBackgroundFillBaseAlt", (ThemeResourceKeyType)1);
				_SolidBackgroundFillBaseAltColorKey = val2;
				val = val2;
			}
			return val;
		}
	}

	public static ThemeResourceKey SolidBackgroundFillBaseAltBrushKey
	{
		get
		{
			//IL_0014: Unknown result type (might be due to invalid IL or missing references)
			//IL_0019: Unknown result type (might be due to invalid IL or missing references)
			//IL_001f: Expected Obj, but got Unknown
			ThemeResourceKey val = _SolidBackgroundFillBaseAltBrushKey;
			if (val == null)
			{
				ThemeResourceKey val2 = new ThemeResourceKey(Category, "SolidBackgroundFillBaseAlt", (ThemeResourceKeyType)3);
				_SolidBackgroundFillBaseAltBrushKey = val2;
				val = val2;
			}
			return val;
		}
	}

	public static ThemeResourceKey SolidBackgroundFillQuaternaryColorKey
	{
		get
		{
			//IL_0014: Unknown result type (might be due to invalid IL or missing references)
			//IL_0019: Unknown result type (might be due to invalid IL or missing references)
			//IL_001f: Expected Obj, but got Unknown
			ThemeResourceKey val = _SolidBackgroundFillQuaternaryColorKey;
			if (val == null)
			{
				ThemeResourceKey val2 = new ThemeResourceKey(Category, "SolidBackgroundFillQuaternary", (ThemeResourceKeyType)1);
				_SolidBackgroundFillQuaternaryColorKey = val2;
				val = val2;
			}
			return val;
		}
	}

	public static ThemeResourceKey SolidBackgroundFillQuaternaryBrushKey
	{
		get
		{
			//IL_0014: Unknown result type (might be due to invalid IL or missing references)
			//IL_0019: Unknown result type (might be due to invalid IL or missing references)
			//IL_001f: Expected Obj, but got Unknown
			ThemeResourceKey val = _SolidBackgroundFillQuaternaryBrushKey;
			if (val == null)
			{
				ThemeResourceKey val2 = new ThemeResourceKey(Category, "SolidBackgroundFillQuaternary", (ThemeResourceKeyType)3);
				_SolidBackgroundFillQuaternaryBrushKey = val2;
				val = val2;
			}
			return val;
		}
	}

	public static ThemeResourceKey SolidBackgroundFillQuinaryColorKey
	{
		get
		{
			//IL_0014: Unknown result type (might be due to invalid IL or missing references)
			//IL_0019: Unknown result type (might be due to invalid IL or missing references)
			//IL_001f: Expected Obj, but got Unknown
			ThemeResourceKey val = _SolidBackgroundFillQuinaryColorKey;
			if (val == null)
			{
				ThemeResourceKey val2 = new ThemeResourceKey(Category, "SolidBackgroundFillQuinary", (ThemeResourceKeyType)1);
				_SolidBackgroundFillQuinaryColorKey = val2;
				val = val2;
			}
			return val;
		}
	}

	public static ThemeResourceKey SolidBackgroundFillQuinaryBrushKey
	{
		get
		{
			//IL_0014: Unknown result type (might be due to invalid IL or missing references)
			//IL_0019: Unknown result type (might be due to invalid IL or missing references)
			//IL_001f: Expected Obj, but got Unknown
			ThemeResourceKey val = _SolidBackgroundFillQuinaryBrushKey;
			if (val == null)
			{
				ThemeResourceKey val2 = new ThemeResourceKey(Category, "SolidBackgroundFillQuinary", (ThemeResourceKeyType)3);
				_SolidBackgroundFillQuinaryBrushKey = val2;
				val = val2;
			}
			return val;
		}
	}

	public static ThemeResourceKey SolidBackgroundFillSecondaryColorKey
	{
		get
		{
			//IL_0014: Unknown result type (might be due to invalid IL or missing references)
			//IL_0019: Unknown result type (might be due to invalid IL or missing references)
			//IL_001f: Expected Obj, but got Unknown
			ThemeResourceKey val = _SolidBackgroundFillSecondaryColorKey;
			if (val == null)
			{
				ThemeResourceKey val2 = new ThemeResourceKey(Category, "SolidBackgroundFillSecondary", (ThemeResourceKeyType)1);
				_SolidBackgroundFillSecondaryColorKey = val2;
				val = val2;
			}
			return val;
		}
	}

	public static ThemeResourceKey SolidBackgroundFillSecondaryBrushKey
	{
		get
		{
			//IL_0014: Unknown result type (might be due to invalid IL or missing references)
			//IL_0019: Unknown result type (might be due to invalid IL or missing references)
			//IL_001f: Expected Obj, but got Unknown
			ThemeResourceKey val = _SolidBackgroundFillSecondaryBrushKey;
			if (val == null)
			{
				ThemeResourceKey val2 = new ThemeResourceKey(Category, "SolidBackgroundFillSecondary", (ThemeResourceKeyType)3);
				_SolidBackgroundFillSecondaryBrushKey = val2;
				val = val2;
			}
			return val;
		}
	}

	public static ThemeResourceKey SolidBackgroundFillSenaryColorKey
	{
		get
		{
			//IL_0014: Unknown result type (might be due to invalid IL or missing references)
			//IL_0019: Unknown result type (might be due to invalid IL or missing references)
			//IL_001f: Expected Obj, but got Unknown
			ThemeResourceKey val = _SolidBackgroundFillSenaryColorKey;
			if (val == null)
			{
				ThemeResourceKey val2 = new ThemeResourceKey(Category, "SolidBackgroundFillSenary", (ThemeResourceKeyType)1);
				_SolidBackgroundFillSenaryColorKey = val2;
				val = val2;
			}
			return val;
		}
	}

	public static ThemeResourceKey SolidBackgroundFillSenaryBrushKey
	{
		get
		{
			//IL_0014: Unknown result type (might be due to invalid IL or missing references)
			//IL_0019: Unknown result type (might be due to invalid IL or missing references)
			//IL_001f: Expected Obj, but got Unknown
			ThemeResourceKey val = _SolidBackgroundFillSenaryBrushKey;
			if (val == null)
			{
				ThemeResourceKey val2 = new ThemeResourceKey(Category, "SolidBackgroundFillSenary", (ThemeResourceKeyType)3);
				_SolidBackgroundFillSenaryBrushKey = val2;
				val = val2;
			}
			return val;
		}
	}

	public static ThemeResourceKey SolidBackgroundFillTertiaryColorKey
	{
		get
		{
			//IL_0014: Unknown result type (might be due to invalid IL or missing references)
			//IL_0019: Unknown result type (might be due to invalid IL or missing references)
			//IL_001f: Expected Obj, but got Unknown
			ThemeResourceKey val = _SolidBackgroundFillTertiaryColorKey;
			if (val == null)
			{
				ThemeResourceKey val2 = new ThemeResourceKey(Category, "SolidBackgroundFillTertiary", (ThemeResourceKeyType)1);
				_SolidBackgroundFillTertiaryColorKey = val2;
				val = val2;
			}
			return val;
		}
	}

	public static ThemeResourceKey SolidBackgroundFillTertiaryBrushKey
	{
		get
		{
			//IL_0014: Unknown result type (might be due to invalid IL or missing references)
			//IL_0019: Unknown result type (might be due to invalid IL or missing references)
			//IL_001f: Expected Obj, but got Unknown
			ThemeResourceKey val = _SolidBackgroundFillTertiaryBrushKey;
			if (val == null)
			{
				ThemeResourceKey val2 = new ThemeResourceKey(Category, "SolidBackgroundFillTertiary", (ThemeResourceKeyType)3);
				_SolidBackgroundFillTertiaryBrushKey = val2;
				val = val2;
			}
			return val;
		}
	}

	public static ThemeResourceKey SubtleFillDisabledColorKey
	{
		get
		{
			//IL_0014: Unknown result type (might be due to invalid IL or missing references)
			//IL_0019: Unknown result type (might be due to invalid IL or missing references)
			//IL_001f: Expected Obj, but got Unknown
			ThemeResourceKey val = _SubtleFillDisabledColorKey;
			if (val == null)
			{
				ThemeResourceKey val2 = new ThemeResourceKey(Category, "SubtleFillDisabled", (ThemeResourceKeyType)1);
				_SubtleFillDisabledColorKey = val2;
				val = val2;
			}
			return val;
		}
	}

	public static ThemeResourceKey SubtleFillDisabledBrushKey
	{
		get
		{
			//IL_0014: Unknown result type (might be due to invalid IL or missing references)
			//IL_0019: Unknown result type (might be due to invalid IL or missing references)
			//IL_001f: Expected Obj, but got Unknown
			ThemeResourceKey val = _SubtleFillDisabledBrushKey;
			if (val == null)
			{
				ThemeResourceKey val2 = new ThemeResourceKey(Category, "SubtleFillDisabled", (ThemeResourceKeyType)3);
				_SubtleFillDisabledBrushKey = val2;
				val = val2;
			}
			return val;
		}
	}

	public static ThemeResourceKey SubtleFillSecondaryColorKey
	{
		get
		{
			//IL_0014: Unknown result type (might be due to invalid IL or missing references)
			//IL_0019: Unknown result type (might be due to invalid IL or missing references)
			//IL_001f: Expected Obj, but got Unknown
			ThemeResourceKey val = _SubtleFillSecondaryColorKey;
			if (val == null)
			{
				ThemeResourceKey val2 = new ThemeResourceKey(Category, "SubtleFillSecondary", (ThemeResourceKeyType)1);
				_SubtleFillSecondaryColorKey = val2;
				val = val2;
			}
			return val;
		}
	}

	public static ThemeResourceKey SubtleFillSecondaryBrushKey
	{
		get
		{
			//IL_0014: Unknown result type (might be due to invalid IL or missing references)
			//IL_0019: Unknown result type (might be due to invalid IL or missing references)
			//IL_001f: Expected Obj, but got Unknown
			ThemeResourceKey val = _SubtleFillSecondaryBrushKey;
			if (val == null)
			{
				ThemeResourceKey val2 = new ThemeResourceKey(Category, "SubtleFillSecondary", (ThemeResourceKeyType)3);
				_SubtleFillSecondaryBrushKey = val2;
				val = val2;
			}
			return val;
		}
	}

	public static ThemeResourceKey SubtleFillTertiaryColorKey
	{
		get
		{
			//IL_0014: Unknown result type (might be due to invalid IL or missing references)
			//IL_0019: Unknown result type (might be due to invalid IL or missing references)
			//IL_001f: Expected Obj, but got Unknown
			ThemeResourceKey val = _SubtleFillTertiaryColorKey;
			if (val == null)
			{
				ThemeResourceKey val2 = new ThemeResourceKey(Category, "SubtleFillTertiary", (ThemeResourceKeyType)1);
				_SubtleFillTertiaryColorKey = val2;
				val = val2;
			}
			return val;
		}
	}

	public static ThemeResourceKey SubtleFillTertiaryBrushKey
	{
		get
		{
			//IL_0014: Unknown result type (might be due to invalid IL or missing references)
			//IL_0019: Unknown result type (might be due to invalid IL or missing references)
			//IL_001f: Expected Obj, but got Unknown
			ThemeResourceKey val = _SubtleFillTertiaryBrushKey;
			if (val == null)
			{
				ThemeResourceKey val2 = new ThemeResourceKey(Category, "SubtleFillTertiary", (ThemeResourceKeyType)3);
				_SubtleFillTertiaryBrushKey = val2;
				val = val2;
			}
			return val;
		}
	}

	public static ThemeResourceKey SubtleFillTransparentColorKey
	{
		get
		{
			//IL_0014: Unknown result type (might be due to invalid IL or missing references)
			//IL_0019: Unknown result type (might be due to invalid IL or missing references)
			//IL_001f: Expected Obj, but got Unknown
			ThemeResourceKey val = _SubtleFillTransparentColorKey;
			if (val == null)
			{
				ThemeResourceKey val2 = new ThemeResourceKey(Category, "SubtleFillTransparent", (ThemeResourceKeyType)1);
				_SubtleFillTransparentColorKey = val2;
				val = val2;
			}
			return val;
		}
	}

	public static ThemeResourceKey SubtleFillTransparentBrushKey
	{
		get
		{
			//IL_0014: Unknown result type (might be due to invalid IL or missing references)
			//IL_0019: Unknown result type (might be due to invalid IL or missing references)
			//IL_001f: Expected Obj, but got Unknown
			ThemeResourceKey val = _SubtleFillTransparentBrushKey;
			if (val == null)
			{
				ThemeResourceKey val2 = new ThemeResourceKey(Category, "SubtleFillTransparent", (ThemeResourceKeyType)3);
				_SubtleFillTransparentBrushKey = val2;
				val = val2;
			}
			return val;
		}
	}

	public static ThemeResourceKey SurfaceBackgroundFillDefaultColorKey
	{
		get
		{
			//IL_0014: Unknown result type (might be due to invalid IL or missing references)
			//IL_0019: Unknown result type (might be due to invalid IL or missing references)
			//IL_001f: Expected Obj, but got Unknown
			ThemeResourceKey val = _SurfaceBackgroundFillDefaultColorKey;
			if (val == null)
			{
				ThemeResourceKey val2 = new ThemeResourceKey(Category, "SurfaceBackgroundFillDefault", (ThemeResourceKeyType)1);
				_SurfaceBackgroundFillDefaultColorKey = val2;
				val = val2;
			}
			return val;
		}
	}

	public static ThemeResourceKey SurfaceBackgroundFillDefaultBrushKey
	{
		get
		{
			//IL_0014: Unknown result type (might be due to invalid IL or missing references)
			//IL_0019: Unknown result type (might be due to invalid IL or missing references)
			//IL_001f: Expected Obj, but got Unknown
			ThemeResourceKey val = _SurfaceBackgroundFillDefaultBrushKey;
			if (val == null)
			{
				ThemeResourceKey val2 = new ThemeResourceKey(Category, "SurfaceBackgroundFillDefault", (ThemeResourceKeyType)3);
				_SurfaceBackgroundFillDefaultBrushKey = val2;
				val = val2;
			}
			return val;
		}
	}

	public static ThemeResourceKey SurfaceStrokeDefaultColorKey
	{
		get
		{
			//IL_0014: Unknown result type (might be due to invalid IL or missing references)
			//IL_0019: Unknown result type (might be due to invalid IL or missing references)
			//IL_001f: Expected Obj, but got Unknown
			ThemeResourceKey val = _SurfaceStrokeDefaultColorKey;
			if (val == null)
			{
				ThemeResourceKey val2 = new ThemeResourceKey(Category, "SurfaceStrokeDefault", (ThemeResourceKeyType)1);
				_SurfaceStrokeDefaultColorKey = val2;
				val = val2;
			}
			return val;
		}
	}

	public static ThemeResourceKey SurfaceStrokeDefaultBrushKey
	{
		get
		{
			//IL_0014: Unknown result type (might be due to invalid IL or missing references)
			//IL_0019: Unknown result type (might be due to invalid IL or missing references)
			//IL_001f: Expected Obj, but got Unknown
			ThemeResourceKey val = _SurfaceStrokeDefaultBrushKey;
			if (val == null)
			{
				ThemeResourceKey val2 = new ThemeResourceKey(Category, "SurfaceStrokeDefault", (ThemeResourceKeyType)3);
				_SurfaceStrokeDefaultBrushKey = val2;
				val = val2;
			}
			return val;
		}
	}

	public static ThemeResourceKey SurfaceStrokeFlyoutColorKey
	{
		get
		{
			//IL_0014: Unknown result type (might be due to invalid IL or missing references)
			//IL_0019: Unknown result type (might be due to invalid IL or missing references)
			//IL_001f: Expected Obj, but got Unknown
			ThemeResourceKey val = _SurfaceStrokeFlyoutColorKey;
			if (val == null)
			{
				ThemeResourceKey val2 = new ThemeResourceKey(Category, "SurfaceStrokeFlyout", (ThemeResourceKeyType)1);
				_SurfaceStrokeFlyoutColorKey = val2;
				val = val2;
			}
			return val;
		}
	}

	public static ThemeResourceKey SurfaceStrokeFlyoutBrushKey
	{
		get
		{
			//IL_0014: Unknown result type (might be due to invalid IL or missing references)
			//IL_0019: Unknown result type (might be due to invalid IL or missing references)
			//IL_001f: Expected Obj, but got Unknown
			ThemeResourceKey val = _SurfaceStrokeFlyoutBrushKey;
			if (val == null)
			{
				ThemeResourceKey val2 = new ThemeResourceKey(Category, "SurfaceStrokeFlyout", (ThemeResourceKeyType)3);
				_SurfaceStrokeFlyoutBrushKey = val2;
				val = val2;
			}
			return val;
		}
	}

	public static ThemeResourceKey SystemFillAttentionColorKey
	{
		get
		{
			//IL_0014: Unknown result type (might be due to invalid IL or missing references)
			//IL_0019: Unknown result type (might be due to invalid IL or missing references)
			//IL_001f: Expected Obj, but got Unknown
			ThemeResourceKey val = _SystemFillAttentionColorKey;
			if (val == null)
			{
				ThemeResourceKey val2 = new ThemeResourceKey(Category, "SystemFillAttention", (ThemeResourceKeyType)1);
				_SystemFillAttentionColorKey = val2;
				val = val2;
			}
			return val;
		}
	}

	public static ThemeResourceKey SystemFillAttentionBrushKey
	{
		get
		{
			//IL_0014: Unknown result type (might be due to invalid IL or missing references)
			//IL_0019: Unknown result type (might be due to invalid IL or missing references)
			//IL_001f: Expected Obj, but got Unknown
			ThemeResourceKey val = _SystemFillAttentionBrushKey;
			if (val == null)
			{
				ThemeResourceKey val2 = new ThemeResourceKey(Category, "SystemFillAttention", (ThemeResourceKeyType)3);
				_SystemFillAttentionBrushKey = val2;
				val = val2;
			}
			return val;
		}
	}

	public static ThemeResourceKey SystemFillAttentionBackgroundColorKey
	{
		get
		{
			//IL_0014: Unknown result type (might be due to invalid IL or missing references)
			//IL_0019: Unknown result type (might be due to invalid IL or missing references)
			//IL_001f: Expected Obj, but got Unknown
			ThemeResourceKey val = _SystemFillAttentionBackgroundColorKey;
			if (val == null)
			{
				ThemeResourceKey val2 = new ThemeResourceKey(Category, "SystemFillAttentionBackground", (ThemeResourceKeyType)1);
				_SystemFillAttentionBackgroundColorKey = val2;
				val = val2;
			}
			return val;
		}
	}

	public static ThemeResourceKey SystemFillAttentionBackgroundBrushKey
	{
		get
		{
			//IL_0014: Unknown result type (might be due to invalid IL or missing references)
			//IL_0019: Unknown result type (might be due to invalid IL or missing references)
			//IL_001f: Expected Obj, but got Unknown
			ThemeResourceKey val = _SystemFillAttentionBackgroundBrushKey;
			if (val == null)
			{
				ThemeResourceKey val2 = new ThemeResourceKey(Category, "SystemFillAttentionBackground", (ThemeResourceKeyType)3);
				_SystemFillAttentionBackgroundBrushKey = val2;
				val = val2;
			}
			return val;
		}
	}

	public static ThemeResourceKey SystemFillCautionColorKey
	{
		get
		{
			//IL_0014: Unknown result type (might be due to invalid IL or missing references)
			//IL_0019: Unknown result type (might be due to invalid IL or missing references)
			//IL_001f: Expected Obj, but got Unknown
			ThemeResourceKey val = _SystemFillCautionColorKey;
			if (val == null)
			{
				ThemeResourceKey val2 = new ThemeResourceKey(Category, "SystemFillCaution", (ThemeResourceKeyType)1);
				_SystemFillCautionColorKey = val2;
				val = val2;
			}
			return val;
		}
	}

	public static ThemeResourceKey SystemFillCautionBrushKey
	{
		get
		{
			//IL_0014: Unknown result type (might be due to invalid IL or missing references)
			//IL_0019: Unknown result type (might be due to invalid IL or missing references)
			//IL_001f: Expected Obj, but got Unknown
			ThemeResourceKey val = _SystemFillCautionBrushKey;
			if (val == null)
			{
				ThemeResourceKey val2 = new ThemeResourceKey(Category, "SystemFillCaution", (ThemeResourceKeyType)3);
				_SystemFillCautionBrushKey = val2;
				val = val2;
			}
			return val;
		}
	}

	public static ThemeResourceKey SystemFillCautionBackgroundColorKey
	{
		get
		{
			//IL_0014: Unknown result type (might be due to invalid IL or missing references)
			//IL_0019: Unknown result type (might be due to invalid IL or missing references)
			//IL_001f: Expected Obj, but got Unknown
			ThemeResourceKey val = _SystemFillCautionBackgroundColorKey;
			if (val == null)
			{
				ThemeResourceKey val2 = new ThemeResourceKey(Category, "SystemFillCautionBackground", (ThemeResourceKeyType)1);
				_SystemFillCautionBackgroundColorKey = val2;
				val = val2;
			}
			return val;
		}
	}

	public static ThemeResourceKey SystemFillCautionBackgroundBrushKey
	{
		get
		{
			//IL_0014: Unknown result type (might be due to invalid IL or missing references)
			//IL_0019: Unknown result type (might be due to invalid IL or missing references)
			//IL_001f: Expected Obj, but got Unknown
			ThemeResourceKey val = _SystemFillCautionBackgroundBrushKey;
			if (val == null)
			{
				ThemeResourceKey val2 = new ThemeResourceKey(Category, "SystemFillCautionBackground", (ThemeResourceKeyType)3);
				_SystemFillCautionBackgroundBrushKey = val2;
				val = val2;
			}
			return val;
		}
	}

	public static ThemeResourceKey SystemFillCriticalColorKey
	{
		get
		{
			//IL_0014: Unknown result type (might be due to invalid IL or missing references)
			//IL_0019: Unknown result type (might be due to invalid IL or missing references)
			//IL_001f: Expected Obj, but got Unknown
			ThemeResourceKey val = _SystemFillCriticalColorKey;
			if (val == null)
			{
				ThemeResourceKey val2 = new ThemeResourceKey(Category, "SystemFillCritical", (ThemeResourceKeyType)1);
				_SystemFillCriticalColorKey = val2;
				val = val2;
			}
			return val;
		}
	}

	public static ThemeResourceKey SystemFillCriticalBrushKey
	{
		get
		{
			//IL_0014: Unknown result type (might be due to invalid IL or missing references)
			//IL_0019: Unknown result type (might be due to invalid IL or missing references)
			//IL_001f: Expected Obj, but got Unknown
			ThemeResourceKey val = _SystemFillCriticalBrushKey;
			if (val == null)
			{
				ThemeResourceKey val2 = new ThemeResourceKey(Category, "SystemFillCritical", (ThemeResourceKeyType)3);
				_SystemFillCriticalBrushKey = val2;
				val = val2;
			}
			return val;
		}
	}

	public static ThemeResourceKey SystemFillCriticalBackgroundColorKey
	{
		get
		{
			//IL_0014: Unknown result type (might be due to invalid IL or missing references)
			//IL_0019: Unknown result type (might be due to invalid IL or missing references)
			//IL_001f: Expected Obj, but got Unknown
			ThemeResourceKey val = _SystemFillCriticalBackgroundColorKey;
			if (val == null)
			{
				ThemeResourceKey val2 = new ThemeResourceKey(Category, "SystemFillCriticalBackground", (ThemeResourceKeyType)1);
				_SystemFillCriticalBackgroundColorKey = val2;
				val = val2;
			}
			return val;
		}
	}

	public static ThemeResourceKey SystemFillCriticalBackgroundBrushKey
	{
		get
		{
			//IL_0014: Unknown result type (might be due to invalid IL or missing references)
			//IL_0019: Unknown result type (might be due to invalid IL or missing references)
			//IL_001f: Expected Obj, but got Unknown
			ThemeResourceKey val = _SystemFillCriticalBackgroundBrushKey;
			if (val == null)
			{
				ThemeResourceKey val2 = new ThemeResourceKey(Category, "SystemFillCriticalBackground", (ThemeResourceKeyType)3);
				_SystemFillCriticalBackgroundBrushKey = val2;
				val = val2;
			}
			return val;
		}
	}

	public static ThemeResourceKey SystemFillNeutralColorKey
	{
		get
		{
			//IL_0014: Unknown result type (might be due to invalid IL or missing references)
			//IL_0019: Unknown result type (might be due to invalid IL or missing references)
			//IL_001f: Expected Obj, but got Unknown
			ThemeResourceKey val = _SystemFillNeutralColorKey;
			if (val == null)
			{
				ThemeResourceKey val2 = new ThemeResourceKey(Category, "SystemFillNeutral", (ThemeResourceKeyType)1);
				_SystemFillNeutralColorKey = val2;
				val = val2;
			}
			return val;
		}
	}

	public static ThemeResourceKey SystemFillNeutralBrushKey
	{
		get
		{
			//IL_0014: Unknown result type (might be due to invalid IL or missing references)
			//IL_0019: Unknown result type (might be due to invalid IL or missing references)
			//IL_001f: Expected Obj, but got Unknown
			ThemeResourceKey val = _SystemFillNeutralBrushKey;
			if (val == null)
			{
				ThemeResourceKey val2 = new ThemeResourceKey(Category, "SystemFillNeutral", (ThemeResourceKeyType)3);
				_SystemFillNeutralBrushKey = val2;
				val = val2;
			}
			return val;
		}
	}

	public static ThemeResourceKey SystemFillNeutralBackgroundColorKey
	{
		get
		{
			//IL_0014: Unknown result type (might be due to invalid IL or missing references)
			//IL_0019: Unknown result type (might be due to invalid IL or missing references)
			//IL_001f: Expected Obj, but got Unknown
			ThemeResourceKey val = _SystemFillNeutralBackgroundColorKey;
			if (val == null)
			{
				ThemeResourceKey val2 = new ThemeResourceKey(Category, "SystemFillNeutralBackground", (ThemeResourceKeyType)1);
				_SystemFillNeutralBackgroundColorKey = val2;
				val = val2;
			}
			return val;
		}
	}

	public static ThemeResourceKey SystemFillNeutralBackgroundBrushKey
	{
		get
		{
			//IL_0014: Unknown result type (might be due to invalid IL or missing references)
			//IL_0019: Unknown result type (might be due to invalid IL or missing references)
			//IL_001f: Expected Obj, but got Unknown
			ThemeResourceKey val = _SystemFillNeutralBackgroundBrushKey;
			if (val == null)
			{
				ThemeResourceKey val2 = new ThemeResourceKey(Category, "SystemFillNeutralBackground", (ThemeResourceKeyType)3);
				_SystemFillNeutralBackgroundBrushKey = val2;
				val = val2;
			}
			return val;
		}
	}

	public static ThemeResourceKey SystemFillSolidAttentionBackgroundColorKey
	{
		get
		{
			//IL_0014: Unknown result type (might be due to invalid IL or missing references)
			//IL_0019: Unknown result type (might be due to invalid IL or missing references)
			//IL_001f: Expected Obj, but got Unknown
			ThemeResourceKey val = _SystemFillSolidAttentionBackgroundColorKey;
			if (val == null)
			{
				ThemeResourceKey val2 = new ThemeResourceKey(Category, "SystemFillSolidAttentionBackground", (ThemeResourceKeyType)1);
				_SystemFillSolidAttentionBackgroundColorKey = val2;
				val = val2;
			}
			return val;
		}
	}

	public static ThemeResourceKey SystemFillSolidAttentionBackgroundBrushKey
	{
		get
		{
			//IL_0014: Unknown result type (might be due to invalid IL or missing references)
			//IL_0019: Unknown result type (might be due to invalid IL or missing references)
			//IL_001f: Expected Obj, but got Unknown
			ThemeResourceKey val = _SystemFillSolidAttentionBackgroundBrushKey;
			if (val == null)
			{
				ThemeResourceKey val2 = new ThemeResourceKey(Category, "SystemFillSolidAttentionBackground", (ThemeResourceKeyType)3);
				_SystemFillSolidAttentionBackgroundBrushKey = val2;
				val = val2;
			}
			return val;
		}
	}

	public static ThemeResourceKey SystemFillSolidNeutralColorKey
	{
		get
		{
			//IL_0014: Unknown result type (might be due to invalid IL or missing references)
			//IL_0019: Unknown result type (might be due to invalid IL or missing references)
			//IL_001f: Expected Obj, but got Unknown
			ThemeResourceKey val = _SystemFillSolidNeutralColorKey;
			if (val == null)
			{
				ThemeResourceKey val2 = new ThemeResourceKey(Category, "SystemFillSolidNeutral", (ThemeResourceKeyType)1);
				_SystemFillSolidNeutralColorKey = val2;
				val = val2;
			}
			return val;
		}
	}

	public static ThemeResourceKey SystemFillSolidNeutralBrushKey
	{
		get
		{
			//IL_0014: Unknown result type (might be due to invalid IL or missing references)
			//IL_0019: Unknown result type (might be due to invalid IL or missing references)
			//IL_001f: Expected Obj, but got Unknown
			ThemeResourceKey val = _SystemFillSolidNeutralBrushKey;
			if (val == null)
			{
				ThemeResourceKey val2 = new ThemeResourceKey(Category, "SystemFillSolidNeutral", (ThemeResourceKeyType)3);
				_SystemFillSolidNeutralBrushKey = val2;
				val = val2;
			}
			return val;
		}
	}

	public static ThemeResourceKey SystemFillSolidNeutralBackgroundColorKey
	{
		get
		{
			//IL_0014: Unknown result type (might be due to invalid IL or missing references)
			//IL_0019: Unknown result type (might be due to invalid IL or missing references)
			//IL_001f: Expected Obj, but got Unknown
			ThemeResourceKey val = _SystemFillSolidNeutralBackgroundColorKey;
			if (val == null)
			{
				ThemeResourceKey val2 = new ThemeResourceKey(Category, "SystemFillSolidNeutralBackground", (ThemeResourceKeyType)1);
				_SystemFillSolidNeutralBackgroundColorKey = val2;
				val = val2;
			}
			return val;
		}
	}

	public static ThemeResourceKey SystemFillSolidNeutralBackgroundBrushKey
	{
		get
		{
			//IL_0014: Unknown result type (might be due to invalid IL or missing references)
			//IL_0019: Unknown result type (might be due to invalid IL or missing references)
			//IL_001f: Expected Obj, but got Unknown
			ThemeResourceKey val = _SystemFillSolidNeutralBackgroundBrushKey;
			if (val == null)
			{
				ThemeResourceKey val2 = new ThemeResourceKey(Category, "SystemFillSolidNeutralBackground", (ThemeResourceKeyType)3);
				_SystemFillSolidNeutralBackgroundBrushKey = val2;
				val = val2;
			}
			return val;
		}
	}

	public static ThemeResourceKey SystemFillSuccessColorKey
	{
		get
		{
			//IL_0014: Unknown result type (might be due to invalid IL or missing references)
			//IL_0019: Unknown result type (might be due to invalid IL or missing references)
			//IL_001f: Expected Obj, but got Unknown
			ThemeResourceKey val = _SystemFillSuccessColorKey;
			if (val == null)
			{
				ThemeResourceKey val2 = new ThemeResourceKey(Category, "SystemFillSuccess", (ThemeResourceKeyType)1);
				_SystemFillSuccessColorKey = val2;
				val = val2;
			}
			return val;
		}
	}

	public static ThemeResourceKey SystemFillSuccessBrushKey
	{
		get
		{
			//IL_0014: Unknown result type (might be due to invalid IL or missing references)
			//IL_0019: Unknown result type (might be due to invalid IL or missing references)
			//IL_001f: Expected Obj, but got Unknown
			ThemeResourceKey val = _SystemFillSuccessBrushKey;
			if (val == null)
			{
				ThemeResourceKey val2 = new ThemeResourceKey(Category, "SystemFillSuccess", (ThemeResourceKeyType)3);
				_SystemFillSuccessBrushKey = val2;
				val = val2;
			}
			return val;
		}
	}

	public static ThemeResourceKey SystemFillSuccessBackgroundColorKey
	{
		get
		{
			//IL_0014: Unknown result type (might be due to invalid IL or missing references)
			//IL_0019: Unknown result type (might be due to invalid IL or missing references)
			//IL_001f: Expected Obj, but got Unknown
			ThemeResourceKey val = _SystemFillSuccessBackgroundColorKey;
			if (val == null)
			{
				ThemeResourceKey val2 = new ThemeResourceKey(Category, "SystemFillSuccessBackground", (ThemeResourceKeyType)1);
				_SystemFillSuccessBackgroundColorKey = val2;
				val = val2;
			}
			return val;
		}
	}

	public static ThemeResourceKey SystemFillSuccessBackgroundBrushKey
	{
		get
		{
			//IL_0014: Unknown result type (might be due to invalid IL or missing references)
			//IL_0019: Unknown result type (might be due to invalid IL or missing references)
			//IL_001f: Expected Obj, but got Unknown
			ThemeResourceKey val = _SystemFillSuccessBackgroundBrushKey;
			if (val == null)
			{
				ThemeResourceKey val2 = new ThemeResourceKey(Category, "SystemFillSuccessBackground", (ThemeResourceKeyType)3);
				_SystemFillSuccessBackgroundBrushKey = val2;
				val = val2;
			}
			return val;
		}
	}

	public static ThemeResourceKey TextFillDisabledColorKey
	{
		get
		{
			//IL_0014: Unknown result type (might be due to invalid IL or missing references)
			//IL_0019: Unknown result type (might be due to invalid IL or missing references)
			//IL_001f: Expected Obj, but got Unknown
			ThemeResourceKey val = _TextFillDisabledColorKey;
			if (val == null)
			{
				ThemeResourceKey val2 = new ThemeResourceKey(Category, "TextFillDisabled", (ThemeResourceKeyType)1);
				_TextFillDisabledColorKey = val2;
				val = val2;
			}
			return val;
		}
	}

	public static ThemeResourceKey TextFillDisabledBrushKey
	{
		get
		{
			//IL_0014: Unknown result type (might be due to invalid IL or missing references)
			//IL_0019: Unknown result type (might be due to invalid IL or missing references)
			//IL_001f: Expected Obj, but got Unknown
			ThemeResourceKey val = _TextFillDisabledBrushKey;
			if (val == null)
			{
				ThemeResourceKey val2 = new ThemeResourceKey(Category, "TextFillDisabled", (ThemeResourceKeyType)3);
				_TextFillDisabledBrushKey = val2;
				val = val2;
			}
			return val;
		}
	}

	public static ThemeResourceKey TextFillPrimaryColorKey
	{
		get
		{
			//IL_0014: Unknown result type (might be due to invalid IL or missing references)
			//IL_0019: Unknown result type (might be due to invalid IL or missing references)
			//IL_001f: Expected Obj, but got Unknown
			ThemeResourceKey val = _TextFillPrimaryColorKey;
			if (val == null)
			{
				ThemeResourceKey val2 = new ThemeResourceKey(Category, "TextFillPrimary", (ThemeResourceKeyType)1);
				_TextFillPrimaryColorKey = val2;
				val = val2;
			}
			return val;
		}
	}

	public static ThemeResourceKey TextFillPrimaryBrushKey
	{
		get
		{
			//IL_0014: Unknown result type (might be due to invalid IL or missing references)
			//IL_0019: Unknown result type (might be due to invalid IL or missing references)
			//IL_001f: Expected Obj, but got Unknown
			ThemeResourceKey val = _TextFillPrimaryBrushKey;
			if (val == null)
			{
				ThemeResourceKey val2 = new ThemeResourceKey(Category, "TextFillPrimary", (ThemeResourceKeyType)3);
				_TextFillPrimaryBrushKey = val2;
				val = val2;
			}
			return val;
		}
	}

	public static ThemeResourceKey TextFillSecondaryColorKey
	{
		get
		{
			//IL_0014: Unknown result type (might be due to invalid IL or missing references)
			//IL_0019: Unknown result type (might be due to invalid IL or missing references)
			//IL_001f: Expected Obj, but got Unknown
			ThemeResourceKey val = _TextFillSecondaryColorKey;
			if (val == null)
			{
				ThemeResourceKey val2 = new ThemeResourceKey(Category, "TextFillSecondary", (ThemeResourceKeyType)1);
				_TextFillSecondaryColorKey = val2;
				val = val2;
			}
			return val;
		}
	}

	public static ThemeResourceKey TextFillSecondaryBrushKey
	{
		get
		{
			//IL_0014: Unknown result type (might be due to invalid IL or missing references)
			//IL_0019: Unknown result type (might be due to invalid IL or missing references)
			//IL_001f: Expected Obj, but got Unknown
			ThemeResourceKey val = _TextFillSecondaryBrushKey;
			if (val == null)
			{
				ThemeResourceKey val2 = new ThemeResourceKey(Category, "TextFillSecondary", (ThemeResourceKeyType)3);
				_TextFillSecondaryBrushKey = val2;
				val = val2;
			}
			return val;
		}
	}

	public static ThemeResourceKey TextFillTertiaryColorKey
	{
		get
		{
			//IL_0014: Unknown result type (might be due to invalid IL or missing references)
			//IL_0019: Unknown result type (might be due to invalid IL or missing references)
			//IL_001f: Expected Obj, but got Unknown
			ThemeResourceKey val = _TextFillTertiaryColorKey;
			if (val == null)
			{
				ThemeResourceKey val2 = new ThemeResourceKey(Category, "TextFillTertiary", (ThemeResourceKeyType)1);
				_TextFillTertiaryColorKey = val2;
				val = val2;
			}
			return val;
		}
	}

	public static ThemeResourceKey TextFillTertiaryBrushKey
	{
		get
		{
			//IL_0014: Unknown result type (might be due to invalid IL or missing references)
			//IL_0019: Unknown result type (might be due to invalid IL or missing references)
			//IL_001f: Expected Obj, but got Unknown
			ThemeResourceKey val = _TextFillTertiaryBrushKey;
			if (val == null)
			{
				ThemeResourceKey val2 = new ThemeResourceKey(Category, "TextFillTertiary", (ThemeResourceKeyType)3);
				_TextFillTertiaryBrushKey = val2;
				val = val2;
			}
			return val;
		}
	}

	public static ThemeResourceKey TextOnAccentFillDisabledColorKey
	{
		get
		{
			//IL_0014: Unknown result type (might be due to invalid IL or missing references)
			//IL_0019: Unknown result type (might be due to invalid IL or missing references)
			//IL_001f: Expected Obj, but got Unknown
			ThemeResourceKey val = _TextOnAccentFillDisabledColorKey;
			if (val == null)
			{
				ThemeResourceKey val2 = new ThemeResourceKey(Category, "TextOnAccentFillDisabled", (ThemeResourceKeyType)1);
				_TextOnAccentFillDisabledColorKey = val2;
				val = val2;
			}
			return val;
		}
	}

	public static ThemeResourceKey TextOnAccentFillDisabledBrushKey
	{
		get
		{
			//IL_0014: Unknown result type (might be due to invalid IL or missing references)
			//IL_0019: Unknown result type (might be due to invalid IL or missing references)
			//IL_001f: Expected Obj, but got Unknown
			ThemeResourceKey val = _TextOnAccentFillDisabledBrushKey;
			if (val == null)
			{
				ThemeResourceKey val2 = new ThemeResourceKey(Category, "TextOnAccentFillDisabled", (ThemeResourceKeyType)3);
				_TextOnAccentFillDisabledBrushKey = val2;
				val = val2;
			}
			return val;
		}
	}

	public static ThemeResourceKey TextOnAccentFillPrimaryColorKey
	{
		get
		{
			//IL_0014: Unknown result type (might be due to invalid IL or missing references)
			//IL_0019: Unknown result type (might be due to invalid IL or missing references)
			//IL_001f: Expected Obj, but got Unknown
			ThemeResourceKey val = _TextOnAccentFillPrimaryColorKey;
			if (val == null)
			{
				ThemeResourceKey val2 = new ThemeResourceKey(Category, "TextOnAccentFillPrimary", (ThemeResourceKeyType)1);
				_TextOnAccentFillPrimaryColorKey = val2;
				val = val2;
			}
			return val;
		}
	}

	public static ThemeResourceKey TextOnAccentFillPrimaryBrushKey
	{
		get
		{
			//IL_0014: Unknown result type (might be due to invalid IL or missing references)
			//IL_0019: Unknown result type (might be due to invalid IL or missing references)
			//IL_001f: Expected Obj, but got Unknown
			ThemeResourceKey val = _TextOnAccentFillPrimaryBrushKey;
			if (val == null)
			{
				ThemeResourceKey val2 = new ThemeResourceKey(Category, "TextOnAccentFillPrimary", (ThemeResourceKeyType)3);
				_TextOnAccentFillPrimaryBrushKey = val2;
				val = val2;
			}
			return val;
		}
	}

	public static ThemeResourceKey TextOnAccentFillSecondaryColorKey
	{
		get
		{
			//IL_0014: Unknown result type (might be due to invalid IL or missing references)
			//IL_0019: Unknown result type (might be due to invalid IL or missing references)
			//IL_001f: Expected Obj, but got Unknown
			ThemeResourceKey val = _TextOnAccentFillSecondaryColorKey;
			if (val == null)
			{
				ThemeResourceKey val2 = new ThemeResourceKey(Category, "TextOnAccentFillSecondary", (ThemeResourceKeyType)1);
				_TextOnAccentFillSecondaryColorKey = val2;
				val = val2;
			}
			return val;
		}
	}

	public static ThemeResourceKey TextOnAccentFillSecondaryBrushKey
	{
		get
		{
			//IL_0014: Unknown result type (might be due to invalid IL or missing references)
			//IL_0019: Unknown result type (might be due to invalid IL or missing references)
			//IL_001f: Expected Obj, but got Unknown
			ThemeResourceKey val = _TextOnAccentFillSecondaryBrushKey;
			if (val == null)
			{
				ThemeResourceKey val2 = new ThemeResourceKey(Category, "TextOnAccentFillSecondary", (ThemeResourceKeyType)3);
				_TextOnAccentFillSecondaryBrushKey = val2;
				val = val2;
			}
			return val;
		}
	}

	public static ThemeResourceKey TextOnAccentFillSelectedTextColorKey
	{
		get
		{
			//IL_0014: Unknown result type (might be due to invalid IL or missing references)
			//IL_0019: Unknown result type (might be due to invalid IL or missing references)
			//IL_001f: Expected Obj, but got Unknown
			ThemeResourceKey val = _TextOnAccentFillSelectedTextColorKey;
			if (val == null)
			{
				ThemeResourceKey val2 = new ThemeResourceKey(Category, "TextOnAccentFillSelectedText", (ThemeResourceKeyType)1);
				_TextOnAccentFillSelectedTextColorKey = val2;
				val = val2;
			}
			return val;
		}
	}

	public static ThemeResourceKey TextOnAccentFillSelectedTextBrushKey
	{
		get
		{
			//IL_0014: Unknown result type (might be due to invalid IL or missing references)
			//IL_0019: Unknown result type (might be due to invalid IL or missing references)
			//IL_001f: Expected Obj, but got Unknown
			ThemeResourceKey val = _TextOnAccentFillSelectedTextBrushKey;
			if (val == null)
			{
				ThemeResourceKey val2 = new ThemeResourceKey(Category, "TextOnAccentFillSelectedText", (ThemeResourceKeyType)3);
				_TextOnAccentFillSelectedTextBrushKey = val2;
				val = val2;
			}
			return val;
		}
	}
}
