#include <cstdio>
#include <iostream>
#include <cstring>
using namespace std;

int T,n,res;
int MOD=100003;
bool vst[100];

void dfs(int u,int depth,int cnt)
{
	if(depth==n)
	{
		cnt++;
		int id=cnt;
		bool f=true;
		while(f)
		{
			if(id==1) break;
			if(!vst[id]) f=false;
			else
			{
				int rank=1;
				for(int i=2;i<id;i++)
					if(vst[i]) rank++;
				id=rank;
			}
		}
		if(f) res=(res+1)%MOD;
		return ;
	}
	dfs(u+1,depth+1,cnt);
	vst[u]=true;
	dfs(u+1,depth+1,cnt+1);
	vst[u]=false;
}

int main()
{
	freopen("d://C-small-attempt0.in","r",stdin);
	freopen("d://2.txt","w",stdout);
	int cases=1;
	scanf("%d",&T);
	while(T--)
	{
		scanf("%d",&n);
		memset(vst,0,sizeof(vst));
		res=0;vst[1]=true;
		dfs(2,2,0);
		printf("Case #%d: %d\n",cases++,res);
	}
	return 0;
}