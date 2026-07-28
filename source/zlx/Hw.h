#pragma once
namespace ZLX {
    enum { INIT_USB = 1, INIT_ATA = 2, INIT_ATAPI = 4, INIT_FILESYSTEM = 8 };
    namespace Hw {
        inline void SystemInit(unsigned int flags) {}
        inline void SystemPoll() {}
    }
}
