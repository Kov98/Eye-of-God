using System;
using System.Text;
using System.Windows.Media;
using System.Windows.Threading;
using Microsoft.VisualStudio.Shell.Helpers;
using Microsoft.VisualStudio.Shell.Interop;

namespace Microsoft.VisualStudio.Shell.Styles;

public class Typography : ObservableClass, IVsBroadcastMessageEvents
{
	private const uint WM_SYSCOLORCHANGE = 21u;

	public static readonly double BodyLineHeight = 20.0;

	public static readonly double BodyLargeLineHeight = 24.0;

	public static readonly double CaptionLineHeight = 16.0;

	public static readonly double CodeLineHeight = 20.0;

	public static readonly double DisplayLineHeight = 92.0;

	public static readonly double SubtitleLineHeight = 28.0;

	public static readonly double TitleLineHeight = 36.0;

	public static readonly double TitleLargeLineHeight = 52.0;

	private static Typography? instance;

	private double bodyFontSize = 14.0;

	private double bodyLargeFontSize = 18.0;

	private double captionFontSize = 12.0;

	private FontFamily codeFontFamily = new FontFamily("Cascadia Mono");

	private double codeFontSize = 14.0;

	private double displayFontSize = 68.0;

	private FontFamily shellFontFamily = new FontFamily("Segoe UI");

	private double shellFontSize = 12.0;

	private double subtitleFontSize = 20.0;

	private double titleFontSize = 28.0;

	private double titleLargeFontSize = 40.0;

	public static Typography Instance => instance ?? (instance = new Typography());

	public double BodyFontSize
	{
		get
		{
			return bodyFontSize;
		}
		private set
		{
			SetProperty(ref bodyFontSize, value, "BodyFontSize");
		}
	}

	public double BodyLargeFontSize
	{
		get
		{
			return bodyLargeFontSize;
		}
		private set
		{
			SetProperty(ref bodyLargeFontSize, value, "BodyLargeFontSize");
		}
	}

	public double CaptionFontSize
	{
		get
		{
			return captionFontSize;
		}
		private set
		{
			SetProperty(ref captionFontSize, value, "CaptionFontSize");
		}
	}

	public FontFamily CodeFontFamily
	{
		get
		{
			return codeFontFamily;
		}
		private set
		{
			SetProperty(ref codeFontFamily, value, "CodeFontFamily");
		}
	}

	public double CodeFontSize
	{
		get
		{
			return codeFontSize;
		}
		private set
		{
			SetProperty(ref codeFontSize, value, "CodeFontSize");
		}
	}

	public double DisplayFontSize
	{
		get
		{
			return displayFontSize;
		}
		private set
		{
			SetProperty(ref displayFontSize, value, "DisplayFontSize");
		}
	}

	public FontFamily ShellFontFamily
	{
		get
		{
			return shellFontFamily;
		}
		private set
		{
			SetProperty(ref shellFontFamily, value, "ShellFontFamily");
		}
	}

	public double ShellFontSize
	{
		get
		{
			return shellFontSize;
		}
		private set
		{
			SetProperty(ref shellFontSize, value, "ShellFontSize");
		}
	}

	public double SubtitleFontSize
	{
		get
		{
			return subtitleFontSize;
		}
		private set
		{
			SetProperty(ref subtitleFontSize, value, "SubtitleFontSize");
		}
	}

	public double TitleFontSize
	{
		get
		{
			return titleFontSize;
		}
		private set
		{
			SetProperty(ref titleFontSize, value, "TitleFontSize");
		}
	}

	public double TitleLargeFontSize
	{
		get
		{
			return titleLargeFontSize;
		}
		private set
		{
			SetProperty(ref titleLargeFontSize, value, "TitleLargeFontSize");
		}
	}

	private Typography()
	{
		Dispatcher.CurrentDispatcher.VerifyAccess();
		object service = ServiceProvider.GlobalProvider.GetService(typeof(SVsShell));
		IVsShell val = (IVsShell)((service is IVsShell) ? service : null);
		if (val != null)
		{
			UpdateFontValues();
			uint num = default;
			val.AdviseBroadcastMessages((IVsBroadcastMessageEvents)(object)this, ref num);
		}
	}

	int IVsBroadcastMessageEvents.OnBroadcastMessage(uint msg, IntPtr wParam, IntPtr lParam)
	{
		if (msg == 21)
		{
			UpdateFontValues();
		}
		return 0;
	}

	private string GetFontFamilyName(UIDLGLOGFONT logFont)
	{
		//IL_002a: Unknown result type (might be due to invalid IL or missing references)
		//IL_000c: Unknown result type (might be due to invalid IL or missing references)
		//IL_0017: Unknown result type (might be due to invalid IL or missing references)
		StringBuilder stringBuilder = new StringBuilder(32);
		for (int i = 0; i < logFont.lfFaceName.Length && logFont.lfFaceName[i] != 0; i++)
		{
			stringBuilder.Append((char)logFont.lfFaceName[i]);
		}
		return stringBuilder.ToString();
	}

	private void UpdateFontValues()
	{
		//IL_0038: Unknown result type (might be due to invalid IL or missing references)
		object service = ServiceProvider.GlobalProvider.GetService(typeof(SUIHostLocale));
		IUIHostLocale val = (IUIHostLocale)((service is IUIHostLocale) ? service : null);
		if (val != null)
		{
			UIDLGLOGFONT[] array = new UIDLGLOGFONT[1];
			if (!ErrorHandler.Failed(val.GetDialogFont(array)))
			{
				ShellFontFamily = new FontFamily(GetFontFamilyName(array[0]));
				ShellFontSize = (double)Math.Abs(array[0].lfHeight) * 96.0 / VisualTree.SystemDpiY;
				BodyFontSize = Math.Round(ShellFontSize * 1.1667);
				BodyLargeFontSize = Math.Round(ShellFontSize * 1.5);
				CaptionFontSize = ShellFontSize;
				CodeFontSize = Math.Round(ShellFontSize * 1.1667);
				DisplayFontSize = Math.Round(ShellFontSize * 5.6667);
				SubtitleFontSize = Math.Round(ShellFontSize * 1.6667);
				TitleFontSize = Math.Round(ShellFontSize * 2.3333);
				TitleLargeFontSize = Math.Round(ShellFontSize * 3.3333);
			}
		}
	}
}
