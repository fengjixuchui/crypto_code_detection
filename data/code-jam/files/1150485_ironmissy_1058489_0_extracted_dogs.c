#include <stdio.h>
#include <stdlib.h>


#define infile "dogs.in"
#define outfile "dogs.out"

#define N 110

#define MIN(a,b) ((a)<(b)?(a):(b))

int main() {
	FILE *fin, *fout;
	int t, i, j, n, k, p[N], v[N];
	int sum, ok, ve, d;
	double start, finish, ti, z, pos;

	fin = freopen(infile, "r", stdin);
	fout = freopen(outfile, "w", stdout);
	
	scanf("%d", &t);
	for (i = 0; i < t; i++) {
		sum = 0;
		scanf("%d%d",  &n, &d);
		for (j = 0; j < n; j++) {
			scanf("%d%d",  &p[j], &v[j]);
			sum += v[j];
		}
	
		start = 0;
		finish = sum * d;
		for (k = 0; k < 65; k++) {
		
			ti = (start + finish) / 2.0;
			j = n - 1;
			
			pos = p[n - 1] + ti;
			ve = v[j] - 1;
			ok = 1;

			while (ok && j >= 0) {
				if (ve == 0) {
					j--;
					if (j < 0)
						break;
					ve = v[j];
				}

				z = pos - d;

				if (p[j] - ti > z) {
					ok = 0;
					break;
				}

				pos = MIN(z , p[j] + ti);
				ve--;
			}

			if (ok)
				finish = ti;
			else
				start = ti;
		}

		printf("Case #%d: %.7lf\n", i + 1, ti);
	}

	return 0;
}
