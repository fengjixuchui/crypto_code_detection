#include <string>
#include <iostream>
#include <vector>
#include <fstream>
#include <sstream>
#include <map>
#include <math.h>
#include <iomanip>
#include <numeric>
#include <stdlib.h>
#include <queue>
#include <set>
#include <algorithm>
#include <cmath>
#include <string.h>
#include <stdio.h>
using namespace std;

int res[300]; int v[300]; int curr[300];

int dist(int n1, int n2, int m) {
  if (abs(n1 - n2) <= m) return 0;
  return (abs(n1 - n2) - 1) / m;
}

int main()
{
  int nc; cin >> nc; for (int cc = 0; cc < nc; cc++) {
    int del, ins, m, n; cin >> del >> ins >> m >> n;
    for (int i = 0; i < n; i++) cin >> v[i];
    for (int i = 0; i <= 255; i++) res[i] = ins; res[256] = 0;

    for (int i = 0; i < n; i++) {
      for (int j = 0; j <= 255; j++) {
        int d = abs(j - v[i]);
        curr[j] = res[256] + d;
        for (int k = 0; k <= 255; k++) if (j == k || m != 0) {
          if (m != 0) curr[j] = min(curr[j], d + res[k] + ins * dist(j, k, m));
          else curr[j] = min(curr[j], d + res[k]);
        }
      }
      for (int j = 0; j <= 255; j++) res[j] = min(del + res[j], curr[j]);
      res[256] = del + res[256];
    }

    int r = res[0]; for (int i = 1; i <= 256; i++) r = min(r, res[i]);
    cout << "Case #" << cc + 1 << ": " << r << endl;
  }
  return 0;
}
