//	GCJ 2010 Round 3 (C)

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <cassert>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#include <queue>
#include <set>
#include <map>
#include <utility>
#include <numeric>
#include <algorithm>
#include <bitset>
#include <complex>

using namespace std;

typedef unsigned uint;
typedef long long Int;
typedef vector<int> vint;
typedef vector<string> vstr;
typedef pair<int,int> pint;
#define mp make_pair

template<class T> void pv(T a, T b) { for (T i = a; i != b; ++i) cout << *i << " "; cout << endl; }
template<class T> void pvp(T a, T b) { for (T i = a; i != b; ++i) cout << "(" << i->first << ", " << i->second << ") "; cout << endl; }
void chmin(int &t, int f) { if (t > f) t = f; }
void chmax(int &t, int f) { if (t < f) t = f; }
void chmin(Int &t, Int f) { if (t > f) t = f; }
void chmax(Int &t, Int f) { if (t < f) t = f; }
void chmin(double &t, double f) { if (t > f) t = f; }
void chmax(double &t, double f) { if (t < f) t = f; }
int in_c() { int c; for (; (c = getchar()) <= ' '; ) { if (!~c) throw ~0; } return c; }
int in() { int x = 0, c; for (; (uint)((c = getchar()) - '0') >= 10; ) { if (c == '-') return -in(); if (!~c) throw ~0; } do { x = (x << 3) + (x << 1) + (c - '0'); } while ((uint)((c = getchar()) - '0') < 10); return x; }
Int In() { Int x = 0, c; for (; (uint)((c = getchar()) - '0') >= 10; ) { if (c == '-') return -In(); if (!~c) throw ~0; } do { x = (x << 3) + (x << 1) + (c - '0'); } while ((uint)((c = getchar()) - '0') < 10); return x; }

int C;

int main() {
	int v, p;
	
	for (int TC = in(), tc = 0; ++tc <= TC; ) {
		map<int,int> is;
		set<int> lar;
		C = in();
		for (; C--; ) {
			p = in();
			v = in();
			is[p] += v;
			if (is[p] > 1) {
				lar.insert(p);
			}
		}
		Int ans = 0;
		for (; !lar.empty(); ) {
			p = *lar.begin();
			lar.erase(lar.begin());
//cout<<p<<" "<<is[p]<<endl;
			v = is[p] / 2;
			ans += v;
			is[p] -= v * 2;
			if ((is[p - 1] += v) > 1) lar.insert(p - 1);
			if ((is[p + 1] += v) > 1) lar.insert(p + 1);
		}
		printf("Case #%d: %lld\n", tc, ans);
	}
	
	return 0;
}

