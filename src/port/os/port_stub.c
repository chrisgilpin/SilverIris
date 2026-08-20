/* Dummy host OS / libultra symbols so ge_sim can link (PR-05a). Real
 * implementations land in later PRs. */
#include <stdio.h>

int main(void)
{
    puts("silveriris port_stub: ge_sim linked");
    return 0;
}
