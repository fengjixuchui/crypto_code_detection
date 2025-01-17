#include <bits/stdc++.h>

#define FOR(i,a,b) for(int i=(a);i<(b);++i)
#define FORD(i, a, b) for(int i = (a); i >= (b); --i)
#define VAR(v, i) __typeof(i) v=(i)
#define FORE(i, c) for(VAR(i, (c).begin()); i != (c).end(); ++i)
#define all(v) (v).begin(),(v).end()

#define PII pair<int,int>
#define mp make_pair
#define st first
#define nd second
#define pb push_back
#define lint long long int
#define VI vector<int>

#define debug(x) {cerr <<#x <<" = " <<x <<endl; }
#define debug2(x,y) {cerr <<#x <<" = " <<x << ", "<<#y<<" = "<< y <<endl; } 
#define debug3(x,y,z) {cerr <<#x <<" = " <<x << ", "<<#y<<" = "<< y << ", " << #z << " = " << z <<endl; } 
#define debugv(x) {{cerr <<#x <<" = "; FORE(itt, (x)) cerr <<*itt <<", "; cerr <<endl; }}
#define debugt(t,n) {{cerr <<#t <<" = "; FOR(it,0,(n)) cerr <<t[it] <<", "; cerr <<endl; }}

#define make( x) int (x); scanf("%d",&(x));
#define make2( x, y) int (x), (y); scanf("%d%d",&(x),&(y));
#define make3(x, y, z) int (x), (y), (z); scanf("%d%d%d",&(x),&(y),&(z));
#define make4(x, y, z, t) int (x), (y), (z), (t); scanf("%d%d%d%d",&(x),&(y),&(z),&(t));
#define makev(v,n) VI (v); FOR(i,0,(n)) { make(a); (v).pb(a);} 
#define IOS ios_base::sync_with_stdio(0)
#define HEAP priority_queue

#define read( x) scanf("%d",&(x));
#define read2( x, y) scanf("%d%d",&(x),&(y));
#define read3(x, y, z) scanf("%d%d%d",&(x),&(y),&(z));
#define read4(x, y, z, t) scanf("%d%d%d%d",&(x),&(y),&(z),&(t));
#define readv(v,n) FOR(i,0,(n)) { make(a); (v).pb(a);}


using namespace std;
const int max_n = 1e5+5;

int n, l;
char b[205];
char s[105][205];

void solve() {
	read2(n, l);
	FOR(i,0,n) scanf("%s", s[i]);
	scanf("%s", b);
	bool pocz = false, kon = false;
	FOR(i,0,n) if(s[i][0]=='1') pocz = true;
	FOR(i,0,n) if(s[i][l-1]=='1') kon = true;
	if (pocz && kon) printf("IMPOSSIBLE\n");
	else {
		if (l==1) {
			printf("0\n?\n");
			return;
		}
		if (!pocz) {
			FOR(i,0,l-1) putchar('?'); 
			putchar(' ');
			putchar('?');
			FOR(i,0,l) {
				putchar('1');
				putchar('0');
			}
			printf("\n");
		} else {
			FOR(i,0,l-2) putchar('?');
			printf("0? ");
			FOR(i,0,l) {
				putchar('1');
				putchar('0');
			}
			putchar('0');
			putchar('?');
			printf("\n");
		}
	}
}


int main() {
	make(tt);
	FOR(test,1,tt+1) {
		printf("Case #%d: ",test);
		solve();
	}
}

	
