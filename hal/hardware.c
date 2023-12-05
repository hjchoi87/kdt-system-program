#include <string.h>
#include <stdio.h>
#include <pthread.h>
#include <errno.h>
#include <limits.h>
#include <dlfcn.h>

#include <hardware.h>

#define HAL_LIBRARY_PATH1 "./libcamera.so"

static int load(const struct hw_module_t **pHmi)
{
    int status;
    void *handle;
    struct hw_module_t *hmi;

    handle = dlopen(HAL_LIBRARY_PATH1, RTLD_NOW);
    if (handle == NULL) {
        char const *err_str = dlerror();
        printf("load: module\n%s", err_str?err_str:"unknown");
        status = -EINVAL;
        hmi = NULL;
        if (handle != NULL) {
            dlclose(handle);
            handle = NULL;
        }
        return status;
    }

    const char *sym = HAL_MODULE_INFO_SYM_AS_STR;
    hmi = (struct hw_module_t *)dlsym(handle, sym);
    if (hmi == NULL) {
        printf("load: couldn't find symbol %s", sym);
        status = -EINVAL;
        hmi = NULL;
        if (handle != NULL) {
            dlclose(handle);
            handle = NULL;
        }
        return status;
    }

    printf("loaded HAL hmi=%p handle=%p", *pHmi, handle);

    *pHmi = hmi;

    return status;
}

int hw_get_camera_module(const struct hw_module_t **module)
{
    return load(module);
}
