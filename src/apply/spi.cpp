#include "apply/spi.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

namespace cs::apply {

// SPI_SETCURSORS 的定义值。用 static_assert 把「我们以为的值」钉住：
// 若某天 SDK 头里的值变了，编译期就会炸，而不是运行时静默错一个号。
static_assert(SPI_SETCURSORS == 0x0057, "SPI_SETCURSORS value drifted from expectation");

SpiResult set_cursors_from_registry(uint32_t flags) {
    SpiResult r;
    SetLastError(0);
    // uiParam / pvParam 按文档对 SPI_SETCURSORS 的要求传 0 / NULL。
    const BOOL ok = SystemParametersInfoW(SPI_SETCURSORS, 0, nullptr, flags);
    r.performed = true;
    r.called_ok = (ok != 0);
    r.last_error = GetLastError();
    return r;
}

}  // namespace cs::apply
