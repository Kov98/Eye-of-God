using System.Windows;

namespace Microsoft.VisualStudio.Shell.Styles;

public static class CornerRadii
{
	public static readonly double PrimitiveNone = 0.0;

	public static readonly double PrimitiveS = 2.0;

	public static readonly double PrimitiveM = 4.0;

	public static readonly double PrimitiveL = 6.0;

	public static readonly double PrimitiveXL = 8.0;

	public static readonly double PrimitiveFocusRect = 6.0;

	public static readonly CornerRadius None = new CornerRadius(PrimitiveNone);

	public static readonly CornerRadius S = new CornerRadius(PrimitiveS);

	public static readonly CornerRadius M = new CornerRadius(PrimitiveM);

	public static readonly CornerRadius L = new CornerRadius(PrimitiveL);

	public static readonly CornerRadius XL = new CornerRadius(PrimitiveXL);

	public static readonly CornerRadius FocusRect = new CornerRadius(PrimitiveFocusRect);

	public static readonly CornerRadius TopLeftS = new CornerRadius(PrimitiveS, PrimitiveNone, PrimitiveNone, PrimitiveNone);

	public static readonly CornerRadius TopLeftM = new CornerRadius(PrimitiveM, PrimitiveNone, PrimitiveNone, PrimitiveNone);

	public static readonly CornerRadius TopLeftL = new CornerRadius(PrimitiveL, PrimitiveNone, PrimitiveNone, PrimitiveNone);

	public static readonly CornerRadius TopLeftXL = new CornerRadius(PrimitiveXL, PrimitiveNone, PrimitiveNone, PrimitiveNone);

	public static readonly CornerRadius TopRightS = new CornerRadius(PrimitiveNone, PrimitiveS, PrimitiveNone, PrimitiveNone);

	public static readonly CornerRadius TopRightM = new CornerRadius(PrimitiveNone, PrimitiveM, PrimitiveNone, PrimitiveNone);

	public static readonly CornerRadius TopRightL = new CornerRadius(PrimitiveNone, PrimitiveL, PrimitiveNone, PrimitiveNone);

	public static readonly CornerRadius TopRightXL = new CornerRadius(PrimitiveNone, PrimitiveXL, PrimitiveNone, PrimitiveNone);

	public static readonly CornerRadius BottomRightS = new CornerRadius(PrimitiveNone, PrimitiveNone, PrimitiveS, PrimitiveNone);

	public static readonly CornerRadius BottomRightM = new CornerRadius(PrimitiveNone, PrimitiveNone, PrimitiveM, PrimitiveNone);

	public static readonly CornerRadius BottomRightL = new CornerRadius(PrimitiveNone, PrimitiveNone, PrimitiveL, PrimitiveNone);

	public static readonly CornerRadius BottomRightXL = new CornerRadius(PrimitiveNone, PrimitiveNone, PrimitiveXL, PrimitiveNone);

	public static readonly CornerRadius BottomLeftS = new CornerRadius(PrimitiveNone, PrimitiveNone, PrimitiveNone, PrimitiveS);

	public static readonly CornerRadius BottomLeftM = new CornerRadius(PrimitiveNone, PrimitiveNone, PrimitiveNone, PrimitiveM);

	public static readonly CornerRadius BottomLeftL = new CornerRadius(PrimitiveNone, PrimitiveNone, PrimitiveNone, PrimitiveL);

	public static readonly CornerRadius BottomLeftXL = new CornerRadius(PrimitiveNone, PrimitiveNone, PrimitiveNone, PrimitiveXL);

	public static readonly CornerRadius TopLeftBottomLeftS = new CornerRadius(PrimitiveS, PrimitiveNone, PrimitiveNone, PrimitiveS);

	public static readonly CornerRadius TopLeftBottomLeftM = new CornerRadius(PrimitiveM, PrimitiveNone, PrimitiveNone, PrimitiveM);

	public static readonly CornerRadius TopLeftBottomLeftL = new CornerRadius(PrimitiveL, PrimitiveNone, PrimitiveNone, PrimitiveL);

	public static readonly CornerRadius TopLeftBottomLeftXL = new CornerRadius(PrimitiveXL, PrimitiveNone, PrimitiveNone, PrimitiveXL);

	public static readonly CornerRadius TopRightBottomRightS = new CornerRadius(PrimitiveNone, PrimitiveS, PrimitiveS, PrimitiveNone);

	public static readonly CornerRadius TopRightBottomRightM = new CornerRadius(PrimitiveNone, PrimitiveM, PrimitiveM, PrimitiveNone);

