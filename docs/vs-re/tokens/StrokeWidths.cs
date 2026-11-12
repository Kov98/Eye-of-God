using System.Windows;

namespace Microsoft.VisualStudio.Shell.Styles;

public static class StrokeWidths
{
	public static readonly double PrimitiveNone = 0.0;

	public static readonly double PrimitiveThin = 1.0;

	public static readonly double PrimitiveThick = 2.0;

	public static readonly double PrimitiveThicker = 3.0;

	public static readonly double PrimitiveThickest = 4.0;

	public static readonly Thickness None = new Thickness(PrimitiveNone);

	public static readonly Thickness Thin = new Thickness(PrimitiveThin);

	public static readonly Thickness Thick = new Thickness(PrimitiveThick);

	public static readonly Thickness Thicker = new Thickness(PrimitiveThicker);

	public static readonly Thickness Thickest = new Thickness(PrimitiveThickest);

	public static readonly Thickness LeftThin = new Thickness(PrimitiveThin, PrimitiveNone, PrimitiveNone, PrimitiveNone);

	public static readonly Thickness LeftThick = new Thickness(PrimitiveThick, PrimitiveNone, PrimitiveNone, PrimitiveNone);

	public static readonly Thickness LeftThicker = new Thickness(PrimitiveThicker, PrimitiveNone, PrimitiveNone, PrimitiveNone);

	public static readonly Thickness LeftThickest = new Thickness(PrimitiveThickest, PrimitiveNone, PrimitiveNone, PrimitiveNone);

	public static readonly Thickness TopThin = new Thickness(PrimitiveNone, PrimitiveThin, PrimitiveNone, PrimitiveNone);

	public static readonly Thickness TopThick = new Thickness(PrimitiveNone, PrimitiveThick, PrimitiveNone, PrimitiveNone);

	public static readonly Thickness TopThicker = new Thickness(PrimitiveNone, PrimitiveThicker, PrimitiveNone, PrimitiveNone);

	public static readonly Thickness TopThickest = new Thickness(PrimitiveNone, PrimitiveThickest, PrimitiveNone, PrimitiveNone);

	public static readonly Thickness RightThin = new Thickness(PrimitiveNone, PrimitiveNone, PrimitiveThin, PrimitiveNone);

	public static readonly Thickness RightThick = new Thickness(PrimitiveNone, PrimitiveNone, PrimitiveThick, PrimitiveNone);

	public static readonly Thickness RightThicker = new Thickness(PrimitiveNone, PrimitiveNone, PrimitiveThicker, PrimitiveNone);

	public static readonly Thickness RightThickest = new Thickness(PrimitiveNone, PrimitiveNone, PrimitiveThickest, PrimitiveNone);

	public static readonly Thickness BottomThin = new Thickness(PrimitiveNone, PrimitiveNone, PrimitiveNone, PrimitiveThin);

	public static readonly Thickness BottomThick = new Thickness(PrimitiveNone, PrimitiveNone, PrimitiveNone, PrimitiveThick);

	public static readonly Thickness BottomThicker = new Thickness(PrimitiveNone, PrimitiveNone, PrimitiveNone, PrimitiveThicker);

	public static readonly Thickness BottomThickest = new Thickness(PrimitiveNone, PrimitiveNone, PrimitiveNone, PrimitiveThickest);

	public static readonly Thickness HorizontalThin = new Thickness(PrimitiveThin, PrimitiveNone, PrimitiveThin, PrimitiveNone);

	public static readonly Thickness HorizontalThick = new Thickness(PrimitiveThick, PrimitiveNone, PrimitiveThick, PrimitiveNone);

	public static readonly Thickness HorizontalThicker = new Thickness(PrimitiveThicker, PrimitiveNone, PrimitiveThicker, PrimitiveNone);

	public static readonly Thickness HorizontalThickest = new Thickness(PrimitiveThickest, PrimitiveNone, PrimitiveThickest, PrimitiveNone);

	public static readonly Thickness VerticalThin = new Thickness(PrimitiveNone, PrimitiveThin, PrimitiveNone, PrimitiveThin);

	public static readonly Thickness VerticalThick = new Thickness(PrimitiveNone, PrimitiveThick, PrimitiveNone, PrimitiveThick);

	public static readonly Thickness VerticalThicker = new Thickness(PrimitiveNone, PrimitiveThicker, PrimitiveNone, PrimitiveThicker);

	public static readonly Thickness VerticalThickest = new Thickness(PrimitiveNone, PrimitiveThickest, PrimitiveNone, PrimitiveThickest);

	public static readonly Thickness LeftTopRightThin = new Thickness(PrimitiveThin, PrimitiveThin, PrimitiveThin, PrimitiveNone);

	public static readonly Thickness LeftTopRightThick = new Thickness(PrimitiveThick, PrimitiveThick, PrimitiveThick, PrimitiveNone);

	public static readonly Thickness LeftTopRightThicker = new Thickness(PrimitiveThicker, PrimitiveThicker, PrimitiveThicker, PrimitiveNone);

	public static readonly Thickness LeftTopRightThickest = new Thickness(PrimitiveThickest, PrimitiveThickest, PrimitiveThickest, PrimitiveNone);

	public static readonly Thickness TopRightBottomThin = new Thickness(PrimitiveNone, PrimitiveThin, PrimitiveThin, PrimitiveThin);

	public static readonly Thickness TopRightBottomThick = new Thickness(PrimitiveNone, PrimitiveThick, PrimitiveThick, PrimitiveThick);

	public static readonly Thickness TopRightBottomThicker = new Thickness(PrimitiveNone, PrimitiveThicker, PrimitiveThicker, PrimitiveThicker);

	public static readonly Thickness TopRightBottomThickest = new Thickness(PrimitiveNone, PrimitiveThickest, PrimitiveThickest, PrimitiveThickest);

	public static readonly Thickness LeftRightBottomThin = new Thickness(PrimitiveThin, PrimitiveNone, PrimitiveThin, PrimitiveThin);

	public static readonly Thickness LeftRightBottomThick = new Thickness(PrimitiveThick, PrimitiveNone, PrimitiveThick, PrimitiveThick);

	public static readonly Thickness LeftRightBottomThicker = new Thickness(PrimitiveThicker, PrimitiveNone, PrimitiveThicker, PrimitiveThicker);

	public static readonly Thickness LeftRightBottomThickest = new Thickness(PrimitiveThickest, PrimitiveNone, PrimitiveThickest, PrimitiveThickest);

	public static readonly Thickness LeftTopBottomThin = new Thickness(PrimitiveThin, PrimitiveThin, PrimitiveNone, PrimitiveThin);

	public static readonly Thickness LeftTopBottomThick = new Thickness(PrimitiveThick, PrimitiveThick, PrimitiveNone, PrimitiveThick);

	public static readonly Thickness LeftTopBottomThicker = new Thickness(PrimitiveThicker, PrimitiveThicker, PrimitiveNone, PrimitiveThicker);

	public static readonly Thickness LeftTopBottomThickest = new Thickness(PrimitiveThickest, PrimitiveThickest, PrimitiveNone, PrimitiveThickest);
}
