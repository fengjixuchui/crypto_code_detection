#pragma comment(linker, "/STACK:836777216")

#include <algorithm>
#include <iostream>
#include<stdio.h>
#include <string>
#include<sstream>   
#include<string.h>
#include <vector>
#include <bitset>
#include <cmath>
#include <queue>
#include<stack>
#include <set>
#include <map>
#include<ctime>
#include<memory.h>

using namespace std;

typedef long long ll;
typedef vector<int> vi;
typedef pair<int,int> pii;
typedef pair<double,double> pdd;
typedef pair<int,pii> p3i;
typedef long double ld;
typedef vector<ld> vd;

#define FOR(i,a,b) for (int i(a); i < (b); i++) 
#define REP(i,n) FOR(i,0,n) 
#define UN(v) sort((v).begin(),(v).end()),v.erase(unique(v.begin(),v.end()),v.end())
#define CL(a,b) memset(a,b,sizeof(a))
#define pb push_back
#define SORT(a) sort((a).begin(),(a).end())


int main(){
	freopen("input.txt","r",stdin);
	freopen("output.txt","w",stdout);

	int  TC;
	cin>>TC;
	REP(tc,TC){
		int k,c;
		cin>>k>>c;
		int num=k;
		int sum=k;
		FOR(i,2,c+1){
			int nn = k - sum/i;
			sum += nn*i;
			num += nn;
		}
		printf("Case #%d: %d\n",tc+1,num);
	}

	return 0;
}