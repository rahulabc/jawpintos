#ifndef __LIB_REALS_H__
#define __LIB_REALS_H__

int int_to_real(int n);
int floor_real_to_int(int x); // floor(x)
int round_real_to_int(int x); // round(x)
int sum_reals(int x, int y); // x + y
int diff_reals(int x, int y); // x - y
int sum_real_int(int x, int n); // x + n*f
int diff_real_int(int x, int n); // x - n*f
int mult_reals(int x, int y); // x*y/f
int mult_real_int(int x, int n); // x*n
int div_reals(int x, int y); // x/y
int div_real_int(int x, int n); // x/n

#endif // __LIB_REALS_H__
