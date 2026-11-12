using System.Windows;

namespace Microsoft.VisualStudio.Shell.Styles;

public static class Spacings
{
	public static readonly double PrimitiveNone = 0.0;

	public static readonly double PrimitiveXXXS = 1.0;

	public static readonly double PrimitiveXXS = 2.0;

	public static readonly double PrimitiveXS = 4.0;

	public static readonly double PrimitiveS = 8.0;

	public static readonly double PrimitiveM = 12.0;

	public static readonly double PrimitiveL = 16.0;

	public static readonly double PrimitiveXL = 20.0;

	public static readonly double PrimitiveXXL = 24.0;

	public static readonly double PrimitiveXXXL = 32.0;

	public static readonly Thickness None = new Thickness(PrimitiveNone);

	public static readonly Thickness XXXS = new Thickness(PrimitiveXXXS);

	public static readonly Thickness XXS = new Thickness(PrimitiveXXS);

	public static readonly Thickness XS = new Thickness(PrimitiveXS);

	public static readonly Thickness S = new Thickness(PrimitiveS);

	public static readonly Thickness M = new Thickness(PrimitiveM);

	public static readonly Thickness L = new Thickness(PrimitiveL);

	public static readonly Thickness XL = new Thickness(PrimitiveXL);

	public static readonly Thickness XXL = new Thickness(PrimitiveXXL);

	public static readonly Thickness XXXL = new Thickness(PrimitiveXXXL);

	public static readonly Thickness LeftXXXS = new Thickness(PrimitiveXXXS, PrimitiveNone, PrimitiveNone, PrimitiveNone);

	public static readonly Thickness LeftXXS = new Thickness(PrimitiveXXS, PrimitiveNone, PrimitiveNone, PrimitiveNone);

	public static readonly Thickness LeftXS = new Thickness(PrimitiveXS, PrimitiveNone, PrimitiveNone, PrimitiveNone);

	public static readonly Thickness LeftS = new Thickness(PrimitiveS, PrimitiveNone, PrimitiveNone, PrimitiveNone);

	public static readonly Thickness LeftM = new Thickness(PrimitiveM, PrimitiveNone, PrimitiveNone, PrimitiveNone);

	public static readonly Thickness LeftL = new Thickness(PrimitiveL, PrimitiveNone, PrimitiveNone, PrimitiveNone);

	public static readonly Thickness LeftXL = new Thickness(PrimitiveXL, PrimitiveNone, PrimitiveNone, PrimitiveNone);

	public static readonly Thickness LeftXXL = new Thickness(PrimitiveXXL, PrimitiveNone, PrimitiveNone, PrimitiveNone);

	public static readonly Thickness LeftXXXL = new Thickness(PrimitiveXXXL, PrimitiveNone, PrimitiveNone, PrimitiveNone);

	public static readonly Thickness TopXXXS = new Thickness(PrimitiveNone, PrimitiveXXXS, PrimitiveNone, PrimitiveNone);

	public static readonly Thickness TopXXS = new Thickness(PrimitiveNone, PrimitiveXXS, PrimitiveNone, PrimitiveNone);

	public static readonly Thickness TopXS = new Thickness(PrimitiveNone, PrimitiveXS, PrimitiveNone, PrimitiveNone);

	public static readonly Thickness TopS = new Thickness(PrimitiveNone, PrimitiveS, PrimitiveNone, PrimitiveNone);

	public static readonly Thickness TopM = new Thickness(PrimitiveNone, PrimitiveM, PrimitiveNone, PrimitiveNone);

	public static readonly Thickness TopL = new Thickness(PrimitiveNone, PrimitiveL, PrimitiveNone, PrimitiveNone);

	public static readonly Thickness TopXL = new Thickness(PrimitiveNone, PrimitiveXL, PrimitiveNone, PrimitiveNone);

	public static readonly Thickness TopXXL = new Thickness(PrimitiveNone, PrimitiveXXL, PrimitiveNone, PrimitiveNone);

	public static readonly Thickness TopXXXL = new Thickness(PrimitiveNone, PrimitiveXXXL, PrimitiveNone, PrimitiveNone);

	public static readonly Thickness RightXXXS = new Thickness(PrimitiveNone, PrimitiveNone, PrimitiveXXXS, PrimitiveNone);

	public static readonly Thickness RightXXS = new Thickness(PrimitiveNone, PrimitiveNone, PrimitiveXXS, PrimitiveNone);

	public static readonly Thickness RightXS = new Thickness(PrimitiveNone, PrimitiveNone, PrimitiveXS, PrimitiveNone);

	public static readonly Thickness RightS = new Thickness(PrimitiveNone, PrimitiveNone, PrimitiveS, PrimitiveNone);

