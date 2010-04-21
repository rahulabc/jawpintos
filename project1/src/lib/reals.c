#include <reals.h>
#include <stdint.h>
#define FIXED_P 17
#define FIXED_Q 14

int 
int_to_real(int n) 
{
  return n * (1 << FIXED_Q);
}

/* floor(x) */
int 
floor_real_to_int(int x) 
{ 
  return x / (1 << FIXED_Q);
}

/* round(x) */
int 
round_real_to_int(int x) 
{  
  if (x >= 0) 
    {
      return (x + (1 << FIXED_Q) / 2) / (1 << FIXED_Q);
    }
  return (x - (1 << FIXED_Q) / 2) / (1 << FIXED_Q);
}

/* x + y */
int 
sum_reals(int x, int y) 
{ 
  return x + y;
}

/* x - y */
int 
diff_reals(int x, int y) 
{ 
  return x - y;
}

/* x + n * f */
int 
sum_real_int(int x, int n) 
{ 
  return x + n * (1 << FIXED_Q);
}

/* x - n * f */
int 
diff_real_int(int x, int n) 
{ 
  return x - n * (1 << FIXED_Q);
}

/* x * y / f */
int 
mult_reals(int x, int y) 
{ 
  return ((int64_t) x) * y / (1 << FIXED_Q);
}

/* x * n */
int 
mult_real_int(int x, int n) 
{ 
  return x * n;
}

/* x / y */
int 
div_reals(int x, int y) 
{ 
  return ((int64_t) x) * (1 << FIXED_Q) / y;
}

/* x / n */
int 
div_real_int(int x, int n) 
{ 
  return x / n;
}

