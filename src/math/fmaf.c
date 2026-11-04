#include <math.h>
#include <stdint.h>

float fmaf(float x, float y, float z)
{
	double xy = (double)x * y;
	union {double r; uint64_t i;} u = {xy + z};
	int e = u.i>>52 & 0x7ff;
	/* covers |r| > 0x1p-126 halfway cases (may round incorrectly) */
	int halfway = (u.i & 0x1fffffff) == 0x10000000;
	/* covers tiny inexact (may miss uflow) and tiny halfway cases */
	int tiny = e <= 0x3ff-126 && e >= 0x3ff-149;
	if (!halfway && !tiny)
		/* common case, optimization only */
		return (float)u.r;
	if (e != 0x7ff) {
		/* r+t == x*y+z exactly in nearest rounding, otherwise
		 * rounding does not affect t!=0, t<0 and inexact flag. */
		int s = u.i >> 63;
		double t = s == (xy < z) ? xy - u.r + z : z - u.r + xy;
		if (t) {
			/* adjust r toward r+t if r%2==0 (round to odd).
			 * may be needed if r is halfway or exact float,
			 * wrong if r becomes halfway or exact float. */
			u.i -= s ^ (t<0);
			u.i |= 1;
		}
	}
	return (float)u.r;
}