	public static readonly Thickness RightM = new Thickness(PrimitiveNone, PrimitiveNone, PrimitiveM, PrimitiveNone);

	public static readonly Thickness RightL = new Thickness(PrimitiveNone, PrimitiveNone, PrimitiveL, PrimitiveNone);

	public static readonly Thickness RightXL = new Thickness(PrimitiveNone, PrimitiveNone, PrimitiveXL, PrimitiveNone);

	public static readonly Thickness RightXXL = new Thickness(PrimitiveNone, PrimitiveNone, PrimitiveXXL, PrimitiveNone);

	public static readonly Thickness RightXXXL = new Thickness(PrimitiveNone, PrimitiveNone, PrimitiveXXXL, PrimitiveNone);

	public static readonly Thickness BottomXXXS = new Thickness(PrimitiveNone, PrimitiveNone, PrimitiveNone, PrimitiveXXXS);

	public static readonly Thickness BottomXXS = new Thickness(PrimitiveNone, PrimitiveNone, PrimitiveNone, PrimitiveXXS);

	public static readonly Thickness BottomXS = new Thickness(PrimitiveNone, PrimitiveNone, PrimitiveNone, PrimitiveXS);

	public static readonly Thickness BottomS = new Thickness(PrimitiveNone, PrimitiveNone, PrimitiveNone, PrimitiveS);

	public static readonly Thickness BottomM = new Thickness(PrimitiveNone, PrimitiveNone, PrimitiveNone, PrimitiveM);

	public static readonly Thickness BottomL = new Thickness(PrimitiveNone, PrimitiveNone, PrimitiveNone, PrimitiveL);

	public static readonly Thickness BottomXL = new Thickness(PrimitiveNone, PrimitiveNone, PrimitiveNone, PrimitiveXL);

	public static readonly Thickness BottomXXL = new Thickness(PrimitiveNone, PrimitiveNone, PrimitiveNone, PrimitiveXXL);

	public static readonly Thickness BottomXXXL = new Thickness(PrimitiveNone, PrimitiveNone, PrimitiveNone, PrimitiveXXXL);

	public static readonly Thickness HorizontalXXXS = new Thickness(PrimitiveXXXS, PrimitiveNone, PrimitiveXXXS, PrimitiveNone);

	public static readonly Thickness HorizontalXXS = new Thickness(PrimitiveXXS, PrimitiveNone, PrimitiveXXS, PrimitiveNone);

	public static readonly Thickness HorizontalXS = new Thickness(PrimitiveXS, PrimitiveNone, PrimitiveXS, PrimitiveNone);

	public static readonly Thickness HorizontalS = new Thickness(PrimitiveS, PrimitiveNone, PrimitiveS, PrimitiveNone);

	public static readonly Thickness HorizontalM = new Thickness(PrimitiveM, PrimitiveNone, PrimitiveM, PrimitiveNone);

	public static readonly Thickness HorizontalL = new Thickness(PrimitiveL, PrimitiveNone, PrimitiveL, PrimitiveNone);

	public static readonly Thickness HorizontalXL = new Thickness(PrimitiveXL, PrimitiveNone, PrimitiveXL, PrimitiveNone);

	public static readonly Thickness HorizontalXXL = new Thickness(PrimitiveXXL, PrimitiveNone, PrimitiveXXL, PrimitiveNone);

	public static readonly Thickness HorizontalXXXL = new Thickness(PrimitiveXXXL, PrimitiveNone, PrimitiveXXXL, PrimitiveNone);

	public static readonly Thickness VerticalXXXS = new Thickness(PrimitiveNone, PrimitiveXXXS, PrimitiveNone, PrimitiveXXXS);

	public static readonly Thickness VerticalXXS = new Thickness(PrimitiveNone, PrimitiveXXS, PrimitiveNone, PrimitiveXXS);

	public static readonly Thickness VerticalXS = new Thickness(PrimitiveNone, PrimitiveXS, PrimitiveNone, PrimitiveXS);

	public static readonly Thickness VerticalS = new Thickness(PrimitiveNone, PrimitiveS, PrimitiveNone, PrimitiveS);

	public static readonly Thickness VerticalM = new Thickness(PrimitiveNone, PrimitiveM, PrimitiveNone, PrimitiveM);

	public static readonly Thickness VerticalL = new Thickness(PrimitiveNone, PrimitiveL, PrimitiveNone, PrimitiveL);

	public static readonly Thickness VerticalXL = new Thickness(PrimitiveNone, PrimitiveXL, PrimitiveNone, PrimitiveXL);

	public static readonly Thickness VerticalXXL = new Thickness(PrimitiveNone, PrimitiveXXL, PrimitiveNone, PrimitiveXXL);

