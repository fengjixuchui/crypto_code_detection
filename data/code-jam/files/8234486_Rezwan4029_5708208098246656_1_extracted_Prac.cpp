/*
 	 rezwan_4029 , AUST
 */

#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <cmath>
#include <cctype>
#include <climits>

#include <iostream>
#include <algorithm>
#include <fstream>
#include <sstream>
#include <utility>
#include <vector>
#include <set>
#include <stack>
#include <queue>
#include <map>

#define pb push_back
#define all(x) x.begin(),x.end()
#define ms(a,v) memset(a,v,sizeof a)
#define II ({int a; read(a); a;})
#define LL ({Long a; read(a); a;})
#define DD ({double a; scanf("%lf", &a); a;})
#define ff first
#define ss second
#define mp make_pair
#define gc getchar
#define EPS 1e-10
#define pi 3.1415926535897932384626433832795
using namespace std;

#define CS printf("Case #%d:",cs)

#define FI freopen ("in.txt", "r", stdin)
#define FO freopen ("out.txt", "w", stdout)

typedef long long Long;
typedef long long int64;
typedef unsigned long long ull;
typedef vector<int> vi;
typedef set<int> si;
typedef vector<Long> vl;
typedef pair<int, int> pii;
typedef pair<string, int> psi;
typedef pair<Long, Long> pll;
typedef pair<double, double> pdd;
typedef vector<pii> vpii;

#define forab(i, a, b)	for (__typeof (b) i = (a) ; i <= b ; ++i)
#define rep(i, n)		forab (i, 0, (n) - 1)
#define For(i, n)		forab (i, 1, n)
#define rofba(i, a, b)	for (__typeof (b)i = (b) ; i >= a ; --i)
#define per(i, n)		rofba (i, 0, (n) - 1)
#define rof(i, n)		rofba (i, 1, n)
#define forstl(i, s)	for (__typeof ((s).end ()) i = (s).begin (); i != (s).end (); ++i)

#define __(args...) {dbg,args; cerr<<endl;}
struct debugger {
	template<typename T> debugger& operator ,(const T& v) {
		cerr << v << "\t";
		return *this;
	}
} dbg;
#define __1D(a,n) rep(i,n) { if(i) printf(" ") ; cout << a[i] ; }
#define __2D(a,r,c,f) forab(i,f,r-!f){forab(j,f,c-!f){if(j!=f)printf(" ");cout<<a[i][j];}cout<<endl;}

template<class A, class B> ostream &operator<<(ostream& o,
		const pair<A, B>& p) {
	return o << "(" << p.ff << ", " << p.ss << ")";
} //Pair print
template<class T> ostream& operator<<(ostream& o, const vector<T>& v) {
	o << "[";
	forstl(it,v)
		o << *it << ", ";
	return o << "]";
} //Vector print
template<class T> ostream& operator<<(ostream& o, const set<T>& v) {
	o << "[";
	forstl(it,v)
		o << *it << ", ";
	return o << "]";
} //Set print
template<class T> inline void MAX(T &a, T b) {
	if (a < b)
		a = b;
}
template<class T> inline void MIN(T &a, T b) {
	if (a > b)
		a = b;
}

//Fast Reader
template<class T> inline bool read(T &x) {
	int c = gc();
	int sgn = 1;
	while (~c && c < '0' || c > '9') {
		if (c == '-')
			sgn = -1;
		c = gc();
	}
	for (x = 0; ~c && '0' <= c && c <= '9'; c = gc())
		x = x * 10 + c - '0';
	x *= sgn;
	return ~c;
}

int dx[] = {};
int dy[] = {};

const int MX = 111 ;
char a[MX][MX];
int n, m, Vis[MX][MX][10];

int solve(){
	ms(Vis,0);

	For(i,n)
		For (j,m)
			if (a[i][j] != '.'){
				Vis[i][j][0] = 1;
				break;
			}


	For(i,n)
		rof(j, m)
			if (a[i][j] != '.'){
				Vis[i][j][1] = 1;
				break;
			}


	For(j, m)
		For(i,n)
			if (a[i][j] != '.'){
				Vis[i][j][2] = 1;
					break;
			}


	For(j, m)
		rof(i, n)
			if( a[i][j] != '.'){
				Vis[i][j][3] = 1;
				break;
			}

	int ret = 0;
	For(i, n){
		For(j, m){
			if ( Vis[i][j][0] and Vis[i][j][1] and
				 Vis[i][j][2] and Vis[i][j][3])
				return -1;

			if ( Vis[i][j][0] and a[i][j] == '<' )	ret++;
			if ( Vis[i][j][1] and a[i][j] == '>' )	ret++;
			if ( Vis[i][j][2] and a[i][j] == '^' )	ret++;
			if ( Vis[i][j][3] and a[i][j] == 'v' )	ret++;
			}
		}
	return ret;

}

int main(){
	FI;
	FO;
	int T = II ;
	For(cs,T){
		n = II , m = II ;
		For(i,n) For(j,m) cin >> a[i][j] ;
		CS;
		int ret = solve();
		if( ret != -1 ) cout << " " << ret << endl;
		else cout << " IMPOSSIBLE" << endl;
	}
}
