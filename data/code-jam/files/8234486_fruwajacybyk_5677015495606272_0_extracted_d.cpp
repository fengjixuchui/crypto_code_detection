#include <bits/stdc++.h>

#define FOR(i,a,b) for(int i=(a);i<(b);++i)
#define FORD(i, a, b) for(int i = (a); i >= (b); --i)
#define VAR(v, i) __typeof(i) v=(i)
#define FORE(i, c) for(VAR(i, (c).begin()); i != (c).end(); ++i)
#define all(v) (v).begin(),(v).end()

#define VI vector<int>
#define PII pair<int,int>
#define st first
#define nd second
#define mp make_pair
#define pb push_back
#define lint long long int

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

#define max_n 1000005

int mod = 1e9+7;

void solve(){
	make2(r,c);
	int ans = 0;
	if(c%3!=0){
		if(r%3==1) printf("1\n");
		if(r%3==0) printf("2\n");
		if(r%3==2) printf("1\n");
	}
	else{
		int a[105]; // 3
		int b[105]; // 2, 1
		int c[105]; // 2
		a[0] = 0; a[1] = 1;
		b[0] = 0; b[1] = 1;
		c[0] = 1; c[1] = 0;
		FOR(i,2,105){
			a[i] = (b[i-2]+c[i-2])%mod;
			b[i] = a[i-2]%mod;
			c[i] = a[i-1]%mod;
		}
		int ans = a[r-1];
		ans = (ans+b[r-1])%mod;
		ans = (ans+c[r-1])%mod;
		printf("%d\n",ans);
	}
}


int main(){
	make(t);
	FOR(i,1,t+1){
		printf("Case #%d: ",i);
		solve();
	}
}

