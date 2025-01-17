#define _CRT_SECURE_NO_DEPRECATE
#include <iostream>
#include <vector>
#include <set>
#include <queue>
#include <map>
#include <stack>
#include <algorithm>
#include <numeric>
using namespace std;

int n,m,N;
char a[100][100];
map<vector<int>,int > M;
vector<int> need;

bool connected(vector<int>& cur)
{
	if (cur.size()==1)
		return true;

	for (int i=0;i<N;i++)
	{
		int good=false;
		for (int j=0;j<N;j++)
		if (i!=j)
		{
			int d=abs(cur[i]/100-cur[j]/100)+abs(cur[i]%100-cur[j]%100);
			if (d==1)
			{
				good=true;
				break;
			}
			if (d==0)
				return false;
		}
		if (!good)
			return false;
	}
	return true;
}

queue<vector<int> > q;
int T;

bool valid(int X)
{
	return (a[X/100][X%100]!='#');
}

bool add_queue(vector<int> next,int hid)
{
	if (connected(next))
	{
		sort(next.begin(),next.end());
		if (!M.count(next))
		{
			q.push(next);
			M[next]=hid+1;
			if (next==need)
			{
				printf("Case #%d: %d\n",T,hid+1);
				return true;
			}
		}
	}
	return false;
}

bool add_move(vector<int> next,int hid)
{
	sort(next.begin(),next.end());
	if (!connected(next))
	{
		for (int i=0;i<N;i++)
		{
			if (!binary_search(next.begin(),next.end(),next[i]+100)&&!binary_search(next.begin(),next.end(),next[i]-100))
			{
				int x=next[i]/100;
				int y=next[i]%100;

				if (x<=0||x>=n-1)
					continue;
				if (a[x+1][y]=='#'||a[x-1][y]=='#')
					continue;

				next[i]+=100;
				if (add_queue(next,hid+1))
					return true;
				next[i]-=200;
				if (add_queue(next,hid+1))
					return true;
				next[i]+=100;
			}
		}

		for (int i=0;i<N;i++)
		{
			if (!binary_search(next.begin(),next.end(),next[i]+1)&&!binary_search(next.begin(),next.end(),next[i]-1))
			{
				int x=next[i]/100;
				int y=next[i]%100;

				if (y<=0||y>=m-1)
					continue;
				if (a[x][y+1]=='#'||a[x][y-1]=='#')
					continue;

				++next[i];
				if (add_queue(next,hid+1))
					return true;
				next[i]-=2;
				if (add_queue(next,hid+1))
					return true;
				++next[i];
			}
		}
		return false;
	} else
	return add_queue(next,hid);
}

int main()
{
	freopen("in.txt","r",stdin);
	freopen("out.txt","w",stdout);

	int test;
	scanf("%d",&test);
	for (T=1;T<=test;T++)
	{
		scanf("%d%d\n",&n,&m);
		for (int i=0;i<n;i++)
			gets(a[i]);
		M.clear();
		N=0;

		need.clear();
		vector<int> cur;
		for (int i=0;i<n;i++)
		{
			for (int j=0;j<m;j++)
			{
				if (a[i][j]=='x'||a[i][j]=='w')
				{
					need.push_back(i*100+j);
				}
				if (a[i][j]=='o'||a[i][j]=='w')
				{
					cur.push_back(i*100+j);
					++N;
				}
			}
		}
		sort(need.begin(),need.end());

		sort(cur.begin(),cur.end());
		
		if (cur==need)
		{
			printf("Case #%d: 0\n",T);
			continue;
		}

		M[cur]=0;
		while (!q.empty())
			q.pop();
		q.push(cur);

		vector<int> next;
		bool found=false;
		while (!q.empty())
		{
			cur=q.front();
			q.pop();

			int hid=M[cur];
			next=cur;

			for (int i=0;i<N;i++)
			{
				if (!binary_search(cur.begin(),cur.end(),cur[i]+100)&&!binary_search(cur.begin(),cur.end(),cur[i]-100))
				{
					int x=cur[i]/100;
					int y=cur[i]%100;

					if (x<=0||x>=n-1)
						continue;
					if (a[x+1][y]=='#'||a[x-1][y]=='#')
						continue;

					next[i]+=100;
					
					if (add_move(next,hid))
					{
						found=true;
						break;
					}
					next[i]-=200;
					if (add_move(next,hid))
					{
						found=true;
						break;
					}
					next[i]+=100;
				}
			}

			for (int i=0;i<N;i++)
			{
				if (!binary_search(cur.begin(),cur.end(),cur[i]+1)&&!binary_search(cur.begin(),cur.end(),cur[i]-1))
				{
					int x=cur[i]/100;
					int y=cur[i]%100;
					if (y<=0||y>=m-1)
						continue;
					if (a[x][y+1]=='#'||a[x][y-1]=='#')
						continue;

					++next[i];
					if (add_move(next,hid))
					{
						found=true;
						break;
					}
					next[i]-=2;
					if (add_move(next,hid))
					{
						found=true;
						break;
					}
					++next[i];
				}
			}


			if (found)
				break;
		}

		if (!found)
			printf("Case #%d: -1\n",T);
	}
	return 0;
}