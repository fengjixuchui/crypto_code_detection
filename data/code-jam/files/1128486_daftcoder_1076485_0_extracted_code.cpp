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
#define             FOR(i,a,b)  for(int i=int(a);i<int(b);++i)
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

ll gcd( ll a, ll b )
{
   if( a > b )
      swap(a,b);
   if( a <= 0 )
      return b;
   return gcd(a,b%a);
}

ll lca( ll a, ll b )
{
   return a * b / gcd(a,b);
}


int main()
{
   freopen("in.txt","r",stdin);
#ifndef _DEBUG
   freopen("out.txt","w",stdout);
#endif
   int tests;
   cin >> tests;

   FOR(test,0,tests)
   {
      int n;
      ll x, y;
      ll a[10000];
      cin >> n >> x >> y;
      FOR(i,0,n)
         cin >> a[i];
      bool sc = false;
      int res = 0;
      FOR(i,x,y+1)
      {
         bool e = false;
         FOR(j,0,n)
            if( a[j] % i && i % a[j] )
            {
               e = true;
               break;
            }
         if( !e )
         {
            sc = true;
            res = i;
            break;
         }
      }
      cout << "Case #" << test+1 << ": ";
      if( sc )
         cout << res << endl;
      else
         cout << "NO" << endl;
   }
   return 0;
}