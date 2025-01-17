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

ll gcd( ll a, ll b )
{
   if( a < b )
      swap(a,b);
   if( b <= 0 )
      return a;
   return gcd( b, a % b );
}

ll lcm( ll a, ll b )
{
   return a*b/gcd(a,b);
}

int main()
{
   freopen("in.txt","r",stdin);
#ifndef _DEBUG
   freopen("out.txt","w",stdout);
#endif
   cin >> tests;
   FOR(test,0,tests)
   {
      cin >> n;
      int x, y;
      ll v;
      
      v = 1;
      x = 1;
      FOR(i,2,n+1)
      {
         ll nv = lcm( v, i );
         if( nv != v )
            ++x;   
         v = nv;
      }
      y = x;

      v = n;
      x = 1;
      for(int i=n-1; i > 0; --i)
      {
         ll nv = lcm( v, i );
         if( nv != v )
            ++x;   
         v = nv;
      }


      printf("Case #%d: %d\n", test+1,y-x);

   }
   return 0;
}
