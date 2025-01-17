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

int n,tests;

bool cmp( double a, double b )
{
   return a > b;
}

int main()
{
   freopen("in.txt","r",stdin);
#ifndef _DEBUG
   freopen("out.txt","w",stdout);
#endif
   cin >> tests;

   int l, n, c;
   ll t;
   int _a[1000];
   vector <double> v;

   FOR(test,0,tests)
   {
      
      cin >> l >> t >> n >> c;
      v.clear();
      FOR(read,0,c)
      {
         cin >> _a[read];
      }
      FOR(rr,0,n)
      {
         v.push_back(2*_a[rr%c]);
      }
      int j = 0;
      double res = 0;
      while( t && j < n )
      {
         int q = v[j];
         ll r = t;
         if( q < t )
            r = q;
         res += r;
         v[j] -= r;
         t -= r;
         ++j;
      }
      sort(v.begin(),v.end(),cmp);
      FOR(i,0,l)
      {
         v[i] /= 2.0;
      }
      
      FOR(i,0,n)
      {
         res += v[i];
      }

      cout << "Case #" << test+1 << ": ";
      printf("%.0lf\n",res);

   }
   return 0;
}