#include <string.h>

size_t strlen(const char *str)
{
    size_t i = 0;
    for (; str[i] != '\0'; ++i)
        ;
    return i;
}

/**
 * @brief 复制字符 ch 到参数 src 所指向的字符串的前 cnt 个字符（也可用于内存填充）。
 *
 * @param src 起始地址
 * @param ch 填充内容
 * @param cnt 大小
 * 
 * @return 返回起始地址 src
 */
void *memset(void *src, char ch, size_t cnt)
{
    /** TODO: 自 src 起填充内存内容为 cnt 个 ch */
}
