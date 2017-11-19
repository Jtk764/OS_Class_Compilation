#ifndef DECIMALS
#define DECIMALS

#define P 17
#define Q 14
#define F 1 << (Q)
/* gotten from Pintos B.6 */
#define CONVERT_TO_FP(n) (n) * (F)
#define CONVERT_TO_INT_ZERO(x) (x) / (F)
#define CONVERT_TO_INT_NEAREST(x) ((x) >= 0 ? ((x) + (F) / 2)\
                                   / (F) : ((x) - (F) / 2)\
                                   / (F))
#define ADD(x, y) (x) + (y)
#define SUB(x, y) (x) - (y)
#define ADD_INT(x, n) (x) + (n) * (F)
#define SUB_INT(x, n) (x) - (n) * (F)
#define MULTIPLE(x, y) ((int64_t)(x)) * (y) / (F)
#define MULT_INT(x, n) (x) * (n)
#define DIVIDE(x, y) ((int64_t)(x)) * (F) / (y)
#define DIV_INT(x, n) (x) / (n)

#endif
/* <## */