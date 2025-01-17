//#undef _GLIBCXX_DEBUG

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <limits>
#include <list>
#include <map>
#include <numeric>
#include <queue>
#include <set>
#include <sstream>
#include <valarray>
#include <vector>

using namespace std;

#define foreach(iter,cont) for(typeof((cont).begin()) iter = (cont).begin(); iter != (cont).end(); ++iter)
#define min3(a, b, c) min(a, min(b, c))
#define max3(a, b, c) max(a, max(b, c))
#define inf 1000000008
#define eps 1.0e-10L
#define divisible(a, b) (((a) / (b)) * (b) == (a))
static const long long p = 1000000007;
__attribute__((unused)) static bool eq(long double a, long double b)  { return (a == b ? true : (a * b == 0 ? abs(a - b) < eps : abs(a - b) / (abs(a) + abs(b)) < eps));  }
__attribute__((unused)) static long long modp(long long n, long long m) { long long r = n % m; return r >= 0 ? r : r + m; }
__attribute__((unused)) static long long gcd(long long a, long long b) { return (a < 0 ? gcd(-a, b) : (b < 0 ? gcd(a, -b) : (!a && !b ? 1 : (b ? gcd(b, a % b) : a)))); }
template<class T> static int len(const T& cont) { return (int) cont.size(); }

// BEGIN CUT HERE
template<class T, class U> ostream &operator<<(ostream &os, const pair<T, U> &p) { return os << "(" << p.first << ", " << p.second << ")"; }
template<class T> ostream &operator<<(ostream& os, const vector<T> &vec) { os << "[ "; for(size_t i = 0; i < vec.size(); i++) { os << vec[i]; if(i != vec.size() - 1) { os << ", "; } } return os << " ]"; }
template<class T> ostream &operator<<(ostream& os, const list<T> &l) { os << "[ "; bool first = true; for(typename list<T>::const_iterator lstIt = l.begin(); lstIt != l.end(); ++lstIt) { if(!first) { os << ", "; } first = false; os << *lstIt; } return os << " ]"; }
template<class T> ostream &operator<<(ostream& os, const deque<T> &d) { os << "[ "; bool first = true; for(typename deque<T>::const_iterator dIt = d.begin(); dIt != d.end(); ++dIt) { if(!first) { os << ", "; } first = false; os << *dIt; } return os << " ]"; }
template<class T> ostream &operator<<(ostream& os, const vector<vector<T> > &mat) { for(size_t i = 0; i < mat.size(); i++) { if(i) os << endl; os << mat[i]; } return os; }
template<class T> ostream &operator<<(ostream &os, const set<T> &s) { os << "{ "; bool first = true; for(typename set<T>::const_iterator setIt = s.begin(); setIt != s.end(); ++setIt) { if(!first) { os << ", "; } first = false; os << *setIt; } return os << " }"; }
template<class T, class U> ostream &operator<<(ostream &os, const map<T, U> &m) { os << "{ "; bool first = true; for(typename map<T, U>::const_iterator mapIt = m.begin(); mapIt != m.end(); ++mapIt) { if(!first) { os << ", "; } first = false; os << mapIt->first << " = " << mapIt->second; } return os << " }"; }
// END CUT HERE

int main(__attribute__((unused)) int argc, __attribute__((unused)) char **argv) {
	int numTc;
	cin >> numTc;
	for(int tcNum = 1; tcNum <= numTc; tcNum++) {
		int a, n;
		cin >> a >> n;
		vector<int> sizes(n);
		for(int i = 0; i < n; i++) {
			cin >> sizes[i];
		}

		sort(sizes.begin(), sizes.end());
		
		vector<vector<int> > minMoves(n + 1, vector<int>(a + 1, inf));
		minMoves[0][a] = 0;

		for(int i = 1; i <= n; i++) {
			for(int prevSize = 0; prevSize < len(minMoves[i - 1]); prevSize++) {
				if(minMoves[i - 1][prevSize] == inf) {
					continue;
				}
				/*
				if(prevSize > sizes[i - 1]) {
					// Absorb as-is
					int newSize = prevSize + sizes[i - 1];
					minMoves[i].resize(max(len(minMoves[i]), newSize + 1), inf);
					minMoves[i][newSize] = min(minMoves[i][newSize], minMoves[i - 1][prevSize]);
				}
				*/
				// Repeatedly make new ones to absorb
				if(prevSize > 1) {
					int cnt = 0;
					int newSize = prevSize;
					while(newSize <= sizes[i - 1]) {
						newSize += newSize - 1;
						cnt++;
					}
					newSize += sizes[i - 1];

					minMoves[i].resize(max(len(minMoves[i]), newSize + 1), inf);
					minMoves[i][newSize] = min(minMoves[i][newSize], minMoves[i - 1][prevSize] + cnt);
				}

				// Delete
				minMoves[i].resize(max(len(minMoves[i]), prevSize + 1), inf);
				minMoves[i][prevSize] = min(minMoves[i][prevSize], minMoves[i - 1][prevSize] + 1);
			}

			//cout << minMoves[i] << endl;
		}

		cout << "Case #" << tcNum << ": " << *min_element(minMoves[n].begin(), minMoves[n].end()) << endl;
	}

	return 0;
}
