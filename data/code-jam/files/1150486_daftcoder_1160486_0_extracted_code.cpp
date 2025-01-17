#include <cstdio>
#include <iostream>
#include <algorithm>
#include <vector>
#include <set>
#include <map>
#include <string>
#include <cmath>
#include <ctime>
#include <string.h>
#include <memory.h>
using namespace std;
#define              FOR(i,a,b)  for(int i=int(a),_b=int(b);i<_b;++i)
#define                       X  first
#define                       Y  second
#define                 MP(a,b)  make_pair(a,b)
typedef               long long  ll;
typedef       pair < int, int >  pii;
typedef         pair < ll, ll >  pll;
typedef pair < double, double >  pdd;
typedef          vector < int >  vi;
typedef           vector < ll >  vll;
typedef             set < int >  si;
typedef              set < ll >  sll;
inline double sqr( double x ) { return x*x; } 

int tests, n, m, d;

char c[501][501];


//ll gcd( ll a, ll b )
//{
//   if( a > b )
//      swap(a,b);
//   if( !a )
//      return b;
//   return gcd( a, b % a );
//}
//
//ll lcm( ll a, ll b )
//{
//   return a*b/gcd(a,b);
//}

int main()
{
   freopen("in.txt","r",stdin);
#ifndef _DEBUG
   freopen("out.txt","w",stdout);
#endif
   cin >> tests;
   FOR(test,0,tests)
   {
      cin >> n >> m >> d;
      gets(c[0]);
      FOR(i,0,n)
         gets(c[i]);
      int ans = 0;
      for(int l = min(n,m); l >= 3; --l)
         if( ans )
            break;
         else
            FOR(i,0,n-l+1) FOR(j,0,m-l+1)
            {
               double cx = 0, cy = 0;
               double t = l/2.0;
               FOR(x,0,l) FOR(y,0,l)
               {
                  if(( x == 0 || x == l-1) && (y == 0 || y == l-1 ))
                     continue;
                  cx += ( x+0.5 - t )*( d + c[i+x][j+y] - '0' );
                  cy += ( y+0.5 - t )*( d + c[i+x][j+y] - '0' );
               }
               
               if( fabs(cx) < 1e-9 && fabs(cy) < 1e-9)
                  ans = l;
            }



      printf("Case #%d: ", test+1);
      if( ans ) 
         cout << ans << endl;
      else
         cout << "IMPOSSIBLE\n";
   }
   return 0;
}
