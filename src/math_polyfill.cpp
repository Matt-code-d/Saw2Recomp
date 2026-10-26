// MSVC CRT may not export C23 roundeven*; generated PPC code references them.
#include <cmath>

extern "C" float roundevenf(float x) {
  return std::nearbyintf(x);
}

extern "C" double roundeven(double x) {
  return std::nearbyint(x);
}