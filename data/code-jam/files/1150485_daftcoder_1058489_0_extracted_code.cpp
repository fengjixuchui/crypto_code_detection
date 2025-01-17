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

int n,tests,d;

int main()
{
   freopen("in.txt","r",stdin);
#ifndef _DEBUG
   freopen("out.txt","w",stdout);
#endif
   cin >> tests;
   vector <int> p;
   vector <double> r;
   FOR(test,0,tests)
   {
      cin >> n >> d;
      p.clear();
      r.clear();

      FOR(i,0,n)
      {
         int c, k;
         cin >> c >> k;
         FOR(j,0,k)
            p.push_back(c);
      }
      sort(p.begin(),p.end());
      n = p.size();
      r.resize(n);
      double x = 0, y = 1e9;
      while( y - x > 1e-8 )
      {
         double t = y+x;
         t /= 2;
         r[0] = p[0] - t;
         FOR(i,1,n)
         {
            double ps = max( r[i-1]+d, p[i]-t );
            r[i] = ps;
         }
         bool fail = false;
         FOR(i,0,n)
            if( r[i] - p[i] > t )
               fail = true;
         if( fail )
            x = t;
         else
            y = t;
      }

      cout << "Case #" << test+1;
      printf(": %.7lf\n",x);
   }
   return 0;
}