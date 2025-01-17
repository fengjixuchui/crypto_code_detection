#include <vector>
#include <list>
#include <map>
#include <set>
#include <queue>
#include <deque>
#include <stack>
#include <bitset>
#include <algorithm>
#include <functional>
#include <numeric>
#include <utility>
#include <sstream>
#include <iostream>
#include <iomanip>
#include <cstdio>
#include <cmath>
#include <cstdlib>
#include <ctime>
#include <cstring>
#include <cassert>

using namespace std;

typedef long long LL;
typedef pair<int, int> PII;
#define MP make_pair
#define FOR(v,p,k) for(int v=p;v<=k;++v)
#define FORD(v,p,k) for(int v=p;v>=k;--v)
#define REP(i,n) for(int i=0;i<(n);++i)
#define VAR(v,i) __typeof(i) v=(i)
#define FOREACH(i,c) for(__typeof(c.begin()) i=(c.begin());i!=(c).end();++i)
#define PB push_back
#define ST first
#define ND second
#define SIZE(x) (int)x.size()
#define ALL(c) c.begin(),c.end()
#define ZERO(x) memset(x,0,sizeof(x))

LL h[22][22];
LL lev[22][22];

const LL LINF = 1000000000LL * 1000000000LL;

int height, width;


void print(LL t[22][22]) {
	for (int y = 1; y <= height; ++y) {
		for (int x = 1; x <= width; ++x)
			printf("%4lld", t[y][x]);
		printf("\n");
	}
	printf("\n");
}

const int dy[] = {-1, 0, 1, 0};
const int dx[] = {0, 1, 0, -1};

void findLevels() {
	priority_queue<pair<int, PII>, vector<pair<int, PII> >, greater<pair<int, PII> > > q;
	ZERO(lev);
	for (int y = 0; y <= height + 1; ++y)
		for (int x = 0; x <= width + 1; ++x) {
			if (y == 0 || y == height + 1 || x == 0 || x == width + 1)
				q.push(MP(0, MP(y, x)));
			else
				lev[y][x] = LINF;
		}
	while (!q.empty()) {
		int y = q.top().second.first;
		int x = q.top().second.second;
		LL cd = q.top().first;
		q.pop();
		if (cd > lev[y][x])
			continue;
		for (int d = 0; d < 4; ++d) {
			int cy = y + dy[d];
			int cx = x + dx[d];
			if (cy >= 1 && cy <= height && cx >= 1 && cx <= width) {
				LL nd = max(cd, h[cy][cx]);
				if (nd < lev[cy][cx]) {
					lev[cy][cx] = nd;
					q.push(MP(nd, MP(cy, cx)));
				}
			}
		}
	}
}

bool changing[22][22];

LL rounds;

LL m;

bool erode() {
	++rounds;
	findLevels();
	ZERO(changing);
	bool allByM = true;
	for (int y = 1; y <= height; ++y) {
		for (int x = 1; x <= width; ++x) {
			LL mn = lev[y][x];
			for (int d = 0; d < 4; ++d) {
				int cy = y + dy[d];
				int cx = x + dx[d];
				mn = min(mn, lev[cy][cx]);
			}
			LL dif = min(lev[y][x] - mn, m);
			changing[y][x] = (dif > 0);
			h[y][x] -= dif;
			if (dif != 0 && dif != m)
				allByM = false;
		}
	}
	return allByM;
}

bool allZero() {
	for (int y = 1; y <= height; ++y)
		for (int x = 1; x <= width; ++x)
			if (h[y][x] > 0)
				return false;
	return true;
}

void push() {
	if (allZero())
		return;
	while (!erode()) {
		if (allZero())
			return;
	}
	if (allZero())
		return;
	findLevels();
	LL roundsToPass = LINF;
	for (int y = 1; y <= height; ++y) {
		for (int x = 1; x <= width; ++x) {
			if (!changing[y][x])
				continue;
			assert(lev[y][x] == h[y][x]);
			for (int d = 0; d < 4; ++d) {
				int cy = y + dy[d];
				int cx = x + dx[d];
				if (!changing[cy][cx]) {
					if (h[y][x] < h[cy][cx])
						roundsToPass = 0;
					else
						roundsToPass = min(roundsToPass, (h[y][x] - h[cy][cx]) / m);
				}
			}
		}
	}
	rounds += roundsToPass;
	for (int y = 1; y <= height; ++y) {
		for (int x = 1; x <= width; ++x) {
			if (changing[y][x]) {
				h[y][x] -= roundsToPass * m;
			}
		}
	}
}

void alg(int testCase) {
	scanf("%d %d %lld", &height, &width, &m);
	ZERO(h);
	for (int y = 1; y <= height; ++y)
		for (int x = 1; x <= width; ++x)
			scanf("%lld", &h[y][x]);
	rounds = 0;
	while (!allZero())
		push();
	printf("Case #%d: %lld\n", testCase, rounds);
}

int main() {
	int d;
	scanf("%d", &d);
	for (int testCase = 1; testCase <= d; ++testCase) {
		alg(testCase);
	}
}

