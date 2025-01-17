#include <cstdio>
#include <iostream>
#include <cstring>
using namespace std;

const int MOD=100003;

long long C[505][505];
long long dp[505][505];
int sum[505];
int T,N,cases=1;

int main()
{
	freopen("d://C-large.in","r",stdin);
	freopen("d://2.txt","w",stdout);
	C[0][0]=C[1][0]=C[1][1]=1LL;
	for(int i=2;i<=500;i++)
	{
		C[i][0]=1LL;
		for(int j=1;j<=i;j++)
			C[i][j]=(C[i-1][j]+C[i-1][j-1])%MOD;
	}
	memset(dp,0,sizeof(dp));
	dp[2][1]=1LL;
	for(int n=3;n<=500;n++)
	{
		dp[n][1]=1LL;
		for(int k=2;k<n;k++)
		{
			dp[n][k]=0;
			for(int i=1;i<k;i++)
				dp[n][k]=(dp[n][k]+dp[k][i]*C[n-k-1][k-i-1])%MOD;
		}
	}
	for(int n=2;n<=500;n++)
	{
		sum[n]=0;
		for(int i=1;i<n;i++)
			sum[n]=(sum[n]+dp[n][i])%MOD;
	}
	scanf("%d",&T);
	while(T--)
	{
		scanf("%d",&N);
		printf("Case #%d: %d\n",cases++,sum[N]);
	}
	return 0;
}