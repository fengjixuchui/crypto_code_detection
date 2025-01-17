#define _USE_MATH_DEFINES
#define _CRT_SECURE_NO_DEPRECATE

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <ctime>
#include <cassert>
#include <map>
#include <set>
#include <iostream>
#include <memory>
#include <string>
#include <vector>
#include <algorithm>
#include <functional>
#include <sstream>
#include <stack>
#include <queue>
#include <numeric>
#include <iterator>
#include <bitset>

using namespace std;

typedef long long ll;
typedef unsigned long long ull;
typedef pair<int, int> Pii;
typedef pair<ll, ll> Pll;

#define FOR(i,n) for(int i = 0; i < (n); i++)
#define sz(c) ((int)(c).size())
#define ten(x) ((int)1e##x)

#pragma comment(linker,"/STACK:36777216")

template<class T> void chmax(T& l, const T r){ l = max(l, r); }
template<class T> void chmin(T& l, const T r){ l = min(l, r); }

#ifdef _WIN32
#   define mygc(c) (c)=getchar()
#   define mypc(c) putchar(c)
#else
#   define mygc(c) (c)=getchar_unlocked()
#   define mypc(c) putchar_unlocked(c)
#endif

void reader(int& x) { int k, m = 0; x = 0; for (;;) { mygc(k); if (k == '-') { m = 1; break; }if ('0' <= k&&k <= '9') { x = k - '0'; break; } }for (;;) { mygc(k); if (k<'0' || k>'9')break; x = x * 10 + k - '0'; }if (m) x = -x; }
void reader(ll& x) { int k, m = 0; x = 0; for (;;) { mygc(k); if (k == '-') { m = 1; break; }if ('0' <= k&&k <= '9') { x = k - '0'; break; } }for (;;) { mygc(k); if (k<'0' || k>'9')break; x = x * 10 + k - '0'; }if (m) x = -x; }
int reader(char c[]) { int i, s = 0; for (;;) { mygc(i); if (i != ' '&&i != '\n'&&i != '\r'&&i != '\t'&&i != EOF) break; }c[s++] = i; for (;;) { mygc(i); if (i == ' ' || i == '\n' || i == '\r' || i == '\t' || i == EOF) break; c[s++] = i; }c[s] = '\0'; return s; }
template <class T, class S> void reader(T& x, S& y) { reader(x); reader(y); }
template <class T, class S, class U> void reader(T& x, S& y, U& z) { reader(x); reader(y); reader(z); }
template <class T, class S, class U, class V> void reader(T& x, S& y, U& z, V & w) { reader(x); reader(y); reader(z); reader(w); }

void writer(int x, char c) { int s = 0, m = 0; char f[10]; if (x<0)m = 1, x = -x; while (x)f[s++] = x % 10, x /= 10; if (!s)f[s++] = 0; if (m)mypc('-'); while (s--)mypc(f[s] + '0'); mypc(c); }
void writer(ll x, char c) { int s = 0, m = 0; char f[20]; if (x<0)m = 1, x = -x; while (x)f[s++] = x % 10, x /= 10; if (!s)f[s++] = 0; if (m)mypc('-'); while (s--)mypc(f[s] + '0'); mypc(c); }
void writer(const char c[]) { int i; for (i = 0; c[i] != '\0'; i++)mypc(c[i]); }
void writer(const char x[], char c) { int i; for (i = 0; x[i] != '\0'; i++)mypc(x[i]); mypc(c); }
template<class T> void writerLn(T x) { writer(x, '\n'); }
template<class T, class S> void writerLn(T x, S y) { writer(x, ' '); writer(y, '\n'); }
template<class T, class S, class U> void writerLn(T x, S y, U z) { writer(x, ' '); writer(y, ' '); writer(z, '\n'); }
template<class T> void writerArr(T x[], int n) { if (!n) { mypc('\n'); return; }FOR(i, n - 1)writer(x[i], ' '); writer(x[n - 1], '\n'); }

template<class T> T gcd(T a, T b) { return b ? gcd(b, a % b) : a; }
template<class T> T lcm(T a, T b) { return a / gcd(a, b) * b; }
template<class T> T extgcd(T a, T b, T& x, T& y){ for (T u = y = 1, v = x = 0; a;) { T q = b / a; swap(x -= q * u, u); swap(y -= q * v, v); swap(b -= q * a, a); } return b; }
template<class T> T mod_inv(T a, T m){ T x, y; extgcd(a, m, x, y); return (m + x % m) % m; }
template<class T> T CRT(T r1, T m1, T r2, T m2) { T a1, a2; extgcd(m1, m2, a1, a2); T ret = (m1*a1*r2 + m2*a2*r1) % (m1*m2); return ret < 0 ? ret + m1 * m2 : ret; }


vector<ll> solve(map<ll,ll> vp){
	const int n = sz(vp);
	ll sum = 0;
	for (auto kv : vp) sum += kv.second;

	vector<ll> ans;
	while (sum != 1) {
		sum /= 2;
		if (sz(vp) == 1) {
			ans.push_back(0);
			continue;
		}
		auto it = vp.begin();
		ll diff = next(it)->first - it->first;

		map<ll, ll> nxt;
		for (auto& kv : vp) {
			if (kv.second == 0) continue;
			nxt[kv.first + diff] = kv.second;
			vp[kv.first + diff] -= kv.second;
		}

		bool mns = true;
		for (auto kv : vp) if (kv.first == 0 && kv.second != 0) mns = false;

		if (mns) diff = -diff;
		ans.push_back(diff);

		if (diff < 0) {
			swap(nxt, vp);
		}

		nxt = vp;
		vp.clear();
		for (auto kv : nxt) if (kv.second) vp[kv.first] = kv.second;
	}

	sort(ans.begin(), ans.end());
	return ans;

}

void test(){
	int cnt = 0;
	while (true) {
		cout << cnt << endl;
		cnt++;
		vector<ll> vals;
		FOR(i, 3){
			vals.push_back(rand() % 10 - 5);
		}
		sort(vals.begin(), vals.end());

		printf("ans is ");
		FOR(i, sz(vals)){
			printf("%lld%c", vals[i], i == sz(vals) - 1 ? '\n' : ' ');
		}

		map<ll, ll> mp;
		FOR(i, 1 << sz(vals)){
			ll cur = 0;
			FOR(j, sz(vals)) if(i & (1<<j)) cur += vals[j];
			mp[cur]++;
		}

		auto ans = solve(mp);
		FOR(i, 1 << sz(vals)){
			ll cur = 0;
			FOR(j, sz(vals)) if (i & (1 << j)) cur += ans[j];
			mp[cur]--;
		}
		for (auto kv : mp) {
			if (kv.second != 0) {
				cout << "?";
			}
		}
		FOR(i, sz(ans)){
			printf("%lld%c", ans[i], i == sz(ans) - 1 ? '\n' : ' ');
		}
	}
}

int main(){

	// test();
	int t; cin >> t;
	FOR(i, t){
		printf("Case #%d: ", i + 1);
		int n;
		reader(n);

		vector<Pll> vp(n);
		FOR(i, n) reader(vp[i].first);
		FOR(i, n) reader(vp[i].second);

		map<ll, ll> _vp(vp.begin(), vp.end());

		auto ans = solve(_vp);
		FOR(i, sz(ans)){
			printf("%lld%c", ans[i], i == sz(ans) - 1 ? '\n' : ' ');
		}
	}
}