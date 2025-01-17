//問題読解：2時間00分、考察時間：5分、誤読1回

//人i(1 <= i <= D)はPi個のパンケーキを最初持っている。その他無限人の人は0個のパンケーキを持っている。
//各分、次の2つの内どちらか1つの行動をとることができる。
//・パンケーキを持っている人全員が、それぞれの持っているパンケーキを1つ減らす
//・パンケーキを持っている誰か一人(これをAとする), 任意の一人(これをBとする)を選ぶ。この時AがBにパンケーキを任意個渡す。
//全てのパンケーキがなくなるまでにかかる最小の時間は？
//1 <= D <= 1000, 1 <= Pi <= 1000
//small: 1 <= D <= 6, 1 <= Pi <= 9

//考察(複数人OKな操作と、1to1を1回制限で行う操作があるので、独立に考えるっぽいことは難しいっぽーい)
//・パンケーキを渡す系のやつ…パンケーキを食べる前に全部やってしまった方が良い。

//あ、独立に考えられるっぽい。

//・最短時間は…パンケーキを渡す系の奴をやった回数 + パンケーキを渡す系の奴を全て行った後におけるPMAX( PMAX is 最大個パンケーキを所有している人の持つパンケーキの個数)
//パンケーキを渡す回数を決めたときの、PMAXを最小化する問題になった。これ(分ける戦略)を考える。

//・パンケーキを受け取る人：Emptyな人が良いね。
//・パンケーキを渡す人：貪欲に今PMAX個パンケーキを持つ人が良さそう。
//・パンケーキの分け方：不明。1〜Pi-1個について全て試そう。と思ったが、これではLargeが解けないので、高速化します。
//・パンケーキを分けるべきかどうか…パンケーキを渡す回数に達していなければ分ける！そういうルールにしているので簡単！！

#include<iostream>
#include<string>
#include<algorithm>
#include<functional>
#include<vector>
#include<stack>
#include<queue>
#include<set>
#include<map>
#include<cstdio>
#include<cstdlib>
#include<cstring>
#include<cmath>
using namespace std;

void Write(char *name, int T);

int t;
char out[100][1002];

int dp[1001][1001];	//dp[n][k] = nをk以下(の要素)に分割するのに必要な「分ける」操作の回数の最小値を持つ

//nをk以下にするのに必要な「分ける」操作の回数の最小値を返す
int dfs(int n, int k, int dep=0) {
	
	if ( n <= k )
		return 0;
	if ( dp[n][k] != -1 )
		return dp[n][k];

	int ret = 114514;
	for( int i = 1; i < n; i++ ) {
		ret = min(ret, dfs(i, k, dep+1) + dfs(n-i, k, dep+1) + 1 );
	}
	dp[n][k] = ret;	//DP()関数の更新順より、ここでdp[n][k]が確定する。
	return ret;
}

void DP() {
	int i, j;
	for( i = 0; i < 1001; i++ ) {
		for( j = 0; j < 1001; j++ ) {
			dp[i][j] = -1;
		}
	}
	dp[0][0] = 0;
	for( i = 0; i < 1001; i++ ) {
		for( j = 0; j < 1001; j++ ) {
			dp[i][j] = dfs(i, j);
		}
	}
}

signed main() {
	int i, j;
	
	DP();
	
	cin >> t;
	for( i = 0; i < t; i++ ) {
		int d, p[1000];
		cin >> d;
		for( j = 0; j < d; j++ )
			cin >> p[j];
		
		int ans = 114514;
		for( int num = 0; num < 1001; num++ ) {
			int sum = 0;
			for( j = 0; j < d; j++ )
				sum += dp[ p[j] ][num];
			ans = min(ans, sum + num);
		}
		
		sprintf(out[i], "Case #%d: %d\n", i+1, ans);
	}
	
	Write("C:\\Users\\Kawakami\\kyopro\\GCJ\\2015\\qual\\out-b-large.txt", t);
	return 0;
}

//outの中を出力(改行なし)
void Write(char *name, int T) {
	FILE *fp = fopen(name, "w");
	for(int i = 0; i < T; i++ ) {
		fprintf(fp, "%s", out[i]);
		fprintf(stdout, "%s", out[i]);
	}
	fclose(fp);
}