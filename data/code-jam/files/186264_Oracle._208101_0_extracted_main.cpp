#define _CRT_SECURE_NO_DEPRECATE
#include <iostream>
#include <map>
#include <string>
#include <queue>
#include <vector>
using namespace std;

const int dx[]={-1,1,0,0};
const int dy[]={0,0,-1,1};

char a[100][100];
bool res[50][50][1500];

int w;
map<int,string> m;

bool valid(int x,int y)
{
	if (x<0||x>=w||y<0||y>=w)
		return false;
	return true;
}

string getstr(char c)
{
	string res;
	res.push_back(c);
	return res;
}

struct struc
{
	int x,y,val;
	string cur;

	struc(int X=0,int Y=0,string Cur="",int Val=0)
	{
		x=X;y=Y;cur=Cur;
		val=Val;
	}
};

void gen(int x,int y,string cur,int val)
{
	queue<struc> q;
	
	q.push(struc(x,y,cur,val));

	while (!q.empty())
	{
		x=q.front().x;
		y=q.front().y;
		val=q.front().val;
		cur=q.front().cur;
		q.pop();

		if (val<-250||val>250)
			continue;

		if (res[x][y][val+250])
			continue;

		res[x][y][val+250]=true;
		
		if (!m.count(val))
			m[val]=cur; else
		{
			string buf=m[val];
			if (buf.length()>cur.length())
				m[val]=cur; else
			if (buf.length()==cur.length()&&buf>cur)
				m[val]=cur;
		}

		vector<pair<char,pair<int,int> > > v;
		for (int i=0;i<4;i++)
		{
			int nx=x+dx[i];
			int ny=y+dy[i];
			if (valid(nx,ny))
			{
				v.push_back(make_pair(a[nx][ny],make_pair(nx,ny)));
			}
		}
		sort(v.begin(),v.end());

		vector<pair<string,pair<int,int> > > v2;
		for (int k=0;k<(int)v.size();k++)
		{
			for (int j=0;j<4;j++)
			{
				int nnx=v[k].second.first+dx[j];
				int nny=v[k].second.second+dy[j];
				if (valid(nnx,nny))
				{
					v2.push_back(make_pair(getstr(v[k].first)+getstr(a[nnx][nny]),make_pair(nnx,nny)));
				}
			}
		}
		sort(v2.begin(),v2.end());
		for (int j=0;j<(int)v2.size();j++)
		{
			int nval=val;
			if (v2[j].first[0]=='+')
				nval=nval+v2[j].first[1]-'0'; else
				nval=nval-v2[j].first[1]+'0';

			q.push(struc(v2[j].second.first,v2[j].second.second,cur+v2[j].first,nval));
		}
	}
}

int main()
{
	freopen("in.txt","r",stdin);
	freopen("out.txt","w",stdout);
	int T;
	scanf("%d",&T);
	for (int t=0;t<T;t++)
	{
		int q;
		scanf("%d%d\n",&w,&q);
		for (int i=0;i<w;i++)
			gets(a[i]);

		for (int i=0;i<w;i++)
			for (int j=0;j<w;j++)
				for (int k=0;k<1500;k++)
					res[i][j][k]=false;

		m.clear();
		for (int i=0;i<w;i++)
			for (int j=0;j<w;j++)
				if (isdigit(a[i][j]))
					gen(i,j,getstr(a[i][j]),a[i][j]-'0');

		cout<<"Case #"<<t+1<<":"<<endl;
		int x;
		for (int i=0;i<q;i++)
		{
			scanf("%d",&x);
			cout<<m[x]<<endl;
		}
	}
	return 0;
}