	public static readonly Thickness VerticalXXXL = new Thickness(PrimitiveNone, PrimitiveXXXL, PrimitiveNone, PrimitiveXXXL);

	public static readonly Thickness XXXSInvert = new Thickness(0.0 - PrimitiveXXXS);

	public static readonly Thickness XXSInvert = new Thickness(0.0 - PrimitiveXXS);

	public static readonly Thickness XSInvert = new Thickness(0.0 - PrimitiveXS);

	public static readonly Thickness SInvert = new Thickness(0.0 - PrimitiveS);

	public static readonly Thickness LeftXXXSInvert = new Thickness(0.0 - PrimitiveXXXS, PrimitiveNone, PrimitiveNone, PrimitiveNone);

	public static readonly Thickness LeftXXSInvert = new Thickness(0.0 - PrimitiveXXS, PrimitiveNone, PrimitiveNone, PrimitiveNone);

	public static readonly Thickness LeftXSInvert = new Thickness(0.0 - PrimitiveXS, PrimitiveNone, PrimitiveNone, PrimitiveNone);

	public static readonly Thickness LeftSInvert = new Thickness(0.0 - PrimitiveS, PrimitiveNone, PrimitiveNone, PrimitiveNone);

	public static readonly Thickness TopXXXSInvert = new Thickness(PrimitiveNone, 0.0 - PrimitiveXXXS, PrimitiveNone, PrimitiveNone);

	public static readonly Thickness TopXXSInvert = new Thickness(PrimitiveNone, 0.0 - PrimitiveXXS, PrimitiveNone, PrimitiveNone);

	public static readonly Thickness TopXSInvert = new Thickness(PrimitiveNone, 0.0 - PrimitiveXS, PrimitiveNone, PrimitiveNone);

	public static readonly Thickness TopSInvert = new Thickness(PrimitiveNone, 0.0 - PrimitiveS, PrimitiveNone, PrimitiveNone);

	public static readonly Thickness RightXXXSInvert = new Thickness(PrimitiveNone, PrimitiveNone, 0.0 - PrimitiveXXXS, PrimitiveNone);

	public static readonly Thickness RightXXSInvert = new Thickness(PrimitiveNone, PrimitiveNone, 0.0 - PrimitiveXXS, PrimitiveNone);

	public static readonly Thickness RightXSInvert = new Thickness(PrimitiveNone, PrimitiveNone, 0.0 - PrimitiveXS, PrimitiveNone);

	public static readonly Thickness RightSInvert = new Thickness(PrimitiveNone, PrimitiveNone, 0.0 - PrimitiveS, PrimitiveNone);

	public static readonly Thickness BottomXXXSInvert = new Thickness(PrimitiveNone, PrimitiveNone, PrimitiveNone, 0.0 - PrimitiveXXXS);

	public static readonly Thickness BottomXXSInvert = new Thickness(PrimitiveNone, PrimitiveNone, PrimitiveNone, 0.0 - PrimitiveXXS);

	public static readonly Thickness BottomXSInvert = new Thickness(PrimitiveNone, PrimitiveNone, PrimitiveNone, 0.0 - PrimitiveXS);

	public static readonly Thickness BottomSInvert = new Thickness(PrimitiveNone, PrimitiveNone, PrimitiveNone, 0.0 - PrimitiveS);

	public static readonly Thickness HorizontalXXXSInvert = new Thickness(0.0 - PrimitiveXXXS, PrimitiveNone, 0.0 - PrimitiveXXXS, PrimitiveNone);

	public static readonly Thickness HorizontalXXSInvert = new Thickness(0.0 - PrimitiveXXS, PrimitiveNone, 0.0 - PrimitiveXXS, PrimitiveNone);

	public static readonly Thickness HorizontalXSInvert = new Thickness(0.0 - PrimitiveXS, PrimitiveNone, 0.0 - PrimitiveXS, PrimitiveNone);

	public static readonly Thickness HorizontalSInvert = new Thickness(0.0 - PrimitiveS, PrimitiveNone, 0.0 - PrimitiveS, PrimitiveNone);

	public static readonly Thickness VerticalXXXSInvert = new Thickness(PrimitiveNone, 0.0 - PrimitiveXXXS, PrimitiveNone, 0.0 - PrimitiveXXXS);

	public static readonly Thickness VerticalXXSInvert = new Thickness(PrimitiveNone, 0.0 - PrimitiveXXS, PrimitiveNone, 0.0 - PrimitiveXXS);

	public static readonly Thickness VerticalXSInvert = new Thickness(PrimitiveNone, 0.0 - PrimitiveXS, PrimitiveNone, 0.0 - PrimitiveXS);

