#include <stdint.h>
#include "bob_bsp.h"
int main(void)
{
    return sm64_saturn_bob_bsp_bounds_min[0][0] <=
        sm64_saturn_bob_bsp_bounds_max[0][0] &&
        sm64_saturn_bob_bsp_work_weight[0] != 0U ? 0 : 1;
}
