#define _CRT_SECURE_NO_DEPRECATE
#define _SECURE_SCL 0

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>
#include <iostream>
#include <queue>
#include <deque>
#include <stack>
#include <list>
#include <cctype>
#include <sstream>
#include <cassert>
#include <bitset>
#include <memory.h>

using namespace std;

#pragma comment(linker, "/STACK:200000000")

typedef long long int64;

#define forn(i, n) for(int i = 0; i < (int)(n); i++)
#define ford(i, n) for(int i = (int)(n) - 1; i >= 0; i--)
#define fore(i, a, n) for(int i = (int)(a); i < (int)(n); i++)
#define pb push_back
#define mp make_pair
#define fs first
#define sc second
#define last(a) (int(a.size()) - 1)
#define all(a) a.begin(), a.end()

const double EPS = 1E-9;
const int INF = 1000000000;
const int64 INF64 = (int64) 1E18;
const double PI = 3.1415926535897932384626433832795;

const int MOD = 1000003;
char buf[1100000];
int64 z2[110][110][110][110], z[110][110];

int64 rec2(int len1, int len2, int place, int need) {
	if (need < 0)
		return 0;
	if (len1 == 0)
		return len2 == 0 && need == 0 ? 1 : 0;
	if (z2[len1][len2][place][need] != -1)
		return z2[len1][len2][place][need];

	int64 res = 0;
	int nplace = place, nneed = need - 2;
	if (place) {
		nplace = place - 1;
		nneed = need - 1;
	}
		
	res = rec2(len1 - 1, len2, nplace, need);
	fore(i, 1, len2 + 1) {
		res += rec2(len1 - 1, len2 - i, nplace, nneed);
		res %= MOD;
	}

	return z2[len1][len2][place][need] = res;
}

int64 solve2(int len1, int len2, int place, int need) {
	return rec2(len1, len2, place, need);
}

void solve() {
	scanf("%s", buf);

	string s = buf;
	/*
	s = "";
	forn(i, 100)
		s += char('a' + rand() % 26);
	*/
	int r = 1;
	forn(i, s.size())
		if (i && s[i - 1] != s[i])
			r++;

	sort(all(s));

	vector<pair<char, int> > a;
	forn(i, s.size()) {
		int j = i;
		while (j < (int)s.size() && s[i] == s[j])
			j++;

		a.pb(mp(s[i], j - i));
		i = j - 1;
	}

	memset(z, 0, sizeof(z));
	z[0][1] = 1;

	memset(z2, -1, sizeof(z2));

	int len = a[0].sc;
	fore(i, 1, a.size()) {
		fore(j, 1, r + 1)
			if (z[i - 1][j])
				fore(t, j + 1, r + 1) {
					z[i][t] += z[i - 1][j] * solve2(len + 1, a[i].sc, j + 1, t - j) % MOD;
					z[i][t] %= MOD;
				}

		len += a[i].sc;
	}

	int64 ans = z[(int)a.size() - 1][r];
	cout << ans << endl;
}

int main() {
#ifdef RADs_project
    freopen("input.txt", "rt", stdin);
    freopen("output.txt", "wt", stdout);
#endif	
	/*
	forn(ii, 100) {
		cerr << ii << ' ' << clock() << endl;
		solve();
	}

	cerr << clock() << endl;
	return 0;
	*/
	int tt;
	scanf("%d", &tt);
	forn(ii, tt) {
		cerr << ii << ' ' << clock() << endl;
		printf("Case #%d: ", ii + 1);

		solve();
	}

	cerr << clock() << endl;
	
	return 0;
}