	public static readonly Thickness VerticalSInvert = new Thickness(PrimitiveNone, 0.0 - PrimitiveS, PrimitiveNone, 0.0 - PrimitiveS);

	public static readonly double PrimitiveXSNudge = 3.0;

	public static readonly double PrimitiveSNudge = 6.0;

	public static readonly double PrimitiveMNudge = 10.0;

	public static readonly Thickness XSNudge = new Thickness(PrimitiveXSNudge);

	public static readonly Thickness SNudge = new Thickness(PrimitiveSNudge);

	public static readonly Thickness MNudge = new Thickness(PrimitiveMNudge);

	public static readonly Thickness LeftXSNudge = new Thickness(PrimitiveXSNudge, PrimitiveNone, PrimitiveNone, PrimitiveNone);

	public static readonly Thickness LeftSNudge = new Thickness(PrimitiveSNudge, PrimitiveNone, PrimitiveNone, PrimitiveNone);

	public static readonly Thickness LeftMNudge = new Thickness(PrimitiveMNudge, PrimitiveNone, PrimitiveNone, PrimitiveNone);

	public static readonly Thickness TopXSNudge = new Thickness(PrimitiveNone, PrimitiveXSNudge, PrimitiveNone, PrimitiveNone);

	public static readonly Thickness TopSNudge = new Thickness(PrimitiveNone, PrimitiveSNudge, PrimitiveNone, PrimitiveNone);

	public static readonly Thickness TopMNudge = new Thickness(PrimitiveNone, PrimitiveMNudge, PrimitiveNone, PrimitiveNone);

	public static readonly Thickness RightXSNudge = new Thickness(PrimitiveNone, PrimitiveNone, PrimitiveXSNudge, PrimitiveNone);

	public static readonly Thickness RightSNudge = new Thickness(PrimitiveNone, PrimitiveNone, PrimitiveSNudge, PrimitiveNone);

	public static readonly Thickness RightMNudge = new Thickness(PrimitiveNone, PrimitiveNone, PrimitiveMNudge, PrimitiveNone);

	public static readonly Thickness BottomXSNudge = new Thickness(PrimitiveNone, PrimitiveNone, PrimitiveNone, PrimitiveXSNudge);

	public static readonly Thickness BottomSNudge = new Thickness(PrimitiveNone, PrimitiveNone, PrimitiveNone, PrimitiveSNudge);

	public static readonly Thickness BottomMNudge = new Thickness(PrimitiveNone, PrimitiveNone, PrimitiveNone, PrimitiveMNudge);

	public static readonly Thickness HorizontalXSNudge = new Thickness(PrimitiveXSNudge, PrimitiveNone, PrimitiveXSNudge, PrimitiveNone);

	public static readonly Thickness HorizontalSNudge = new Thickness(PrimitiveSNudge, PrimitiveNone, PrimitiveSNudge, PrimitiveNone);

	public static readonly Thickness HorizontalMNudge = new Thickness(PrimitiveMNudge, PrimitiveNone, PrimitiveMNudge, PrimitiveNone);

	public static readonly Thickness VerticalXSNudge = new Thickness(PrimitiveNone, PrimitiveXSNudge, PrimitiveNone, PrimitiveXSNudge);

	public static readonly Thickness VerticalSNudge = new Thickness(PrimitiveNone, PrimitiveSNudge, PrimitiveNone, PrimitiveSNudge);

	public static readonly Thickness VerticalMNudge = new Thickness(PrimitiveNone, PrimitiveMNudge, PrimitiveNone, PrimitiveMNudge);

	public static readonly Thickness XSNudgeInvert = new Thickness(0.0 - PrimitiveXSNudge);

	public static readonly Thickness LeftXSNudgeInvert = new Thickness(0.0 - PrimitiveXSNudge, 0.0, 0.0, 0.0);

	public static readonly Thickness TopXSNudgeInvert = new Thickness(0.0, 0.0 - PrimitiveXSNudge, 0.0, 0.0);

	public static readonly Thickness RightXSNudgeInvert = new Thickness(0.0, 0.0, 0.0 - PrimitiveXSNudge, 0.0);

	public static readonly Thickness BottomXSNudgeInvert = new Thickness(0.0, 0.0, 0.0, 0.0 - PrimitiveXSNudge);

	public static readonly Thickness HorizontalXSNudgeInvert = new Thickness(0.0 - PrimitiveXSNudge, 0.0, 0.0 - PrimitiveXSNudge, 0.0);

	public static readonly Thickness VerticalXSNudgeInvert = new Thickness(0.0, 0.0 - PrimitiveXSNudge, 0.0, 0.0 - PrimitiveXSNudge);
}