	public static readonly CornerRadius TopRightBottomRightL = new CornerRadius(PrimitiveNone, PrimitiveL, PrimitiveL, PrimitiveNone);

	public static readonly CornerRadius TopRightBottomRightXL = new CornerRadius(PrimitiveNone, PrimitiveXL, PrimitiveXL, PrimitiveNone);

	public static readonly CornerRadius TopLeftTopRightS = new CornerRadius(PrimitiveS, PrimitiveS, PrimitiveNone, PrimitiveNone);

	public static readonly CornerRadius TopLeftTopRightM = new CornerRadius(PrimitiveM, PrimitiveM, PrimitiveNone, PrimitiveNone);

	public static readonly CornerRadius TopLeftTopRightL = new CornerRadius(PrimitiveL, PrimitiveL, PrimitiveNone, PrimitiveNone);

	public static readonly CornerRadius TopLeftTopRightXL = new CornerRadius(PrimitiveXL, PrimitiveXL, PrimitiveNone, PrimitiveNone);

	public static readonly CornerRadius BottomRightBottomLeftS = new CornerRadius(PrimitiveNone, PrimitiveNone, PrimitiveS, PrimitiveS);

	public static readonly CornerRadius BottomRightBottomLeftM = new CornerRadius(PrimitiveNone, PrimitiveNone, PrimitiveM, PrimitiveM);

	public static readonly CornerRadius BottomRightBottomLeftL = new CornerRadius(PrimitiveNone, PrimitiveNone, PrimitiveL, PrimitiveL);

	public static readonly CornerRadius BottomRightBottomLeftXL = new CornerRadius(PrimitiveNone, PrimitiveNone, PrimitiveXL, PrimitiveXL);

	public static readonly CornerRadius TopLeftBottomRightBottomLeftS = new CornerRadius(PrimitiveS, PrimitiveNone, PrimitiveS, PrimitiveS);

	public static readonly CornerRadius TopLeftBottomRightBottomLeftM = new CornerRadius(PrimitiveM, PrimitiveNone, PrimitiveM, PrimitiveM);

	public static readonly CornerRadius TopLeftBottomRightBottomLeftL = new CornerRadius(PrimitiveL, PrimitiveNone, PrimitiveL, PrimitiveL);

	public static readonly CornerRadius TopLeftBottomRightBottomLeftXL = new CornerRadius(PrimitiveXL, PrimitiveNone, PrimitiveXL, PrimitiveXL);

	public static readonly CornerRadius TopLeftTopRightBottomLeftS = new CornerRadius(PrimitiveS, PrimitiveS, PrimitiveNone, PrimitiveS);

	public static readonly CornerRadius TopLeftTopRightBottomLeftM = new CornerRadius(PrimitiveM, PrimitiveM, PrimitiveNone, PrimitiveM);

	public static readonly CornerRadius TopLeftTopRightBottomLeftL = new CornerRadius(PrimitiveL, PrimitiveL, PrimitiveNone, PrimitiveL);

	public static readonly CornerRadius TopLeftTopRightBottomLeftXL = new CornerRadius(PrimitiveXL, PrimitiveXL, PrimitiveNone, PrimitiveXL);

	public static readonly CornerRadius TopLeftTopRightBottomRightS = new CornerRadius(PrimitiveS, PrimitiveS, PrimitiveS, PrimitiveNone);

	public static readonly CornerRadius TopLeftTopRightBottomRightM = new CornerRadius(PrimitiveM, PrimitiveM, PrimitiveM, PrimitiveNone);

	public static readonly CornerRadius TopLeftTopRightBottomRightL = new CornerRadius(PrimitiveL, PrimitiveL, PrimitiveL, PrimitiveNone);

	public static readonly CornerRadius TopLeftTopRightBottomRightXL = new CornerRadius(PrimitiveXL, PrimitiveXL, PrimitiveXL, PrimitiveNone);

	public static readonly CornerRadius TopRightBottomRightBottomLeftS = new CornerRadius(PrimitiveNone, PrimitiveS, PrimitiveS, PrimitiveS);

	public static readonly CornerRadius TopRightBottomRightBottomLeftM = new CornerRadius(PrimitiveNone, PrimitiveM, PrimitiveM, PrimitiveM);

	public static readonly CornerRadius TopRightBottomRightBottomLeftL = new CornerRadius(PrimitiveNone, PrimitiveL, PrimitiveL, PrimitiveL);

	public static readonly CornerRadius TopRightBottomRightBottomLeftXL = new CornerRadius(PrimitiveNone, PrimitiveXL, PrimitiveXL, PrimitiveXL);
}
