#include <mach/mach_types.h>
#include <mach/kmod.h>

extern kern_return_t _start(kmod_info_t *, void *);
extern kern_return_t _stop(kmod_info_t *, void *);

KMOD_EXPLICIT_DECL(local.driver.QcaBtMac, "0.1.0", _start, _stop)

static kern_return_t qcabtmac_start(kmod_info_t *ki, void *data) {
    (void)ki;
    (void)data;
    return KERN_SUCCESS;
}

static kern_return_t qcabtmac_stop(kmod_info_t *ki, void *data) {
    (void)ki;
    (void)data;
    return KERN_SUCCESS;
}

kmod_start_func_t *_realmain = qcabtmac_start;
kmod_stop_func_t  *_antimain = qcabtmac_stop;
