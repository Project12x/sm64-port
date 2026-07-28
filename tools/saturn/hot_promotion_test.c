#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "ztreme_hot_promotion.h"

int main(void)
{
    uint8_t source[32];
    uint8_t destination[32];
    saturn_hot_promotion_t promotion;
    for (uint8_t i = 0; i < sizeof(source); i++) source[i] = i;
    memset(destination, 0xA5, sizeof(destination));
    saturn_hot_promotion_init(&promotion, destination, sizeof(destination));

    uint8_t *first = saturn_hot_promote(&promotion, source, 7U, 8U);
    if (first != destination || promotion.used != 7U ||
        memcmp(first, source, 7U) != 0) return 1;
    uint8_t *second = saturn_hot_promote(&promotion, source + 7U, 9U, 16U);
    if (second != destination + 16U || promotion.used != 25U ||
        memcmp(second, source + 7U, 9U) != 0) return 2;
    if (saturn_hot_promote(&promotion, source, 8U, 0U) != NULL ||
        saturn_hot_promote(&promotion, source, 16U, 3U) != NULL ||
        saturn_hot_promote(&promotion, source, 16U, 16U) != NULL)
        return 3;
    if (saturn_hot_promotion_used(&promotion) != 25U) return 4;
    puts("hot promotion contract: PASS");
    return 0;
}
