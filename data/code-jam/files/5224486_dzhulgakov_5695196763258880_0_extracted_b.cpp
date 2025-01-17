#define _CRT_SECURE_NO_WARNINGS
#pragma comment(linker,"/STACK:32000000")
#include <algorithm>
#include <numeric>
#include <string>
#include <list>
#include <cstring>
#include <set>
#include <map>
#include <vector>
#include <queue>
#include <iostream>
#include <iterator>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <sstream>
#include <cassert>
#include <unordered_map>

using namespace std;

#define REP(i,n) for (int i=0,_n=(n); i < _n; i++)
#define REPD(i,n) for (int i=(n)-1; i >= 0; i--)
#define FOR(i,a,b) for (int _b=(b), i=(a); i <= _b; i++)
#define FORD(i,a,b) for(int i=(a),_b=(b);i>=_b;i--)
#define ALL(c) (c).begin(), (c).end()
#define SORT(c) sort(ALL(c))

#define CLEAR(x) memset(x,0,sizeof x);
#define CLEARA(x) memset(&x,0,sizeof x);
#define FILL(x,v) memset(x,v,sizeof x);
#define FILLA(x,v) memset(&x,v,sizeof x);

#define VAR(a,b) __typeof(b) a=(b)
#define FOREACH(it,c) for(VAR(it,(c).begin());it!=(c).end();++it)

#define REVERSE(c) reverse(ALL(c))
#define UNIQUE(c) SORT(c),(c).resize(unique(ALL(c))-(c).begin())
#define INF 0x7fffffff
#define X first
#define Y second
#define pb push_back
#define SZ(c) (int)(c).size()
#define MP make_pair
#define eps 1.0e-11
const double pi = acos(-1.0);

typedef pair<int, int> PII;
typedef vector<PII> VPII;
typedef vector<int> VI;
typedef vector<VI> VVI;
typedef long long LL;

#define MOD 1000000007
#define N 50

LL fact[N],ifact[N];

template<class T> inline T gcd(T a,T b) {
	if (a<0)a=-a; if (b<0)b=-b; if (a<b)swap(a,b);
	while (b) { T t = b; b=a%b; a=t; } return a;
}
template <typename T> T euclide(T a,T b,T &x,T &y) {
	if (a<0) { T g = euclide(-a,b,x,y); x=-x; return g; }
	if (b<0) { T g = euclide(a,-b,x,y); y=-y; return g; }
	if (a<b) return euclide(b,a,y,x);
	if (!b) { x = 1; y = 0; return a; }
	T x1,y1;
	T g = euclide(b,a%b,x1,y1);
	x = y1;
	y = x1 - (a/b)*y1;
	// in oder to avoid overflow:
	//	T d = x/b; x -= d*b; y += d*a;
	return g;
}
template<class T> inline T inverse(T a,T n)
{
	T x,y; euclide(a,n,x,y); return ((x%n)+n)%n;
}

LL cnk(int n, int k)
{
	return ((fact[n]*ifact[k])%MOD*ifact[n-k])%MOD;
}

LL perm(int n)
{
	LL r = 0;
	REP(i,n+1)
	{
		r += MOD + (i%2?-1:1)*(cnk(n,i)*fact[n-i])%MOD;
		r %= MOD;
	}
	return r%MOD;
}

LL solvefix(int n,int x)
{
	LL r = (cnk(n,x)*cnk(n,x))%MOD*fact[x];
	r %= MOD;
	r *= (perm(n-x)*fact[n-x])%MOD;
	return r%MOD;
}

LL solve(int n,int x)
{
	LL r = 0;
	FOR(i,x,n)
		r += solvefix(n,i);
	return r%MOD;
}

int main(int argc, char **argv)
{
	string FN = "B-small-attempt0";
	if (argc>1) FN = string(argv[1]);
	int shift = 0;
	if (argc>2) sscanf(argv[2],"%d",&shift);
	freopen((FN+".in").c_str(),"r",stdin);
	freopen((FN+".out").c_str(),"w",stdout);


	fact[0]=1;
	REP(i,N) if (i) fact[i]=(fact[i-1]*i)%MOD;
	REP(i,N) ifact[i] = inverse<LL>(fact[i],MOD);

	//REP(i,20) REP(j,i+1) fprintf(stderr,"%d %d - %d\n",i,j,(int)solve(i,j)%MOD);

	int tests;
	scanf("%d",&tests);
	for (int test = 1; test<=tests; test++)
	{
		fprintf(stderr,"=== %s : %d\n", FN.c_str(), test+shift);
		printf("Case #%d: ",test+shift);
		////////////////////////////////////////////////////////////
		int n,x;
		scanf("%d%d",&n,&x);
		printf("%d\n",solve(n,x)%MOD);
	}
	fprintf(stderr,"=== %s DONE\n", FN.c_str());
	return 0;
}