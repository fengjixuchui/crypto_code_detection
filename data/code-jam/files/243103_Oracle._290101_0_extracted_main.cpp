#define _CRT_SECURE_NO_DEPRECATE
#include <iostream>
#include <vector>
#include <set>
#include <queue>
#include <map>
#include <stack>
#include <algorithm>
#include <numeric>
#include <string>
using namespace std;

const int MOD=10009;
char word[200][100];
int cnt_word[200][200];

map<vector<int>,int> M,next;

vector<string> getterms(string s)
{
	vector<string> res;
	while (s.find("+")!=string::npos)
	{
		res.push_back(s.substr(0,s.find("+")));
		s.erase(0,s.find("+")+1);
	}
	res.push_back(s);
	return res;
}

int K,n;

int main()
{
	freopen("in.txt","r",stdin);
	freopen("out.txt","w",stdout);

	int test;
	scanf("%d\n",&test);
	for (int T=1;T<=test;T++)
	{
		memset(cnt_word,0,sizeof(cnt_word));
		char s[100];
		scanf("%s %d\n",s,&K);
		scanf("%d\n",&n);
		for (int i=0;i<n;i++)
		{
			gets(word[i]);
			int len=strlen(word[i]);
			for (int j=0;j<len;j++)
				cnt_word[i][word[i][j]]++;
		}
		

		vector<string> v=getterms(string(s));

		printf("Case #%d: ",T);
		int res[100];
		for (int i=0;i<K;i++)
			res[i]=0;

		char t[20];
		for (int j=0;j<(int)v.size();j++)
		{
			memset(t,0,sizeof(t));
			memcpy(t,v[j].c_str(),v[j].length());
			int tLen=v[j].length();

			M.clear();
			M[vector<int>(tLen,0)]=1;

			for (int i=0;i<K;i++)
			{
				next.clear();
				for (map<vector<int>,int>::iterator it=M.begin();it!=M.end();++it)
				{
					for (int L=0;L<n;L++)
					{
						vector<int> cur=it->first;
						for (int m=0;m<tLen;m++)
						{
							cur[m]+=cnt_word[L][t[m]];
						}
						next[cur]+=it->second;
					}
				}

				M=next;
				for (map<vector<int>,int>::iterator it=M.begin();it!=M.end();++it)
				{
					int cur_res=it->second;
					for (int m=0;m<tLen;m++)
						cur_res=(cur_res*it->first[m])%MOD;

					res[i]=res[i]+cur_res;
					if (res[i]>=MOD)
						res[i]-=MOD;
				}
			}
		}
		for (int i=0;i<K;i++)
			printf("%d%c",res[i],((i==K-1)?'\n':' '));
	}
	return 0;
}