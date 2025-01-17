/*
    Rezwan_4029 , AUST
*/

#include <bits/stdc++.h>

#define pb push_back
#define all(x) x.begin(),x.end()
#define ms(a,v) memset(a,v,sizeof a)
#define II ({int a; scanf("%d", &a); a;})
#define LL ({Long a; scanf("%lld", &a); a;})
#define DD ({double a; scanf("%lf", &a); a;})
#define ff first
#define ss second
#define mp make_pair
#define gc getchar
#define EPS 1e-10
#define pi 3.1415926535897932384626433832795
using namespace std;

#define FI freopen ("in.txt", "r", stdin)
#define FO freopen ("out.txt", "w", stdout)

typedef long long Long;
typedef unsigned long long ull;
typedef vector<int> vi ;
typedef set<int> si;
typedef vector<Long>vl;
typedef pair<int,int>pii;
typedef pair<string,int>psi;
typedef pair<Long,Long>pll;
typedef pair<double,double>pdd;
typedef vector<pii> vpii;

#define forab(i, a, b)	for (__typeof (b) i = (a) ; i <= b ; ++i)
#define rep(i, n)		forab (i, 0, (n) - 1)
#define For(i, n)		forab (i, 1, n)
#define rofba(i, a, b)	for (__typeof (b)i = (b) ; i >= a ; --i)
#define per(i, n)		rofba (i, 0, (n) - 1)
#define rof(i, n)		rofba (i, 1, n)
#define forstl(i, s)	for (__typeof ((s).end ()) i = (s).begin (); i != (s).end (); ++i)

#define __(args...) {dbg,args; cerr<<endl;}
struct debugger{template<typename T> debugger& operator , (const T& v){cerr<<v<<"\t"; return *this; }}dbg;
#define __1D(a,n) rep(i,n) { if(i) printf(" ") ; cout << a[i] ; }
#define __2D(a,r,c,f) forab(i,f,r-!f){forab(j,f,c-!f){if(j!=f)printf(" ");cout<<a[i][j];}cout<<endl;}

template<class A, class B> ostream &operator<<(ostream& o, const pair<A,B>& p){ return o<<"("<<p.ff<<", "<<p.ss<<")";} //Pair print
template<class T> ostream& operator<<(ostream& o, const vector<T>& v){ o<<"[";forstl(it,v)o<<*it<<", ";return o<<"]";} //Vector print
template<class T> ostream& operator<<(ostream& o, const set<T>& v){ o<<"[";forstl(it,v)o<<*it<<", ";return o<<"]";} //Set print
template<class T> inline void MAX(T &a , T b){ if (a < b ) a = b;}
template<class T> inline void MIN(T &a , T b){ if (a > b ) a = b;}

//Fast Reader
template<class T>inline bool read(T &x){int c=gc();int sgn=1;while(~c&&c<'0'||c>'9'){if(c=='-')sgn=-1;c=gc();}for(x=0;~c&&'0'<=c&&c<='9';c=gc())x=x*10+c-'0';x*=sgn;return ~c;}

//int dx[]={1,0,-1,0};int dy[]={0,1,0,-1}; //4 Direction
int dx[]={1,1,0,-1,-1,-1,0,1};int dy[]={0,1,1,1,0,-1,-1,-1};//8 direction
//int dx[]={2,1,-1,-2,-2,-1,1,2};int dy[]={1,2,2,1,-1,-2,-2,-1};//Knight Direction
//int dx[]={2,1,-1,-2,-1,1};int dy[]={0,1,1,0,-1,-1}; //Hexagonal Direction

bool Ans ;
bool G[55][55];
void solve(int R , int C , int M)
{
    int empty = R * C - M ;
    if( M == 0 ) return ;
    if( empty == 1 )
    {
        rep(i,R)rep(j,C)G[i][j] = true ;
        return ;
    }
    if( R == 1 && C == 1 )
    {
        Ans = false ;
        return ;
    }
    if (R == 1 || C == 1)
    {
        if (empty >= 2)
        {
            per(r,R)
            {
                per(c,C)
                {
                    if (r > 1 || c > 1)
                    {
                        G[r][c] = true;
                        M--;
                        if ( M == 0) return;
                    }
                }
            }
            return;
        }
        else
        {
            Ans = false ;
            return ;
        }
    }

    if (empty >= 4)
    {
        if (C > R)
        {
            if (M != R - 1)
            {
                per(r,R)
                {
                    G[r][C - 1] = true ;
                    M--;
                    if ( M == 0 ) return ;
                }
                solve(R, C - 1, M);
            }
            else
            {
                if (M == 1)
                {
                    G[R - 1][C - 1] = true;
                    return;
                }
                for (int r = R - 1; r >= 2; r--)
                {
                    G[r][C - 1] = true;
                    M--;
                }
                G[R - 1][C - 2] = true;
                return;
            }
        }
        else
        {
            if (M != C - 1)
            {
                for (int c = C - 1; c >= 0; c--)
                {
                    G[R - 1][c] = true;
                    if (--M == 0) return;
                }
                solve(R - 1, C, M);
            }
            else
            {
                if (M == 1)
                {
                    G[R - 1][C - 1] = true;
                    return;
                }
                for (int c = C - 1; c >= 2; c--)
                {
                    G[R - 1][c] = true;
                    M--;
                }
                G[R - 2][C - 1] = true;
                return;
            }
        }
    }
    else
    {
        Ans = false ;
        return ;
    }

}
int R , C , M ;
int main() {
  // FI;FO;
    freopen("C-small-attempt4.in", "r", stdin);
    freopen("C-small-attempt4.out", "w", stdout);
    int T ;
    read(T);
    For(cs,T) {
        ms(G,0);
        cin >> R >> C >> M ;
        printf("Case #%d:\n",cs);
        Ans = true ;
        solve(R,C,M);
        if(  Ans == false ) puts("Impossible");
        else
        {
            rep(i,R)
            {
                rep(j,C)
                {
                    if( i == 0 && j == 0 ) printf("c");
                    else if( G[i][j] ) printf("*");
                    else printf(".");
                }
                puts("");
            }
        }
    }
}
