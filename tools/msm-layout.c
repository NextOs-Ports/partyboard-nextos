#include <stddef.h>
#include <stdio.h>

#include "msm/msmsys.h"

#define FIELD(type, member) printf(#type "." #member "=%zu\n", offsetof(type, member))

int main(void) {
    printf("sizeof(MSM_SYS)=%zu\n", sizeof(MSM_SYS));
    FIELD(MSM_SYS, header);
    FIELD(MSM_SYS, info);
    FIELD(MSM_SYS, grpInfo);
    FIELD(MSM_SYS, baseGrpNum);
    FIELD(MSM_SYS, grpData);
    FIELD(MSM_SYS, grpBufA);
    FIELD(MSM_SYS, grpStackA);
    FIELD(MSM_SYS, grpBufB);
    FIELD(MSM_SYS, grpStackB);
    FIELD(MSM_SYS, sampSize);
    FIELD(MSM_SYS, sampSizeBase);
    printf("sizeof(MSM_GRP_STACK)=%zu\n", sizeof(MSM_GRP_STACK));
    FIELD(MSM_GRP_STACK, buf);
    return 0;
}
