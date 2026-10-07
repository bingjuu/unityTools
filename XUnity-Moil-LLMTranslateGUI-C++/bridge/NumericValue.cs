using System;
using System.Globalization;
using System.Linq;

namespace UnityTools
{
    internal static class NumericValue
    {
        public static decimal ParseDecimalExact(string text)
        {
            if (String.IsNullOrWhiteSpace(text)) throw new FormatException("invalid-decimal");
            var raw = text.Trim();
            var mantissa = raw;
            var exponent = 0;
            var exponentIndex = raw.IndexOfAny(new[] { 'e', 'E' });
            if (exponentIndex >= 0)
            {
                mantissa = raw.Substring(0, exponentIndex);
                if (!Int32.TryParse(raw.Substring(exponentIndex + 1), NumberStyles.AllowLeadingSign, CultureInfo.InvariantCulture, out exponent))
                    throw new FormatException("invalid-decimal");
            }
            if (mantissa.StartsWith("+") || mantissa.StartsWith("-")) mantissa = mantissa.Substring(1);
            var dot = mantissa.IndexOf('.');
            var fractional = dot < 0 ? 0 : mantissa.Length - dot - 1;
            var digits = dot < 0 ? mantissa : mantissa.Remove(dot, 1);
            if (digits.Length == 0 || digits.Any(c => c < '0' || c > '9')) throw new FormatException("invalid-decimal");
            digits = digits.TrimStart('0');
            if (digits.Length == 0) return 0m;
            long scale = (long)fractional - exponent;
            while (scale > 0 && digits.EndsWith("0", StringComparison.Ordinal))
            {
                digits = digits.Substring(0, digits.Length - 1);
                --scale;
            }
            if (scale < 0)
            {
                if (digits.Length - scale > 29) throw new OverflowException("decimal-overflow");
                digits += new string('0', (int)-scale);
                scale = 0;
            }
            const string maximumCoefficient = "79228162514264337593543950335";
            if (scale > 28 || digits.Length > maximumCoefficient.Length
                || (digits.Length == maximumCoefficient.Length && String.CompareOrdinal(digits, maximumCoefficient) > 0))
                throw new OverflowException("decimal-precision-loss");
            decimal result;
            if (!Decimal.TryParse(raw, NumberStyles.Float, CultureInfo.InvariantCulture, out result))
                throw new OverflowException("decimal-overflow");
            return result;
        }
    }
}
