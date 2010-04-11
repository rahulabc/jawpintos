#include <reals.h>
#include <stdint.h>
#define FIXED_P 17
#define FIXED_Q 14

int int_to_real(int n) {
  return n*(1<<FIXED_Q);
}

int floor_real_to_int(int x) { // floor(x)
  return x/(1<<FIXED_Q);
}

int round_real_to_int(int x) { // round(x)
  if(x>=0) {
    return (x+(1<<FIXED_Q)/2)/(1<<FIXED_Q);
  }
  return (x-(1<<FIXED_Q)/2)/(1<<FIXED_Q);
}

int sum_reals(int x, int y) { // x + y
  return x+y;
}

int diff_reals(int x, int y) { // x - y
  return x-y;
}

int sum_real_int(int x, int n) { // x + n*f
  return x+n*(1<<FIXED_Q);
}

int diff_real_int(int x, int n) { // x - n*f
  return x-n*(1<<FIXED_Q);
}

int mult_reals(int x, int y) { // x*y/f
  return ((int64_t)x)*y/(1<<FIXED_Q);
}

int mult_real_int(int x, int n) { // x*n
  return x*n;
}

int div_reals(int x, int y) { // x/y
  return ((int64_t)x)*(1<<FIXED_Q)/y;
}

int div_real_int(int x, int n) { // x/n
  return x/n;
}

